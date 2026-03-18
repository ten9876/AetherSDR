#include "MainWindow.h"
#include "ConnectionPanel.h"
#include "PanadapterApplet.h"
#include "SpectrumWidget.h"
#include "SpectrumOverlayMenu.h"
#include "VfoWidget.h"
#include "AppletPanel.h"
#include "RxApplet.h"
#include "SMeterWidget.h"
#include "TunerApplet.h"
#include "TxApplet.h"
#include "PhoneCwApplet.h"
#include "PhoneApplet.h"
#include "EqApplet.h"
#include "CatApplet.h"
#include "AntennaGeniusApplet.h"
#include "RadioSetupDialog.h"
#include "NetworkDiagnosticsDialog.h"
#include "MemoryDialog.h"
#include "SpotSettingsDialog.h"
#include "ProfileManagerDialog.h"
#include "models/SliceModel.h"
#include "models/MeterModel.h"
#include "models/TunerModel.h"
#include "models/TransmitModel.h"
#include "models/EqualizerModel.h"

#include <QApplication>
#include <QTimer>
#include <QDateTime>
#include <QPropertyAnimation>
#include <QIcon>
#include <QPixmap>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QCloseEvent>
#include <QMessageBox>
#include <QProgressDialog>
#include <QThread>
#include "core/AppSettings.h"
#include <QDebug>

namespace AetherSDR {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("AetherSDR");
    setWindowIcon(QIcon(":/icon.png"));
    setMinimumSize(1024, 600);
    resize(1400, 800);

    applyDarkTheme();
    buildMenuBar();
    buildUI();

    // ── Wire up discovery ──────────────────────────────────────────────────
    // ── Collapsible connection panel ─────────────────────────────────────
    connect(m_connPanel, &ConnectionPanel::collapsedChanged,
            this, [this](bool collapsed) {
        auto sizes = m_splitter->sizes();
        if (collapsed) {
            sizes[1] += sizes[0] - 28;
            sizes[0] = 28;
        } else {
            m_userExpandedPanel = true;
            sizes[1] -= (260 - sizes[0]);
            sizes[0] = 260;
        }
        m_splitter->setSizes(sizes);
        auto& ss = AppSettings::instance();
        ss.setValue("ConnPanelCollapsed", collapsed ? "True" : "False");
        ss.save();
    });

    connect(&m_discovery, &RadioDiscovery::radioDiscovered,
            m_connPanel, &ConnectionPanel::onRadioDiscovered);
    connect(&m_discovery, &RadioDiscovery::radioUpdated,
            m_connPanel, &ConnectionPanel::onRadioUpdated);
    connect(&m_discovery, &RadioDiscovery::radioLost,
            m_connPanel, &ConnectionPanel::onRadioLost);

    connect(m_connPanel, &ConnectionPanel::connectRequested,
            this, [this](const RadioInfo& info){
        m_connPanel->setStatusText("Connecting…");
        m_userDisconnected = false;
        m_radioModel.connectToRadio(info);
        auto& s = AppSettings::instance();
        s.setValue("LastConnectedRadioSerial", info.serial);
        if (info.isRouted) {
            s.setValue("LastRoutedRadioIp", info.address.toString());
        } else {
            s.remove("LastRoutedRadioIp");
        }
        s.save();
    });

    // Auto-connect: when a radio is discovered, check if it matches the last one
    connect(&m_discovery, &RadioDiscovery::radioDiscovered,
            this, [this](const RadioInfo& info) {
        if (m_userDisconnected) return;
        const QString lastSerial = AppSettings::instance()
            .value("LastConnectedRadioSerial").toString();
        if (!lastSerial.isEmpty() && info.serial == lastSerial
            && !m_radioModel.isConnected()) {
            qDebug() << "Auto-connecting to" << info.displayName();
            m_connPanel->setStatusText("Auto-connecting…");
            m_radioModel.connectToRadio(info);
        }
    });
    connect(m_connPanel, &ConnectionPanel::disconnectRequested,
            this, [this]{
        m_userDisconnected = true;
        auto& s = AppSettings::instance();
        s.remove("LastConnectedRadioSerial");
        s.remove("LastRoutedRadioIp");
        s.save();
        m_radioModel.disconnectFromRadio();
    });

    // ── SmartLink ──────────────────────────────────────────────────────────
    m_connPanel->setSmartLinkClient(&m_smartLink);

    connect(m_connPanel, &ConnectionPanel::smartLinkLoginRequested,
            this, [this](const QString& email, const QString& pass) {
        m_smartLink.login(email, pass);
    });

    // WAN radio connect: ask SmartLink server for a handle, then TLS to radio
    connect(m_connPanel, &ConnectionPanel::wanConnectRequested,
            this, [this](const WanRadioInfo& info) {
        m_connPanel->setStatusText("Requesting SmartLink connection…");
        // Store WAN radio info for when connect_ready arrives
        m_pendingWanRadio = info;
        m_smartLink.requestConnect(info.serial);
    });

    // SmartLink server says radio is ready — connect via TLS
    connect(&m_smartLink, &SmartLinkClient::connectReady,
            this, [this](const QString& handle, const QString& serial) {
        if (serial != m_pendingWanRadio.serial) return;
        m_connPanel->setStatusText("TLS connecting to radio…");
        m_wanConnection.connectToRadio(
            m_pendingWanRadio.publicIp,
            static_cast<quint16>(m_pendingWanRadio.publicTlsPort),
            handle);
    });

    // WAN connection established — wire to RadioModel
    // TODO: RadioModel needs to accept WanConnection as an alternative
    // to RadioConnection. For now, log the event.
    connect(&m_wanConnection, &WanConnection::connected, this, [this] {
        qDebug() << "MainWindow: WAN connection established!";
        m_connPanel->setStatusText("Connected via SmartLink");
        m_connPanel->setConnected(true);

        // Wire WanConnection to RadioModel for full operation
        m_radioModel.connectViaWan(&m_wanConnection,
            m_pendingWanRadio.publicIp,
            static_cast<quint16>(m_pendingWanRadio.publicUdpPort > 0
                ? m_pendingWanRadio.publicUdpPort : 4993));
    });
    connect(&m_wanConnection, &WanConnection::disconnected, this, [this] {
        qDebug() << "MainWindow: WAN connection lost";
        m_connPanel->setStatusText("SmartLink disconnected");
        m_connPanel->setConnected(false);
    });
    connect(&m_wanConnection, &WanConnection::errorOccurred, this, [this](const QString& err) {
        m_connPanel->setStatusText("SmartLink error: " + err);
    });

    // ── Wire up radio model ────────────────────────────────────────────────
    connect(&m_radioModel, &RadioModel::connectionStateChanged,
            this, &MainWindow::onConnectionStateChanged);
    connect(&m_radioModel, &RadioModel::connectionError,
            this, &MainWindow::onConnectionError);
    connect(&m_radioModel, &RadioModel::sliceAdded,
            this, &MainWindow::onSliceAdded);
    connect(&m_radioModel, &RadioModel::sliceRemoved,
            this, &MainWindow::onSliceRemoved);

    // ── TX audio stream: set stream ID for DAX TX path ──────────────────
    // DAX TX audio is sent via PanadapterStream::sendToRadio() (the
    // registered VITA-49 socket).  We do NOT start a separate mic TX
    // stream — that would open a QAudioSource and an unregistered UDP
    // socket, wasting resources and corrupting the shared packet counter.
    connect(&m_radioModel, &RadioModel::txAudioStreamReady,
            this, [this](quint32 streamId) {
        m_audio.setTxStreamId(streamId);
        qDebug() << "MainWindow: DAX TX stream ID set to" << Qt::hex << streamId;
        // Start PC audio TX if mic_selection is PC
        if (m_radioModel.transmitModel()->micSelection() == "PC") {
            m_audio.startTxStream(
                m_radioModel.connection()->radioAddress(),
                4991);
        }
    });
    // Start/stop PC audio TX when mic_selection changes
    connect(m_radioModel.transmitModel(), &TransmitModel::micStateChanged,
            this, [this]() {
        if (m_radioModel.transmitModel()->micSelection() == "PC") {
            if (!m_audio.isTxStreaming()) {
                m_audio.startTxStream(
                    m_radioModel.connection()->radioAddress(),
                    4991);
            }
        } else {
            m_audio.stopTxStream();
        }
    });

    // TX/RX transition → waterfall tile source switching
    connect(m_radioModel.transmitModel(), &TransmitModel::moxChanged,
            this, [this](bool tx) {
        spectrum()->setTransmitting(tx);
        // On RX resume, native tiles will restart and m_hasNativeWaterfall
        // will be set again by the first arriving tile.
    });

    // ── Panadapter stream → spectrum widget ───────────────────────────────
    connect(m_radioModel.panStream(), &PanadapterStream::spectrumReady,
            spectrum(), &SpectrumWidget::updateSpectrum);
    connect(m_radioModel.panStream(), &PanadapterStream::waterfallRowReady,
            spectrum(), &SpectrumWidget::updateWaterfallRow);
    connect(m_radioModel.panStream(), &PanadapterStream::waterfallAutoBlackLevel,
            this, [this](quint32 autoBlack) {
        if (spectrum()->wfAutoBlack()) {
            // Auto black level from radio tile header — apply as black level
            // The value is in raw intensity units; map to our 0-125 slider range
            const int level = std::clamp(static_cast<int>(autoBlack), 0, 125);
            spectrum()->setWfBlackLevel(level);
        }
    });
    connect(&m_radioModel, &RadioModel::panadapterInfoChanged,
            spectrum(), &SpectrumWidget::setFrequencyRange);
    connect(&m_radioModel, &RadioModel::panadapterInfoChanged,
            this, [this]() {
        if (!m_displaySettingsPushed) {
            m_displaySettingsPushed = true;
            m_radioModel.setPanAverage(spectrum()->fftAverage());
            m_radioModel.setPanFps(spectrum()->fftFps());
            m_radioModel.setPanWeightedAverage(spectrum()->fftWeightedAvg());
            m_radioModel.setWaterfallColorGain(spectrum()->wfColorGain());
            m_radioModel.setWaterfallBlackLevel(spectrum()->wfBlackLevel());
            m_radioModel.setWaterfallAutoBlack(spectrum()->wfAutoBlack());
            int rate = spectrum()->wfLineDuration();
            m_radioModel.setWaterfallLineDuration(rate);
            // Restore saved WNB and RF gain
            auto& s = AppSettings::instance();
            bool wnbOn = s.value("DisplayWnbEnabled", "False").toString() == "True";
            int wnbLevel = s.value("DisplayWnbLevel", "50").toInt();
            int rfGain = s.value("DisplayRfGain", "0").toInt();
            m_radioModel.setPanWnb(wnbOn);
            m_radioModel.setPanWnbLevel(wnbLevel);
            m_radioModel.setPanRfGain(rfGain);
            spectrum()->setWnbActive(wnbOn);
            spectrum()->setRfGain(rfGain);
            spectrum()->overlayMenu()->setWnbState(wnbOn, wnbLevel);
            spectrum()->overlayMenu()->setRfGain(rfGain);
            // Nudge rate to force waterfall tile re-sync
            QTimer::singleShot(500, this, [this, rate]() {
                m_radioModel.setWaterfallLineDuration(rate + 1);
                m_radioModel.setWaterfallLineDuration(rate);
            });
        }
    });
    connect(&m_radioModel, &RadioModel::panadapterLevelChanged,
            spectrum(), &SpectrumWidget::setDbmRange);
    connect(spectrum(), &SpectrumWidget::bandwidthChangeRequested,
            &m_radioModel, &RadioModel::setPanBandwidth);
    connect(spectrum(), &SpectrumWidget::centerChangeRequested,
            &m_radioModel, &RadioModel::setPanCenter);
    connect(spectrum(), &SpectrumWidget::filterChangeRequested,
            this, [this](int lo, int hi) {
        if (auto* s = activeSlice()) s->setFilterWidth(lo, hi);
    });
    connect(spectrum(), &SpectrumWidget::dbmRangeChangeRequested,
            &m_radioModel, &RadioModel::setPanDbmRange);

    // ── TNF model ↔ spectrum widget ──────────────────────────────────────
    auto* tnf = m_radioModel.tnfModel();
    auto rebuildTnfMarkers = [this]() {
        auto* t = m_radioModel.tnfModel();
        QVector<SpectrumWidget::TnfMarker> markers;
        for (const auto& e : t->tnfs())
            markers.append({e.id, e.freqMhz, e.widthHz, e.depthDb, e.permanent});
        spectrum()->setTnfMarkers(markers);
    };
    connect(tnf, &TnfModel::tnfChanged,          this, rebuildTnfMarkers);
    connect(tnf, &TnfModel::tnfRemoved,           this, rebuildTnfMarkers);
    connect(tnf, &TnfModel::globalEnabledChanged,
            spectrum(), &SpectrumWidget::setTnfGlobalEnabled);
    connect(spectrum(), &SpectrumWidget::tnfCreateRequested,
            tnf, &TnfModel::createTnf);
    connect(spectrum(), &SpectrumWidget::tnfMoveRequested,
            tnf, &TnfModel::setTnfFreq);
    connect(spectrum(), &SpectrumWidget::tnfRemoveRequested,
            tnf, &TnfModel::requestRemoveTnf);
    connect(spectrum(), &SpectrumWidget::tnfWidthRequested,
            tnf, &TnfModel::setTnfWidth);
    connect(spectrum(), &SpectrumWidget::tnfDepthRequested,
            tnf, &TnfModel::setTnfDepth);
    connect(spectrum(), &SpectrumWidget::tnfPermanentRequested,
            tnf, &TnfModel::setTnfPermanent);

    // ── Click-to-tune on the spectrum ─────────────────────────────────────
    connect(spectrum(), &SpectrumWidget::frequencyClicked,
            this, &MainWindow::onFrequencyChanged);

    // ── +RX button: add a new slice on the current panadapter ──────────────
    connect(spectrum()->overlayMenu(), &SpectrumOverlayMenu::addRxClicked,
            this, [this]() { m_radioModel.addSlice(); });
    connect(spectrum()->overlayMenu(), &SpectrumOverlayMenu::addTnfClicked,
            this, [this]() {
        auto* s = activeSlice();
        if (!s) return;
        // Place TNF at the center of the filter passband
        double tnfFreq = s->frequency()
            + (s->filterLow() + s->filterHigh()) / 2.0 / 1.0e6;
        m_radioModel.tnfModel()->createTnf(tnfFreq);
    });

    // ── Slice marker click → switch active slice ────────────────────────────
    connect(spectrum(), &SpectrumWidget::sliceClicked,
            this, &MainWindow::setActiveSlice);
    connect(spectrum(), &SpectrumWidget::sliceTxRequested,
            this, [this](int sliceId) {
        if (auto* s = m_radioModel.slice(sliceId))
            s->setTxSlice(true);
    });
    connect(spectrum(), &SpectrumWidget::sliceCloseRequested,
            this, [this](int sliceId) {
        if (m_radioModel.slices().size() <= 1) return;
        m_radioModel.sendCommand(QString("slice remove %1").arg(sliceId));
    });

    // VFO widget close/lock buttons
    connect(spectrum()->vfoWidget(), &VfoWidget::closeSliceRequested,
            this, [this] {
        if (m_radioModel.slices().size() <= 1) return;
        if (m_activeSliceId >= 0)
            m_radioModel.sendCommand(QString("slice remove %1").arg(m_activeSliceId));
    });
    connect(spectrum()->vfoWidget(), &VfoWidget::lockToggled,
            this, [this](bool locked) {
        if (auto* s = activeSlice())
            s->setLocked(locked);
    });

    // ── Band selection from overlay menu ───────────────────────────────────
    connect(spectrum()->overlayMenu(), &SpectrumOverlayMenu::bandSelected,
            this, [this](const QString& bandName, double freqMhz, const QString& mode) {
        // Band memory save/restore is deprecated pending redesign.
        // For now, always use band defaults (freq + mode from BandDefs).
        qDebug() << "MainWindow: switching to band" << bandName
                 << "freq:" << freqMhz << "mode:" << mode;
        m_bandSettings.setCurrentBand(bandName);
        if (auto* s = activeSlice())
            s->setMode(mode);
        onFrequencyChanged(freqMhz);
    });

    // ── WNB toggle from overlay menu → panadapter + indicator ──────────────
    connect(spectrum()->overlayMenu(), &SpectrumOverlayMenu::wnbToggled,
            this, [this](bool on) {
        m_radioModel.setPanWnb(on);
        spectrum()->setWnbActive(on);
        auto& s = AppSettings::instance();
        s.setValue("DisplayWnbEnabled", on ? "True" : "False");
        s.save();
    });
    connect(spectrum()->overlayMenu(), &SpectrumOverlayMenu::wnbLevelChanged,
            this, [this](int level) {
        m_radioModel.setPanWnbLevel(level);
        auto& s = AppSettings::instance();
        s.setValue("DisplayWnbLevel", QString::number(level));
        s.save();
    });
    connect(spectrum()->overlayMenu(), &SpectrumOverlayMenu::rfGainChanged,
            this, [this](int gain) {
        m_radioModel.setPanRfGain(gain);
        spectrum()->setRfGain(gain);
        auto& s = AppSettings::instance();
        s.setValue("DisplayRfGain", QString::number(gain));
        s.save();
    });

    // ── Display sub-panel → SpectrumWidget (client-side for now) ─────────
    auto* overlay = spectrum()->overlayMenu();
    connect(overlay, &SpectrumOverlayMenu::fftFillAlphaChanged,
            spectrum(), &SpectrumWidget::setFftFillAlpha);
    connect(overlay, &SpectrumOverlayMenu::fftFillColorChanged,
            spectrum(), &SpectrumWidget::setFftFillColor);
    // FFT controls → SpectrumWidget (local) + RadioModel (radio command)
    connect(overlay, &SpectrumOverlayMenu::fftAverageChanged,
            this, [this](int v) {
        spectrum()->setFftAverage(v);
        m_radioModel.setPanAverage(v);
    });
    connect(overlay, &SpectrumOverlayMenu::fftFpsChanged,
            this, [this](int v) {
        spectrum()->setFftFps(v);
        m_radioModel.setPanFps(v);
    });
    connect(overlay, &SpectrumOverlayMenu::fftWeightedAverageChanged,
            this, [this](bool on) {
        spectrum()->setFftWeightedAvg(on);
        m_radioModel.setPanWeightedAverage(on);
    });
    // Waterfall controls → SpectrumWidget (local) + RadioModel (radio command)
    connect(overlay, &SpectrumOverlayMenu::wfColorGainChanged,
            this, [this](int v) {
        spectrum()->setWfColorGain(v);
        m_radioModel.setWaterfallColorGain(v);
    });
    connect(overlay, &SpectrumOverlayMenu::wfBlackLevelChanged,
            this, [this](int v) {
        spectrum()->setWfBlackLevel(v);
        m_radioModel.setWaterfallBlackLevel(v);
    });
    connect(overlay, &SpectrumOverlayMenu::wfAutoBlackChanged,
            this, [this](bool on) {
        spectrum()->setWfAutoBlack(on);
        m_radioModel.setWaterfallAutoBlack(on);
    });
    connect(overlay, &SpectrumOverlayMenu::wfLineDurationChanged,
            this, [this](int ms) {
        spectrum()->setWfLineDuration(ms);
        m_radioModel.setWaterfallLineDuration(ms);
    });
    // Noise floor auto-adjust (client-side, adjusts min_dbm)
    connect(overlay, &SpectrumOverlayMenu::noiseFloorPositionChanged,
            spectrum(), &SpectrumWidget::setNoiseFloorPosition);
    connect(overlay, &SpectrumOverlayMenu::noiseFloorEnableChanged,
            spectrum(), &SpectrumWidget::setNoiseFloorEnable);

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
    connect(spectrum()->vfoWidget(), &VfoWidget::afGainChanged, this, [this](int v) {
        m_audio.setRxVolume(v / 100.0f);
    });

    // ── Client-side NR2 toggle: VfoWidget → AudioEngine ─────────────────
    // On first enable, generate FFTW wisdom if needed (takes several minutes).
    connect(spectrum()->vfoWidget(), &VfoWidget::nr2Toggled,
            this, [this](bool on) {
        if (!on) {
            m_audio.setNr2Enabled(false);
            return;
        }
        // Check if wisdom exists; if not, generate with progress dialog
        if (AudioEngine::needsWisdomGeneration()) {
            auto* dlg = new QProgressDialog(
                "Optimizing FFT plans for NR2...\n\n"
                "This window will automatically close when wisdom generation is complete.",
                QString(), 0, 100, this);
            dlg->setWindowTitle("AetherSDR — FFTW Wisdom");
            dlg->setWindowModality(Qt::ApplicationModal);
            dlg->setMinimumDuration(0);
            dlg->setAutoClose(false);
            dlg->setAutoReset(false);
            dlg->setCancelButton(nullptr);
            dlg->setMinimumWidth(500);
            dlg->setStyleSheet(
                "QProgressBar { text-align: center; font-size: 13px;"
                " font-weight: bold; color: #c8d8e8;"
                " background: #1a2a3a; border: 1px solid #2e4e6e; border-radius: 3px; }"
                "QProgressBar::chunk { background: #00b4d8; }");
            dlg->show();

            // Breathing animation (started later during save phase)
            auto* breathe = new QPropertyAnimation(dlg, "windowOpacity", dlg);
            breathe->setDuration(1500);
            breathe->setStartValue(1.0);
            breathe->setKeyValueAt(0.5, 0.55);
            breathe->setEndValue(1.0);
            breathe->setLoopCount(-1);

            auto* thread = QThread::create([this, dlg, breathe]() {
                AudioEngine::generateWisdom([dlg, breathe](int step, int total, const std::string& desc) {
                    int pct = total > 0 ? (step * 100 / total) : 0;
                    QString d = QString::fromStdString(desc);
                    QMetaObject::invokeMethod(dlg, [dlg, breathe, pct, d]() {
                        if (!d.isEmpty()) {
                            // "Before" callback: update description, keep current %
                            dlg->setLabelText(d + "\n\n"
                                "This window will automatically close when wisdom generation is complete.");
                            // Start breathing on the last few slow plans
                            if (dlg->value() >= 90 && breathe->state() != QAbstractAnimation::Running)
                                breathe->start();
                        } else {
                            // "After" callback: update progress bar
                            dlg->setValue(pct);
                        }
                    });
                });
            });
            connect(thread, &QThread::finished, this, [this, dlg, breathe, thread]() {
                // All plans computed + wisdom saved — show brief "done" then close
                breathe->stop();
                dlg->setWindowOpacity(1.0);
                dlg->setValue(100);
                dlg->setLabelText("Wisdom generation complete!");
                QTimer::singleShot(800, this, [this, dlg, thread]() {
                    dlg->close();
                    dlg->deleteLater();
                    thread->deleteLater();
                    m_audio.setNr2Enabled(true);
                });
            });
            thread->start();
        } else {
            m_audio.setNr2Enabled(true);
        }
    });
    connect(&m_audio, &AudioEngine::nr2EnabledChanged,
            this, [this](bool on) {
        QSignalBlocker sb(spectrum()->vfoWidget()->nr2Button());
        spectrum()->vfoWidget()->nr2Button()->setChecked(on);
    });
    // Overlay DSP panel NR2 → forward to VfoWidget NR2 button (same signal chain)
    connect(spectrum()->overlayMenu(), &SpectrumOverlayMenu::nr2Toggled,
            this, [this](bool on) {
        spectrum()->vfoWidget()->nr2Button()->setChecked(on);  // triggers nr2Toggled
    });
    // Sync overlay NR2 button when state changes
    connect(&m_audio, &AudioEngine::nr2EnabledChanged,
            this, [this](bool on) {
        if (auto* btn = spectrum()->overlayMenu()->dspNr2Button())
            { QSignalBlocker sb(btn); btn->setChecked(on); }
    });

    // RxApplet NR button 3-state cycle → NR2 enable/disable
    connect(m_appletPanel->rxApplet(), &RxApplet::nr2CycleToggled,
            this, [this](bool on) {
        // Forward to VfoWidget NR2 button which triggers wisdom + enable
        spectrum()->vfoWidget()->nr2Button()->setChecked(on);
    });
    // Sync RxApplet NR button visual when NR2 state changes
    connect(&m_audio, &AudioEngine::nr2EnabledChanged,
            this, [this](bool on) {
        auto* rx = m_appletPanel->rxApplet();
        if (on) {
            // NR2 active — disable radio NR if on, show "NR2" green
            if (auto* s = activeSlice(); s && s->nrOn())
                s->setNr(false);
            rx->setNrState(2);
        } else if (rx->nrState() == 2) {
            // NR2 turned off — button goes to off state
            rx->setNrState(0);
        }
    });

    // ── Tuning step size → spectrum widget ─────────────────────────────────
    connect(m_appletPanel->rxApplet(), &RxApplet::stepSizeChanged,
            spectrum(), &SpectrumWidget::setStepSize);
    spectrum()->setStepSize(100);

    // ── Antenna list from radio → applet panel ─────────────────────────────
    connect(&m_radioModel, &RadioModel::antListChanged,
            m_appletPanel, &AppletPanel::setAntennaList);
    connect(&m_radioModel, &RadioModel::antListChanged,
            spectrum()->overlayMenu(), &SpectrumOverlayMenu::setAntennaList);
    connect(&m_radioModel, &RadioModel::antListChanged,
            spectrum()->vfoWidget(), &VfoWidget::setAntennaList);

    // ── S-Meter: MeterModel → SMeterWidget ────────────────────────────────
    connect(m_radioModel.meterModel(), &MeterModel::sLevelChanged,
            m_appletPanel->sMeterWidget(), &SMeterWidget::setLevel);
    connect(m_radioModel.meterModel(), &MeterModel::sLevelChanged,
            spectrum()->vfoWidget(), &VfoWidget::setSignalLevel);
    connect(m_radioModel.meterModel(), &MeterModel::txMetersChanged,
            m_appletPanel->sMeterWidget(), &SMeterWidget::setTxMeters);
    connect(m_radioModel.meterModel(), &MeterModel::micMetersChanged,
            m_appletPanel->sMeterWidget(), &SMeterWidget::setMicMeters);
    connect(m_radioModel.transmitModel(), &TransmitModel::moxChanged,
            m_appletPanel->sMeterWidget(), &SMeterWidget::setTransmitting);

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
    m_appletPanel->rxApplet()->setTransmitModel(m_radioModel.transmitModel());

    // ── P/CW applet: mic meters + ALC meter + model ────────────────────────
    connect(m_radioModel.meterModel(), &MeterModel::micMetersChanged,
            m_appletPanel->phoneCwApplet(), &PhoneCwApplet::updateMeters);
    connect(m_radioModel.meterModel(), &MeterModel::alcChanged,
            m_appletPanel->phoneCwApplet(), &PhoneCwApplet::updateAlc);
    m_appletPanel->phoneCwApplet()->setTransmitModel(m_radioModel.transmitModel());

    // ── PHNE applet: VOX + CW controls ──────────────────────────────────────
    m_appletPanel->phoneApplet()->setTransmitModel(m_radioModel.transmitModel());

    // ── EQ applet: graphic equalizer ─────────────────────────────────────────
    m_appletPanel->eqApplet()->setEqualizerModel(m_radioModel.equalizerModel());

    // ── Antenna Genius applet: external 4O3A antenna switch ──────────────────
    m_appletPanel->agApplet()->setModel(&m_antennaGenius);
    connect(&m_antennaGenius, &AntennaGeniusModel::presenceChanged,
            m_appletPanel, &AppletPanel::setAgVisible);

    // ── 4-channel CAT: rigctld + PTY (A-D, each bound to a slice) ────────────
    {
        static const char kLetters[] = "ABCD";
        for (int i = 0; i < kCatChannels; ++i) {
            m_rigctlServers[i] = new RigctlServer(&m_radioModel, this);
            m_rigctlServers[i]->setSliceIndex(i);
            m_rigctlPtys[i] = new RigctlPty(&m_radioModel, this);
            m_rigctlPtys[i]->setSliceIndex(i);
            m_rigctlPtys[i]->setSymlinkPath(
                QString("/tmp/AetherSDR-CAT-%1").arg(kLetters[i]));
        }
    }
    m_appletPanel->catApplet()->setRadioModel(&m_radioModel);
    m_appletPanel->catApplet()->setRigctlServers(m_rigctlServers, kCatChannels);
    m_appletPanel->catApplet()->setRigctlPtys(m_rigctlPtys, kCatChannels);
    m_appletPanel->catApplet()->setAudioEngine(&m_audio);

    // ── Status bar telemetry ──────────────────────────────────────────────────
    connect(&m_radioModel, &RadioModel::networkQualityChanged,
            this, [this](const QString& quality, int pingMs) {
        // Color code: Excellent/VeryGood=green, Good=cyan, Fair=amber, Poor=red
        QString color = "#00cc66";
        if (quality == "Fair") color = "#cc9900";
        else if (quality == "Poor") color = "#cc3333";
        else if (quality == "Good") color = "#00b4d8";
        Q_UNUSED(pingMs);
        m_networkLabel->setText(QString("Network: [<span style='color:%1'>%2</span>]")
            .arg(color, quality));
        m_networkLabel->setTextFormat(Qt::RichText);
    });

    connect(m_radioModel.meterModel(), &MeterModel::hwTelemetryChanged,
            this, [this](float paTemp, float supplyVolts) {
        m_paTempLabel->setText(QString("PA %1\u00B0C  |  %2 V")
            .arg(paTemp, 0, 'f', 0)
            .arg(supplyVolts, 0, 'f', 1));
    });

    connect(&m_radioModel, &RadioModel::gpsStatusChanged,
            this, [this](const QString& status, int tracked, int visible,
                         const QString& grid, const QString& /*alt*/,
                         const QString& /*lat*/, const QString& /*lon*/,
                         const QString& utcTime) {
        const bool gpsPresent = (status != "Not Present" && status != "");
        m_gpsLabel->setText(gpsPresent
            ? QString("GPS %1/%2 [%3]").arg(tracked).arg(visible).arg(status)
            : "GPS: N/A");

        if (!grid.isEmpty())
            m_gridLabel->setText(grid);

        // Use GPS UTC time if available, otherwise system UTC
        if (gpsPresent && !utcTime.isEmpty()) {
            m_gpsTimeLabel->setText(utcTime);
            m_useSystemClock = false;
        } else {
            m_useSystemClock = true;
        }
    });

    // System clock fallback when no GPS is installed
    auto* clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, [this] {
        if (m_useSystemClock)
            m_gpsTimeLabel->setText(QDateTime::currentDateTimeUtc().toString("HH:mm:ssZ"));
    });
    clockTimer->start(1000);

    // Start discovery
    m_discovery.startListening();

    // Auto-connect to routed radios (probed, not broadcast-discovered)
    connect(m_connPanel, &ConnectionPanel::routedRadioFound,
            this, [this](const RadioInfo& info) {
        if (m_userDisconnected || m_radioModel.isConnected()) return;
        const QString lastSerial = AppSettings::instance()
            .value("LastConnectedRadioSerial").toString();
        if (!lastSerial.isEmpty() && info.serial == lastSerial) {
            qDebug() << "Auto-connecting to routed radio" << info.address.toString();
            m_connPanel->setStatusText("Auto-connecting…");
            m_radioModel.connectToRadio(info);
        }
    });

    // Probe saved routed radio on startup
    {
        auto& s = AppSettings::instance();
        const QString routedIp = s.value("LastRoutedRadioIp").toString();
        if (!routedIp.isEmpty() && !m_userDisconnected) {
            QTimer::singleShot(500, this, [this, routedIp] {
                m_connPanel->probeRadio(routedIp);
            });
        }
    }

    // Restore saved geometry from XML settings
    auto& s = AppSettings::instance();
    const QString geomB64 = s.value("MainWindowGeometry").toString();
    if (!geomB64.isEmpty())
        restoreGeometry(QByteArray::fromBase64(geomB64.toLatin1()));
    const QString stateB64 = s.value("MainWindowState").toString();
    if (!stateB64.isEmpty())
        restoreState(QByteArray::fromBase64(stateB64.toLatin1()));
    const QString splitB64 = s.value("SplitterState").toString();
    if (!splitB64.isEmpty())
        m_splitter->restoreState(QByteArray::fromBase64(splitB64.toLatin1()));

    // Restore connection panel state
    if (s.value("ConnPanelCollapsed", "False").toString() == "True")
        m_connPanel->setCollapsed(true);
}

MainWindow::~MainWindow() = default;

void MainWindow::closeEvent(QCloseEvent* event)
{
    auto& s = AppSettings::instance();
    s.setValue("MainWindowGeometry", saveGeometry().toBase64());
    s.setValue("MainWindowState",   saveState().toBase64());
    s.setValue("SplitterState",     m_splitter->saveState().toBase64());
    s.setValue("ConnPanelCollapsed", m_connPanel->isCollapsed() ? "True" : "False");
    s.save();
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

    // ── Settings menu ──────────────────────────────────────────────────────
    auto* settingsMenu = menuBar()->addMenu("&Settings");

    auto* radioSetup = settingsMenu->addAction("Radio Setup...");
    connect(radioSetup, &QAction::triggered, this, [this] {
        auto* dlg = new RadioSetupDialog(&m_radioModel, &m_audio, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->show();
    });

    auto* chooseRadio = settingsMenu->addAction("Choose Radio / SmartLink Setup...");
    connect(chooseRadio, &QAction::triggered, this, [this] {
        m_connPanel->setCollapsed(false);
    });

    settingsMenu->addAction("FlexControl...");
    auto* networkAction = settingsMenu->addAction("Network...");
    connect(networkAction, &QAction::triggered, this, [this] {
        NetworkDiagnosticsDialog dlg(&m_radioModel, this);
        dlg.exec();
    });
    auto* memoryAction = settingsMenu->addAction("Memory...");
    connect(memoryAction, &QAction::triggered, this, [this] {
        MemoryDialog dlg(&m_radioModel, this);
        dlg.exec();
    });
    settingsMenu->addAction("USB Cables...");
    auto* spotsAction = settingsMenu->addAction("Spots...");
    connect(spotsAction, &QAction::triggered, this, [this] {
        SpotSettingsDialog dlg(&m_radioModel, this);
        dlg.exec();
    });
    settingsMenu->addAction("multiFLEX...");
    settingsMenu->addAction("TX Band Settings...");

    settingsMenu->addSeparator();

    auto* autoRigctlAction = settingsMenu->addAction("Autostart rigctld with AetherSDR");
    autoRigctlAction->setCheckable(true);
    autoRigctlAction->setChecked(
        AppSettings::instance().value("AutoStartRigctld", "False").toString() == "True");
    connect(autoRigctlAction, &QAction::toggled, this, [](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("AutoStartRigctld", on ? "True" : "False");
        s.save();
    });

    auto* autoCatAction = settingsMenu->addAction("Autostart CAT with AetherSDR");
    autoCatAction->setCheckable(true);
    autoCatAction->setChecked(
        AppSettings::instance().value("AutoStartCAT", "False").toString() == "True");
    connect(autoCatAction, &QAction::toggled, this, [](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("AutoStartCAT", on ? "True" : "False");
        s.save();
    });

    auto* autoDaxAction = settingsMenu->addAction("Autostart DAX with AetherSDR");
    autoDaxAction->setCheckable(true);
    autoDaxAction->setChecked(
        AppSettings::instance().value("AutoStartDAX", "False").toString() == "True");
    connect(autoDaxAction, &QAction::toggled, this, [](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("AutoStartDAX", on ? "True" : "False");
        s.save();
    });

    // Connect placeholder items to show "not implemented" message
    for (auto* action : settingsMenu->actions()) {
        if (!action->isSeparator() && action != radioSetup && action != chooseRadio
            && action != networkAction && action != memoryAction && action != spotsAction
            && action != autoRigctlAction && action != autoCatAction && action != autoDaxAction) {
            connect(action, &QAction::triggered, this, [this, action] {
                statusBar()->showMessage(action->text().remove("...") + " — not yet implemented", 3000);
            });
        }
    }

    // ── Profiles menu ──────────────────────────────────────────────────────
    m_profilesMenu = menuBar()->addMenu("&Profiles");
    auto* profileMgrAct = m_profilesMenu->addAction("Profile Manager...");
    connect(profileMgrAct, &QAction::triggered, this, [this] {
        ProfileManagerDialog dlg(&m_radioModel, this);
        dlg.exec();
    });
    auto* profileImportExportAct = m_profilesMenu->addAction("Import/Export Profiles...");
    connect(profileImportExportAct, &QAction::triggered, this, [this] {
        // TODO: open import/export dialog
    });
    m_profilesMenu->addSeparator();

    // Global profile list (populated on connect)
    connect(&m_radioModel, &RadioModel::globalProfilesChanged, this, [this] {
        // Remove old profile actions (after the separator)
        const auto actions = m_profilesMenu->actions();
        for (int i = 3; i < actions.size(); ++i)  // skip Manager, Import/Export, separator
            m_profilesMenu->removeAction(actions[i]);

        // Add current global profiles
        const auto profiles = m_radioModel.globalProfiles();
        const auto active = m_radioModel.activeGlobalProfile();
        for (const auto& name : profiles) {
            auto* act = m_profilesMenu->addAction(name);
            act->setCheckable(true);
            act->setChecked(name == active);
            connect(act, &QAction::triggered, this, [this, name] {
                m_radioModel.loadGlobalProfile(name);
            });
        }
    });

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* themeAct = viewMenu->addAction("Toggle Dark/Light Theme");
    connect(themeAct, &QAction::triggered, this, [this]{
        // Placeholder — full theme switching left as an exercise
        applyDarkTheme();
    });

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("About AetherSDR", this, [this]{
        const QString text = QString(
            "<div style='text-align:center;'>"
            "<h2 style='margin-bottom:2px;'>AetherSDR</h2>"
            "<p style='margin-top:0;'>v%1</p>"
            "<p>Linux-native SmartSDR-compatible client<br>"
            "for FlexRadio transceivers.</p>"
            "<p style='font-size:11px; color:#8aa8c0;'>"
            "Built with Qt %2 &middot; C++20<br>"
            "Compiled: %3</p>"
            "<hr>"
            "<p style='font-size:11px;'>"
            "<b>Contributors</b><br>"
            "Jeremy (KK7GWY)<br>"
            "Claude &middot; Anthropic<br>"
            "Dependabot</p>"
            "<hr>"
            "<p style='font-size:11px; color:#8aa8c0;'>"
            "&copy; 2026 AetherSDR Contributors<br>"
            "Licensed under "
            "<a href='https://www.gnu.org/licenses/gpl-3.0.html' style='color:#00b4d8;'>GPLv3</a></p>"
            "<p style='font-size:11px;'>"
            "<a href='https://github.com/ten9876/AetherSDR' style='color:#00b4d8;'>"
            "github.com/ten9876/AetherSDR</a></p>"
            "<hr>"
            "<p style='font-size:10px; color:#6a8090;'>"
            "SmartSDR protocol &copy; FlexRadio Systems</p>"
            "</div>"
            ).arg(QCoreApplication::applicationVersion(),
                  qVersion(),
                  QStringLiteral(__DATE__));
        QMessageBox about(this);
        about.setWindowTitle("About AetherSDR");
        about.setIconPixmap(QPixmap(":/icon.png").scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        about.setText(text);
        about.exec();
    });
}

void MainWindow::buildUI()
{
    // ── Central splitter: [sidebar | spectrum | applets] ──────────────────
    m_splitter = new QSplitter(Qt::Horizontal, this);
    setCentralWidget(m_splitter);
    auto* splitter = m_splitter;

    // Left sidebar — connection panel
    m_connPanel = new ConnectionPanel(splitter);
    m_connPanel->setFixedWidth(260);
    splitter->addWidget(m_connPanel);

    // Centre — panadapter applet (title bar + FFT spectrum + waterfall)
    m_panApplet = new PanadapterApplet(splitter);
    splitter->addWidget(m_panApplet);
    splitter->setStretchFactor(1, 1);

    // Right — applet panel (includes S-Meter)
    m_appletPanel = new AppletPanel(splitter);
    splitter->addWidget(m_appletPanel);
    splitter->setStretchFactor(2, 0);
    splitter->setCollapsible(2, false);

    // Set initial splitter sizes: left=260, center=stretch, right=310
    // The center pane gets whatever is left after the fixed-width sidebars.
    const int centerWidth = qMax(400, width() - 260 - 310);
    splitter->setSizes({260, centerWidth, 310});

    // ── Status bar ─────────────────────────────────────────────────────────
    const QString statusStyle = "QLabel { color: #8aa8c0; font-size: 11px; background: transparent; }";

    m_connStatusLabel = new QLabel("Disconnected", this);
    m_connStatusLabel->setStyleSheet(statusStyle);
    statusBar()->addWidget(m_connStatusLabel);

    m_networkLabel = new QLabel("", this);
    m_networkLabel->setStyleSheet(statusStyle);
    statusBar()->addWidget(m_networkLabel);

    m_radioInfoLabel = new QLabel("", this);
    m_radioInfoLabel->setStyleSheet(statusStyle);
    statusBar()->addWidget(m_radioInfoLabel);

    m_gpsTimeLabel = new QLabel("", this);
    m_gpsTimeLabel->setStyleSheet(statusStyle);
    m_gpsTimeLabel->setAlignment(Qt::AlignCenter);
    statusBar()->addWidget(m_gpsTimeLabel, 1);

    m_paTempLabel = new QLabel("", this);
    m_paTempLabel->setStyleSheet(statusStyle);
    statusBar()->addPermanentWidget(m_paTempLabel);

    m_gpsLabel = new QLabel("", this);
    m_gpsLabel->setStyleSheet(statusStyle);
    statusBar()->addPermanentWidget(m_gpsLabel);

    m_gridLabel = new QLabel("", this);
    m_gridLabel->setStyleSheet(statusStyle);
    statusBar()->addPermanentWidget(m_gridLabel);
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
        // TX audio stream will start when the radio assigns a stream ID
        // Auto-collapse the connection panel unless the user manually expanded it
        if (!m_userExpandedPanel)
            m_connPanel->setCollapsed(true);

        // Auto-start 4-channel rigctld TCP servers if enabled
        auto& as = AppSettings::instance();
        if (as.value("AutoStartRigctld", "False").toString() == "True") {
            const int basePort = as.value("CatTcpPort", "4532").toInt();
            for (int i = 0; i < kCatChannels; ++i) {
                if (m_rigctlServers[i] && !m_rigctlServers[i]->isRunning()) {
                    m_rigctlServers[i]->start(
                        static_cast<quint16>(basePort + i));
                    qDebug() << "AutoStart: rigctld ch" << i
                             << "on port" << (basePort + i);
                }
            }
            if (m_appletPanel && m_appletPanel->catApplet())
                m_appletPanel->catApplet()->setTcpEnabled(true);
        }
        // Auto-start 4-channel CAT virtual serial ports if enabled
        if (as.value("AutoStartCAT", "False").toString() == "True") {
            for (int i = 0; i < kCatChannels; ++i) {
                if (m_rigctlPtys[i] && !m_rigctlPtys[i]->isRunning()) {
                    m_rigctlPtys[i]->start();
                    qDebug() << "AutoStart: PTY ch" << i
                             << m_rigctlPtys[i]->symlinkPath();
                }
            }
            if (m_appletPanel && m_appletPanel->catApplet())
                m_appletPanel->catApplet()->setPtyEnabled(true);
        }
        // Populate XVTR bands after radio status settles
        QTimer::singleShot(2000, this, [this] {
            if (!m_radioModel.isConnected()) return;
            QVector<SpectrumOverlayMenu::XvtrBand> xvtrBands;
            for (const auto& x : m_radioModel.xvtrList()) {
                if (x.isValid)
                    xvtrBands.append({x.name, x.rfFreq});
            }
            spectrum()->overlayMenu()->setXvtrBands(xvtrBands);
        });

        // Apply saved display settings after panadapter is created
        m_displaySettingsPushed = false;
    } else {
        m_connStatusLabel->setText("Disconnected");
        m_radioInfoLabel->setText("");
        m_connPanel->setStatusText("Not connected");
        m_audio.stopRxStream();
        m_audio.stopTxStream();
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

    // First slice — wire everything up
    if (m_activeSliceId < 0) {
        setActiveSlice(s->sliceId());

        // Detect initial band from radio's frequency
        if (m_bandSettings.currentBand().isEmpty())
            m_bandSettings.setCurrentBand(BandSettings::bandForFrequency(s->frequency()));

        // Re-create audio stream if it was invalidated by a profile load
        if (m_needAudioStream) {
            m_needAudioStream = false;
            m_radioModel.sendCommand(QString("slice set %1 dax=0").arg(s->sliceId()));
            m_radioModel.createAudioStream();
        }
    }

    // Push overlay for this slice to the spectrum widget
    pushSliceOverlay(s);

    // Connect slice state changes → spectrum overlay updates
    connect(s, &SliceModel::frequencyChanged, this, [this, s](double mhz) {
        m_updatingFromModel = true;
        spectrum()->setSliceOverlay(s->sliceId(), mhz,
            s->filterLow(), s->filterHigh(), s->isTxSlice(),
            s->sliceId() == m_activeSliceId);
        m_updatingFromModel = false;

        // Feed frequency to Antenna Genius for band→antenna recall
        if (s->sliceId() == m_activeSliceId)
            m_antennaGenius.setRadioFrequency(mhz);
    });

    // Feed current frequency immediately (AG may connect later and reprocess).
    if (s->sliceId() == m_activeSliceId && s->frequency() > 0.0)
        m_antennaGenius.setRadioFrequency(s->frequency());

    connect(s, &SliceModel::filterChanged, this, [this, s](int lo, int hi) {
        spectrum()->setSliceOverlay(s->sliceId(), s->frequency(),
            lo, hi, s->isTxSlice(), s->sliceId() == m_activeSliceId);
    });
    connect(s, &SliceModel::txSliceChanged, this, [this, s](bool tx) {
        spectrum()->setSliceOverlay(s->sliceId(), s->frequency(),
            s->filterLow(), s->filterHigh(), tx,
            s->sliceId() == m_activeSliceId);
    });

    // When the radio notifies us that this slice became active, switch to it
    connect(s, &SliceModel::activeChanged, this, [this, s](bool active) {
        if (active)
            setActiveSlice(s->sliceId());
    });

    // Update filter limits when the active slice's mode changes
    connect(s, &SliceModel::modeChanged, this, [this, s](const QString& mode) {
        if (s->sliceId() == m_activeSliceId)
            updateFilterLimitsForMode(mode);
    });
}

void MainWindow::onSliceRemoved(int id)
{
    qDebug() << "MainWindow: slice removed" << id;
    spectrum()->removeSliceOverlay(id);

    // Reset panadapter state so display settings re-sync after profile load
    m_radioModel.resetPanState();
    m_needAudioStream = true;

    // If the removed slice was active, switch to the first remaining slice
    if (id == m_activeSliceId) {
        m_appletPanel->setSlice(nullptr);
        spectrum()->vfoWidget()->setSlice(nullptr);
        spectrum()->overlayMenu()->setSlice(nullptr);

        const auto& slices = m_radioModel.slices();
        if (!slices.isEmpty())
            setActiveSlice(slices.first()->sliceId());
        else
            m_activeSliceId = -1;
    }
}

SliceModel* MainWindow::activeSlice() const
{
    if (m_activeSliceId < 0) return nullptr;
    return m_radioModel.slice(m_activeSliceId);
}

void MainWindow::setActiveSlice(int sliceId)
{
    auto* s = m_radioModel.slice(sliceId);
    if (!s) return;

    const int prevId = m_activeSliceId;
    m_activeSliceId = sliceId;

    // Tell the radio this slice is now active (don't move TX — user controls that)
    if (sliceId != prevId)
        s->setActive(true);

    // Update all overlay isActive flags
    for (auto* sl : m_radioModel.slices()) {
        const bool isActive = (sl->sliceId() == sliceId);
        spectrum()->setSliceOverlay(sl->sliceId(), sl->frequency(),
            sl->filterLow(), sl->filterHigh(), sl->isTxSlice(), isActive);
    }

    // Re-wire applet panel, overlay menu, VFO widget to the new active slice
    m_panApplet->setSliceId(sliceId);
    m_appletPanel->setSlice(s);
    spectrum()->overlayMenu()->setSlice(s);
    spectrum()->vfoWidget()->setSlice(s);
    spectrum()->vfoWidget()->setTransmitModel(m_radioModel.transmitModel());

    // Update filter limits for the active slice's mode
    updateFilterLimitsForMode(s->mode());

    // Detect band from frequency
    if (m_bandSettings.currentBand().isEmpty())
        m_bandSettings.setCurrentBand(BandSettings::bandForFrequency(s->frequency()));

    qDebug() << "MainWindow: active slice set to" << sliceId;
}

void MainWindow::updateFilterLimitsForMode(const QString& mode)
{
    int minHz, maxHz;
    if (mode == "LSB" || mode == "DIGL" || mode == "CWL") {
        minHz = -12000; maxHz = 0;
    } else if (mode == "AM" || mode == "SAM" || mode == "DSB") {
        minHz = -12000; maxHz = 12000;
    } else if (mode == "FM" || mode == "NFM" || mode == "DFM") {
        minHz = -12000; maxHz = 12000;
    } else {
        // USB, DIGU, CW, RTTY, etc.
        minHz = 0; maxHz = 12000;
    }
    spectrum()->setFilterLimits(minHz, maxHz);
    spectrum()->setMode(mode);
}

void MainWindow::pushSliceOverlay(SliceModel* s)
{
    spectrum()->setSliceOverlay(s->sliceId(), s->frequency(),
        s->filterLow(), s->filterHigh(), s->isTxSlice(),
        s->sliceId() == m_activeSliceId);
}

SpectrumWidget* MainWindow::spectrum() const
{
    return m_panApplet->spectrumWidget();
}

// ─── Band settings capture / restore ──────────────────────────────────────────

BandSnapshot MainWindow::captureCurrentBandState() const
{
    BandSnapshot snap;
    if (auto* s = activeSlice()) {
        snap.frequencyMhz  = s->frequency();
        snap.mode          = s->mode();
        snap.rxAntenna     = s->rxAntenna();
        snap.filterLow     = s->filterLow();
        snap.filterHigh    = s->filterHigh();
        snap.agcMode       = s->agcMode();
        snap.agcThreshold  = s->agcThreshold();
    }
    snap.panCenterMhz    = m_radioModel.panCenterMhz();
    snap.panBandwidthMhz = m_radioModel.panBandwidthMhz();
    snap.minDbm          = spectrum()->refLevel() - spectrum()->dynamicRange();
    snap.maxDbm          = spectrum()->refLevel();
    snap.spectrumFrac    = spectrum()->spectrumFrac();
    snap.rfGain          = spectrum()->rfGainValue();
    snap.wnbOn           = spectrum()->wnbActive();
    return snap;
}

void MainWindow::restoreBandState(const BandSnapshot& snap)
{
    m_updatingFromModel = true;
    if (auto* s = activeSlice()) {
        s->setMode(snap.mode);
        s->setFrequency(snap.frequencyMhz);
        if (!snap.rxAntenna.isEmpty())
            s->setRxAntenna(snap.rxAntenna);
        s->setFilterWidth(snap.filterLow, snap.filterHigh);
        if (!snap.agcMode.isEmpty())
            s->setAgcMode(snap.agcMode);
        s->setAgcThreshold(snap.agcThreshold);
    }
    m_radioModel.setPanCenter(snap.panCenterMhz);
    m_radioModel.setPanBandwidth(snap.panBandwidthMhz);
    m_radioModel.setPanDbmRange(snap.minDbm, snap.maxDbm);
    m_radioModel.setPanRfGain(snap.rfGain);
    m_radioModel.setPanWnb(snap.wnbOn);
    spectrum()->setSpectrumFrac(snap.spectrumFrac);
    spectrum()->setRfGain(snap.rfGain);
    spectrum()->setWnbActive(snap.wnbOn);
    m_updatingFromModel = false;
}

// ─── GUI control handlers ─────────────────────────────────────────────────────

void MainWindow::onFrequencyChanged(double mhz)
{
    // If the slice is locked, snap spectrum back to the current freq.
    if (auto* s = activeSlice(); s && s->isLocked()) {
        m_updatingFromModel = true;
        spectrum()->setVfoFrequency(s->frequency());
        m_updatingFromModel = false;
        return;
    }

    spectrum()->setVfoFrequency(mhz);
    if (!m_updatingFromModel) {
        if (auto* s = activeSlice())
            s->setFrequency(mhz);
    }
}

} // namespace AetherSDR
