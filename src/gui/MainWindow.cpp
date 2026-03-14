#include "MainWindow.h"
#include "FrequencyDial.h"
#include "ConnectionPanel.h"
#include "SpectrumWidget.h"
#include "AppletPanel.h"
#include "RxApplet.h"
#include "SMeterWidget.h"
#include "TunerApplet.h"
#include "TxApplet.h"
#include "PhoneCwApplet.h"
#include "PhoneApplet.h"
#include "EqApplet.h"
#include "models/SliceModel.h"
#include "models/MeterModel.h"
#include "models/TunerModel.h"
#include "models/TransmitModel.h"
#include "models/EqualizerModel.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QGroupBox>
#include <QDockWidget>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QSlider>
#include <QProgressBar>
#include <QCloseEvent>
#include <QMessageBox>
#include <QSettings>
#include <QDebug>

namespace AetherSDR {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("AetherSDR");
    setMinimumSize(1024, 600);
    resize(1400, 800);

    applyDarkTheme();
    buildMenuBar();
    buildUI();

    // ── Wire up discovery ──────────────────────────────────────────────────
    connect(&m_discovery, &RadioDiscovery::radioDiscovered,
            m_connPanel, &ConnectionPanel::onRadioDiscovered);
    connect(&m_discovery, &RadioDiscovery::radioUpdated,
            m_connPanel, &ConnectionPanel::onRadioUpdated);
    connect(&m_discovery, &RadioDiscovery::radioLost,
            m_connPanel, &ConnectionPanel::onRadioLost);

    connect(m_connPanel, &ConnectionPanel::connectRequested,
            this, [this](const RadioInfo& info){
        m_connPanel->setStatusText("Connecting…");
        m_radioModel.connectToRadio(info);
    });
    connect(m_connPanel, &ConnectionPanel::disconnectRequested,
            this, [this]{ m_radioModel.disconnectFromRadio(); });

    // ── Wire up radio model ────────────────────────────────────────────────
    connect(&m_radioModel, &RadioModel::connectionStateChanged,
            this, &MainWindow::onConnectionStateChanged);
    connect(&m_radioModel, &RadioModel::connectionError,
            this, &MainWindow::onConnectionError);
    connect(&m_radioModel, &RadioModel::sliceAdded,
            this, &MainWindow::onSliceAdded);
    connect(&m_radioModel, &RadioModel::sliceRemoved,
            this, &MainWindow::onSliceRemoved);

    // ── Panadapter stream → spectrum widget ───────────────────────────────
    connect(m_radioModel.panStream(), &PanadapterStream::spectrumReady,
            m_spectrum, &SpectrumWidget::updateSpectrum);
    connect(m_radioModel.panStream(), &PanadapterStream::waterfallRowReady,
            m_spectrum, &SpectrumWidget::updateWaterfallRow);
    connect(&m_radioModel, &RadioModel::panadapterInfoChanged,
            m_spectrum, &SpectrumWidget::setFrequencyRange);
    connect(&m_radioModel, &RadioModel::panadapterLevelChanged,
            m_spectrum, &SpectrumWidget::setDbmRange);

    // ── Click-to-tune on the spectrum ─────────────────────────────────────
    connect(m_spectrum, &SpectrumWidget::frequencyClicked,
            this, &MainWindow::onFrequencyChanged);

    // ── Panadapter stream → audio engine ──────────────────────────────────
    // All VITA-49 traffic arrives on the single client udpport socket owned
    // by PanadapterStream. It strips the header from IF-Data packets and emits
    // audioDataReady(); we feed that directly to the QAudioSink.
    connect(m_radioModel.panStream(), &PanadapterStream::audioDataReady,
            &m_audio, &AudioEngine::feedAudioData);

    // ── AF gain from applet panel → audio engine ──────────────────────────
    connect(m_appletPanel->rxApplet(), &RxApplet::afGainChanged, this, [this](int v) {
        m_audio.setRxVolume(v / 100.0f);
    });

    // ── Tuning step size → spectrum widget + freq dial ────────────────────
    connect(m_appletPanel->rxApplet(), &RxApplet::stepSizeChanged,
            m_spectrum, &SpectrumWidget::setStepSize);
    connect(m_appletPanel->rxApplet(), &RxApplet::stepSizeChanged,
            m_freqDial, &FrequencyDial::setStepSize);
    // Initialise both with the applet's default step (100 Hz, index 2).
    m_spectrum->setStepSize(100);
    m_freqDial->setStepSize(100);

    // ── Antenna list from radio → applet panel ─────────────────────────────
    connect(&m_radioModel, &RadioModel::antListChanged,
            m_appletPanel, &AppletPanel::setAntennaList);

    // ── S-Meter: MeterModel → SMeterWidget ────────────────────────────────
    connect(m_radioModel.meterModel(), &MeterModel::sLevelChanged,
            m_appletPanel->sMeterWidget(), &SMeterWidget::setLevel);

    // ── Tuner: MeterModel TX meters → TunerApplet gauges ────────────────
    connect(m_radioModel.meterModel(), &MeterModel::txMetersChanged,
            m_appletPanel->tunerApplet(), &TunerApplet::updateMeters);
    m_appletPanel->tunerApplet()->setTunerModel(m_radioModel.tunerModel());
    m_appletPanel->tunerApplet()->setMeterModel(m_radioModel.meterModel());

    // Show/hide TUNE button + applet based on TGXL presence
    connect(m_radioModel.tunerModel(), &TunerModel::presenceChanged,
            m_appletPanel, &AppletPanel::setTunerVisible);

    // Switch Fwd Power gauge scale when a power amplifier (PGXL) is detected
    connect(&m_radioModel, &RadioModel::amplifierChanged,
            m_appletPanel->tunerApplet(), &TunerApplet::setAmplifierMode);

    // ── TX applet: meters + model ───────────────────────────────────────────
    connect(m_radioModel.meterModel(), &MeterModel::txMetersChanged,
            m_appletPanel->txApplet(), &TxApplet::updateMeters);
    m_appletPanel->txApplet()->setTransmitModel(m_radioModel.transmitModel());

    // ── P/CW applet: mic meters + model ──────────────────────────────────────
    connect(m_radioModel.meterModel(), &MeterModel::micMetersChanged,
            m_appletPanel->phoneCwApplet(), &PhoneCwApplet::updateMeters);
    m_appletPanel->phoneCwApplet()->setTransmitModel(m_radioModel.transmitModel());

    // ── PHNE applet: VOX + CW controls ──────────────────────────────────────
    m_appletPanel->phoneApplet()->setTransmitModel(m_radioModel.transmitModel());

    // ── EQ applet: graphic equalizer ─────────────────────────────────────────
    m_appletPanel->eqApplet()->setEqualizerModel(m_radioModel.equalizerModel());

    // ── Audio level meter ──────────────────────────────────────────────────
    connect(&m_audio, &AudioEngine::levelChanged,
            this, &MainWindow::onAudioLevel);

    // Start discovery
    m_discovery.startListening();

    // Restore saved geometry
    QSettings settings("AetherSDR", "AetherSDR");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings("AetherSDR", "AetherSDR");
    settings.setValue("geometry",    saveGeometry());
    settings.setValue("windowState", saveState());
    m_discovery.stopListening();
    m_radioModel.disconnectFromRadio();
    m_audio.stopRxStream();
    QMainWindow::closeEvent(event);
}

// ─── UI Construction ──────────────────────────────────────────────────────────

void MainWindow::buildMenuBar()
{
    auto* fileMenu = menuBar()->addMenu("&File");
    auto* quitAct = fileMenu->addAction("&Quit");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* themeAct = viewMenu->addAction("Toggle Dark/Light Theme");
    connect(themeAct, &QAction::triggered, this, [this]{
        // Placeholder — full theme switching left as an exercise
        applyDarkTheme();
    });

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("About AetherSDR", this, [this]{
        QMessageBox::about(this, "About AetherSDR",
            "<b>AetherSDR</b> v0.1<br>"
            "Linux-native SmartSDR-compatible client.<br>"
            "Built with Qt6 and C++20.");
    });
}

void MainWindow::buildUI()
{
    // ── Central splitter: [sidebar | main area] ────────────────────────────
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(splitter);

    // Left sidebar — connection panel
    m_connPanel = new ConnectionPanel(splitter);
    m_connPanel->setFixedWidth(260);
    splitter->addWidget(m_connPanel);

    // Centre — spectrum + controls
    auto* rightWidget = new QWidget(splitter);
    auto* rightVBox   = new QVBoxLayout(rightWidget);
    rightVBox->setContentsMargins(4, 4, 4, 4);
    rightVBox->setSpacing(6);
    splitter->addWidget(rightWidget);
    splitter->setStretchFactor(1, 1);

    // Right — applet panel (includes S-Meter)
    m_appletPanel = new AppletPanel(splitter);
    splitter->addWidget(m_appletPanel);
    splitter->setStretchFactor(2, 0);

    // Spectrum/panadapter
    m_spectrum = new SpectrumWidget(rightWidget);
    rightVBox->addWidget(m_spectrum, 1);

    // ── Controls strip ─────────────────────────────────────────────────────
    auto* ctrlGroup = new QGroupBox("Slice 0", rightWidget);
    auto* ctrlBox   = new QHBoxLayout(ctrlGroup);
    ctrlBox->setSpacing(12);
    rightVBox->addWidget(ctrlGroup);

    // Frequency dial
    m_freqDial = new FrequencyDial(ctrlGroup);
    ctrlBox->addWidget(m_freqDial);

    // Mode selector
    ctrlBox->addWidget(new QLabel("Mode:", ctrlGroup));
    m_modeCombo = new QComboBox(ctrlGroup);
    m_modeCombo->addItems({"USB", "LSB", "CW", "CWL", "AM", "SAM", "FM", "NFM", "DIG", "DIGU", "DIGL"});
    ctrlBox->addWidget(m_modeCombo);

    // Volume
    ctrlBox->addWidget(new QLabel("Vol:", ctrlGroup));
    m_volumeSlider = new QSlider(Qt::Horizontal, ctrlGroup);
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setValue(70);
    m_volumeSlider->setFixedWidth(100);
    ctrlBox->addWidget(m_volumeSlider);

    // Audio level meter
    m_audioLevel = new QProgressBar(ctrlGroup);
    m_audioLevel->setRange(0, 100);
    m_audioLevel->setValue(0);
    m_audioLevel->setFixedWidth(80);
    m_audioLevel->setTextVisible(false);
    m_audioLevel->setStyleSheet(
        "QProgressBar::chunk { background: qlineargradient(x1:0,y1:0,x2:1,y2:0,"
        "stop:0 #00e5ff, stop:0.7 #00ff88, stop:1 #ff4444); }");
    ctrlBox->addWidget(m_audioLevel);

    // Mute button
    m_muteButton = new QPushButton("🔇", ctrlGroup);
    m_muteButton->setCheckable(true);
    m_muteButton->setFixedWidth(40);
    ctrlBox->addWidget(m_muteButton);

    // TX button
    m_txButton = new QPushButton("TX", ctrlGroup);
    m_txButton->setCheckable(true);
    m_txButton->setStyleSheet(
        "QPushButton:checked { background-color: #cc2200; color: white; font-weight: bold; }");
    m_txButton->setFixedWidth(60);
    ctrlBox->addWidget(m_txButton);

    ctrlBox->addStretch();

    // ── Status bar ─────────────────────────────────────────────────────────
    m_connStatusLabel = new QLabel("Disconnected", this);
    m_radioInfoLabel  = new QLabel("", this);
    statusBar()->addWidget(m_connStatusLabel);
    statusBar()->addPermanentWidget(m_radioInfoLabel);

    // ── Connect GUI signals ─────────────────────────────────────────────────
    connect(m_freqDial, &FrequencyDial::frequencyChanged,
            this, &MainWindow::onFrequencyChanged);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);
    connect(m_volumeSlider, &QSlider::valueChanged,
            this, &MainWindow::onVolumeChanged);
    connect(m_muteButton, &QPushButton::toggled,
            this, &MainWindow::onMuteToggled);
    connect(m_txButton, &QPushButton::toggled,
            this, &MainWindow::onTxToggled);
}

// ─── Theme ────────────────────────────────────────────────────────────────────

void MainWindow::applyDarkTheme()
{
    setStyleSheet(R"(
        QWidget {
            background-color: #0f0f1a;
            color: #c8d8e8;
            font-family: "Inter", "Segoe UI", sans-serif;
            font-size: 13px;
        }
        QGroupBox {
            border: 1px solid #203040;
            border-radius: 4px;
            margin-top: 8px;
            padding-top: 8px;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 8px;
            color: #00b4d8;
        }
        QPushButton {
            background-color: #1a2a3a;
            border: 1px solid #203040;
            border-radius: 4px;
            padding: 4px 10px;
            color: #c8d8e8;
        }
        QPushButton:hover  { background-color: #203040; }
        QPushButton:pressed { background-color: #00b4d8; color: #000; }
        QComboBox {
            background-color: #1a2a3a;
            border: 1px solid #203040;
            border-radius: 4px;
            padding: 3px 6px;
        }
        QComboBox::drop-down { border: none; }
        QListWidget {
            background-color: #111120;
            border: 1px solid #203040;
            alternate-background-color: #161626;
        }
        QListWidget::item:selected { background-color: #00b4d8; color: #000; }
        QSlider::groove:horizontal {
            height: 4px;
            background: #203040;
            border-radius: 2px;
        }
        QSlider::handle:horizontal {
            width: 14px; height: 14px;
            margin: -5px 0;
            background: #00b4d8;
            border-radius: 7px;
        }
        QMenuBar { background-color: #0a0a14; }
        QMenuBar::item:selected { background-color: #1a2a3a; }
        QMenu { background-color: #111120; border: 1px solid #203040; }
        QMenu::item:selected { background-color: #00b4d8; color: #000; }
        QStatusBar { background-color: #0a0a14; border-top: 1px solid #203040; }
        QProgressBar {
            background-color: #111120;
            border: 1px solid #203040;
            border-radius: 3px;
        }
        QSplitter::handle { background-color: #203040; width: 2px; }
    )");
}

// ─── Radio/model event handlers ───────────────────────────────────────────────

void MainWindow::onConnectionStateChanged(bool connected)
{
    m_connPanel->setConnected(connected);
    if (connected) {
        const QString info = QString("%1  %2")
            .arg(m_radioModel.model(), m_radioModel.version());
        m_connStatusLabel->setText("Connected");
        m_radioInfoLabel->setText(info);
        m_connPanel->setStatusText("Connected");
        m_audio.startRxStream();
    } else {
        m_connStatusLabel->setText("Disconnected");
        m_radioInfoLabel->setText("");
        m_connPanel->setStatusText("Not connected");
        m_audio.stopRxStream();
    }
}

void MainWindow::onConnectionError(const QString& msg)
{
    m_connPanel->setStatusText("Error: " + msg);
    m_connStatusLabel->setText("Error");
    statusBar()->showMessage("Connection error: " + msg, 5000);
}

void MainWindow::onSliceAdded(SliceModel* s)
{
    qDebug() << "MainWindow: slice added" << s->sliceId();
    // Update controls to reflect the first (active) slice
    if (m_radioModel.slices().size() == 1) {
        updateSliceControls(s);
        m_appletPanel->setSlice(s);
    }

    // Forward slice frequency/mode changes → controls (guard prevents echo back to radio)
    connect(s, &SliceModel::frequencyChanged, this, [this](double mhz){
        m_updatingFromModel = true;
        m_spectrum->setSliceFrequency(mhz);
        m_freqDial->setFrequency(mhz);
        m_updatingFromModel = false;
    });
    connect(s, &SliceModel::filterChanged, m_spectrum, &SpectrumWidget::setSliceFilter);
}

void MainWindow::onSliceRemoved(int /*id*/) {}

void MainWindow::updateSliceControls(SliceModel* s)
{
    if (!s) return;
    m_updatingFromModel = true;
    m_freqDial->setFrequency(s->frequency());
    m_spectrum->setSliceFrequency(s->frequency());
    m_spectrum->setSliceFilter(s->filterLow(), s->filterHigh());
    const int modeIdx = m_modeCombo->findText(s->mode());
    if (modeIdx >= 0) m_modeCombo->setCurrentIndex(modeIdx);
    m_updatingFromModel = false;
}

SliceModel* MainWindow::activeSlice() const
{
    const auto& slices = m_radioModel.slices();
    return slices.isEmpty() ? nullptr : slices.first();
}

// ─── GUI control handlers ─────────────────────────────────────────────────────

void MainWindow::onFrequencyChanged(double mhz)
{
    // If the slice is locked, snap all GUI elements back to the current freq.
    if (auto* s = activeSlice(); s && s->isLocked()) {
        m_updatingFromModel = true;
        m_freqDial->setFrequency(s->frequency());
        m_spectrum->setSliceFrequency(s->frequency());
        m_updatingFromModel = false;
        return;
    }

    m_spectrum->setSliceFrequency(mhz);
    if (!m_updatingFromModel) {
        if (auto* s = activeSlice())
            s->setFrequency(mhz);
    }
}

void MainWindow::onModeChanged(int /*index*/)
{
    if (m_updatingFromModel) return;
    const QString mode = m_modeCombo->currentText();
    if (auto* s = activeSlice())
        s->setMode(mode);
}

void MainWindow::onVolumeChanged(int value)
{
    m_audio.setRxVolume(value / 100.0f);
}

void MainWindow::onMuteToggled(bool muted)
{
    m_audio.setMuted(muted);
    m_muteButton->setText(muted ? "🔇" : "🔊");
}

void MainWindow::onTxToggled(bool tx)
{
    m_radioModel.setTransmit(tx);
}

void MainWindow::onAudioLevel(float rms)
{
    m_audioLevel->setValue(static_cast<int>(rms * 100));
}

} // namespace AetherSDR
