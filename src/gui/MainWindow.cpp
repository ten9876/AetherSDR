#include "MainWindow.h"
#include "ConnectionPanel.h"
#include "TitleBar.h"
#include "PanadapterApplet.h"
#include "PanadapterStack.h"
#include "PanLayoutDialog.h"
#include "core/CommandParser.h"
#include "models/PanadapterModel.h"
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
#include "SupportDialog.h"
#include "models/SliceModel.h"
#include "models/MeterModel.h"
#include "models/TunerModel.h"
#include "models/TransmitModel.h"
#include "models/EqualizerModel.h"

#include <memory>
#include <functional>
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
#include <QDialog>
#include <QGridLayout>
#include <QLineEdit>
#include <QCheckBox>
#include <QMenu>
#include <QAction>
#include <QActionGroup>
#include <QLabel>
#include <QCloseEvent>
#include <QMessageBox>
#include <QShortcut>
#include <QPointer>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QProgressDialog>
#include <QThread>
#include "core/AppSettings.h"
#ifdef HAVE_RADE
#include "core/RADEEngine.h"
#endif
#if defined(Q_OS_MAC)
#include "core/VirtualAudioBridge.h"
#include <QFileInfo>
#elif defined(HAVE_PIPEWIRE)
#include "core/PipeWireAudioBridge.h"
#endif
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
    setupKeyboardShortcuts();

    // ── Wire up discovery ──────────────────────────────────────────────────
    connect(&m_discovery, &RadioDiscovery::radioDiscovered,
            m_connPanel, &ConnectionPanel::onRadioDiscovered);
    connect(&m_discovery, &RadioDiscovery::radioUpdated,
            m_connPanel, &ConnectionPanel::onRadioUpdated);
    connect(&m_discovery, &RadioDiscovery::radioLost,
            m_connPanel, &ConnectionPanel::onRadioLost);

    // ── Heartbeat indicator + disconnect detection via TCP ping ─────────
    m_heartbeatMissTimer = new QTimer(this);
    m_heartbeatMissTimer->setInterval(1500);
    connect(m_heartbeatMissTimer, &QTimer::timeout, this, [this]() {
        if (m_titleBar) m_titleBar->onHeartbeatLost();
    });

    // Ping-based heartbeat — covers local, routed, and SmartLink connections
    connect(&m_radioModel, &RadioModel::pingReceived, this, [this]() {
        if (m_titleBar) {
            m_titleBar->onHeartbeat();
            m_heartbeatMissTimer->start(); // reset miss timer
        }
    });

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

        // Pre-bind UDP socket for VITA-49 reception BEFORE requesting
        // connection, so we can pass our port to the SmartLink server.
        // The server tells the radio our public IP:port for UDP streaming.
        quint16 udpPort = m_radioModel.panStream()->localPort();
        if (udpPort == 0) {
            // Not yet bound — start WAN early to get a port
            const quint16 radioUdpPort = static_cast<quint16>(
                info.publicUdpPort > 0 ? info.publicUdpPort : 4993);
            m_radioModel.panStream()->startWan(
                QHostAddress(info.publicIp), radioUdpPort);
            udpPort = m_radioModel.panStream()->localPort();
        }
        qDebug() << "MainWindow: pre-bound UDP port" << udpPort << "for WAN hole punch";
        m_smartLink.requestConnect(info.serial, udpPort);
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
    // Route TX VITA-49 packets through the registered UDP socket
    connect(&m_audio, &AudioEngine::txPacketReady,
            m_radioModel.panStream(), &PanadapterStream::sendToRadio);

    connect(&m_radioModel, &RadioModel::txAudioStreamReady,
            this, [this](quint32 streamId) {
        m_audio.setTxStreamId(streamId);
        m_audio.setOpusTxEnabled(m_radioModel.audioCompressionParam() == "opus");
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
            // Only start if TX stream ID is already assigned (avoid streamId=0)
            if (!m_audio.isTxStreaming() && m_audio.txStreamId() != 0) {
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
        // Set transmitting on ALL spectrums (multi-pan aware).
        // Each spectrum's m_hasTxSlice flag determines whether it freezes.
        for (auto* pan : m_radioModel.panadapters()) {
            if (auto* sw = m_panStack ? m_panStack->spectrum(pan->panId()) : nullptr)
                sw->setTransmitting(tx);
        }
        if (!m_panStack && m_panApplet)
            m_panApplet->spectrumWidget()->setTransmitting(tx);
        m_audio.setTransmitting(tx);

        // Update TX status bar indicator
        if (tx) {
            m_txIndicator->setText("TX");
            m_txIndicator->setStyleSheet("QLabel { color: white; background: #c03030; font-weight: bold; font-size: 21px; border-radius: 4px; padding: 0px 1px; }");
        } else {
            m_txIndicator->setText("TX");
            m_txIndicator->setStyleSheet("QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 21px; }");
        }
        // On RX resume, native tiles will restart and m_hasNativeWaterfall
        // will be set again by the first arriving tile.
#ifdef HAVE_RADE
        if (m_radeSliceId >= 0 && m_radeEngine && m_radeEngine->isActive()) {
            if (!tx) {
                m_audio.setRadeMode(false);
                m_radeEngine->resetTx();
            }
        }
#endif
#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
        if (m_daxBridge)
            m_daxBridge->setTransmitting(tx);
#endif
#ifdef HAVE_SERIALPORT
        m_serialPort.setTransmitting(tx);
#endif
    });

    // Sync show-TX-in-waterfall setting to all spectrum widgets
    auto syncShowTxWf = [this]() {
        bool show = m_radioModel.transmitModel()->showTxInWaterfall();
        for (auto* pan : m_radioModel.panadapters()) {
            if (auto* sw = m_panStack ? m_panStack->spectrum(pan->panId()) : nullptr)
                sw->setShowTxInWaterfall(show);
        }
        if (!m_panStack && m_panApplet)
            m_panApplet->spectrumWidget()->setShowTxInWaterfall(show);
    };
    connect(m_radioModel.transmitModel(), &TransmitModel::stateChanged,
            this, syncShowTxWf);

    // ── Panadapter stream → spectrum widget ───────────────────────────────
    // Route FFT/waterfall data to the correct SpectrumWidget by stream ID
    connect(m_radioModel.panStream(), &PanadapterStream::spectrumReady,
            this, [this](quint32 streamId, const QVector<float>& bins) {
        for (auto* pan : m_radioModel.panadapters()) {
            if (pan->panStreamId() == streamId) {
                if (auto* sw = m_panStack->spectrum(pan->panId()))
                    sw->updateSpectrum(bins);
                return;
            }
        }
        // Fallback: active spectrum (covers "default" pan before radio connects)
        if (auto* sw = spectrum()) sw->updateSpectrum(bins);
    });
    connect(m_radioModel.panStream(), &PanadapterStream::waterfallRowReady,
            this, [this](quint32 streamId, const QVector<float>& bins,
                         double low, double high, quint32 tc) {
        for (auto* pan : m_radioModel.panadapters()) {
            if (pan->wfStreamId() == streamId) {
                if (auto* sw = m_panStack->spectrum(pan->panId()))
                    sw->updateWaterfallRow(bins, low, high, tc);
                return;
            }
        }
        if (auto* sw = spectrum()) sw->updateWaterfallRow(bins, low, high, tc);
    });
    connect(m_radioModel.panStream(), &PanadapterStream::waterfallAutoBlackLevel,
            this, [this](quint32 streamId, quint32 autoBlack) {
        for (auto* pan : m_radioModel.panadapters()) {
            if (pan->wfStreamId() == streamId) {
                if (auto* sw = m_panStack->spectrum(pan->panId())) {
                    if (sw->wfAutoBlack()) {
                        const int level = std::clamp(static_cast<int>(autoBlack), 0, 125);
                        sw->setWfBlackLevel(level);
                    }
                }
                return;
            }
        }
    });
    // Legacy panadapterInfoChanged — only used for initial display settings push.
    // Per-pan frequency/level tracking is done via PanadapterModel signals in panadapterAdded.
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
            bool wnbOn = s.value(spectrum()->settingsKey("DisplayWnbEnabled"), "False").toString() == "True";
            int wnbLevel = s.value(spectrum()->settingsKey("DisplayWnbLevel"), "50").toInt();
            int rfGain = s.value(spectrum()->settingsKey("DisplayRfGain"), "0").toInt();
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
    // ── Multi-panadapter lifecycle ──────────────────────────────────────────
    connect(&m_radioModel, &RadioModel::panadapterAdded,
            this, [this](PanadapterModel* pan) {
        // During layout application, applyLayout/createPansSequentially handles
        // applet creation and wiring — don't duplicate here.
        if (m_applyingLayout) return;

        // Skip if this pan already has an applet
        if (m_panStack->panadapter(pan->panId())) {
            connect(pan, &PanadapterModel::infoChanged,
                    m_panStack->spectrum(pan->panId()), &SpectrumWidget::setFrequencyRange);
            connect(pan, &PanadapterModel::levelChanged,
                    m_panStack->spectrum(pan->panId()), &SpectrumWidget::setDbmRange);
            return;
        }

        PanadapterApplet* applet = nullptr;

        // If applyLayout already created this applet, just wire signals
        if (m_panStack->panadapter(pan->panId())) {
            applet = m_panStack->panadapter(pan->panId());
        }
        // Reuse the "default" placeholder for the first real pan
        else if (m_panStack->panadapter("default")) {
            applet = m_panStack->panadapter("default");
            applet->setPanId(pan->panId());
            m_panStack->rekey("default", pan->panId());
        } else {
            applet = m_panStack->addPanadapter(pan->panId());
        }
        m_panApplet = applet;
        wirePanadapter(applet);
        connect(pan, &PanadapterModel::infoChanged,
                applet->spectrumWidget(), &SpectrumWidget::setFrequencyRange);
        connect(pan, &PanadapterModel::levelChanged,
                applet->spectrumWidget(), &SpectrumWidget::setDbmRange);

        // Push display dimensions to the radio so it sends full-size FFT bins.
        // Without this, the radio uses xpixels=50 ypixels=20 (default) and
        // FFT data is essentially empty/unusable. Use actual widget dimensions
        // for 1:1 bin-to-pixel mapping (matches SmartSDR behavior from pcap).
        auto* sw = applet->spectrumWidget();
        int xpix = sw ? sw->width() : 1024;
        int ypix = sw ? sw->height() : 700;
        if (xpix < 100) xpix = 1024;  // widget may not be laid out yet
        if (ypix < 100) ypix = 700;
        m_radioModel.sendCommand(
            QString("display pan set %1 xpixels=%2 ypixels=%3")
                .arg(pan->panId()).arg(xpix).arg(ypix));

        qDebug() << "MainWindow: added panadapter applet for" << pan->panId();

        // Debounced layout restore: after all pans are added on connect,
        // rearrange to the saved layout (e.g. 2h instead of default vertical).
        if (!m_layoutRestoreTimer) {
            m_layoutRestoreTimer = new QTimer(this);
            m_layoutRestoreTimer->setSingleShot(true);
            m_layoutRestoreTimer->setInterval(1000);
            connect(m_layoutRestoreTimer, &QTimer::timeout, this, [this]() {
                const QString saved = AppSettings::instance()
                    .value("PanadapterLayout", "1").toString();
                if (saved == "1") return;
                // applyPanLayout handles both adding and removing pans
                // to match the saved layout's expected count
                applyPanLayout(saved);
            });
        }
        m_layoutRestoreTimer->start();  // restart on each new pan
    });
    connect(&m_radioModel, &RadioModel::panadapterRemoved,
            this, [this](const QString& panId) {
        // Disconnect all signals from the dying applet's widgets to prevent
        // dangling pointer crashes in wirePanadapter lambdas (#242)
        if (auto* applet = m_panStack->panadapter(panId)) {
            if (auto* sw = applet->spectrumWidget()) {
                sw->disconnect(this);
                if (auto* menu = sw->overlayMenu())
                    menu->disconnect(this);
            }
        }
        m_panStack->removePanadapter(panId);
        qDebug() << "MainWindow: removed panadapter applet for" << panId;
    });

    // ── Per-panadapter signal wiring (extracted for multi-pan support) ──────
    wirePanadapter(m_panApplet);

    // Display overlay connections are now per-pan in wirePanadapter().

    // ── Panadapter stream → audio engine ──────────────────────────────────
    // All VITA-49 traffic arrives on the single client udpport socket owned
    // by PanadapterStream. It strips the header from IF-Data packets and emits
    // audioDataReady(); we feed that directly to the QAudioSink.
    connect(m_radioModel.panStream(), &PanadapterStream::audioDataReady,
            &m_audio, &AudioEngine::feedAudioData);

    // ── CW decoder: feed audio + display decoded text ─────────────────────
    connect(m_radioModel.panStream(), &PanadapterStream::audioDataReady,
            &m_cwDecoder, &CwDecoder::feedAudio);
    connect(&m_cwDecoder, &CwDecoder::textDecoded,
            m_panApplet, &PanadapterApplet::appendCwText);
    connect(&m_cwDecoder, &CwDecoder::statsUpdated,
            m_panApplet, &PanadapterApplet::setCwStats);

    // ── AF gain from applet panel → radio per-slice audio_level ─────────
    connect(m_appletPanel->rxApplet(), &RxApplet::afGainChanged, this, [this](int v) {
        if (auto* s = activeSlice()) s->setAudioGain(v);
    });

    // ── NR2/RN2 feedback: AudioEngine → all VFO + overlay buttons ──────
    // Iterate all panadapter spectrums to find VFO widgets and overlay menus,
    // since spectrum()/vfoWidget() lookups can return null depending on
    // pan activation state.
    auto syncNr2 = [this](bool on) {
        // Iterate PanadapterStack's actual applets — avoids RadioModel→PanStack
        // ID mismatch during connect when rekey hasn't happened yet.
        if (m_panStack) {
            for (auto* applet : m_panStack->allApplets()) {
                auto* sw = applet->spectrumWidget();
                if (auto* vfo = sw->vfoWidget(m_activeSliceId)) {
                    QSignalBlocker sb(vfo->nr2Button());
                    vfo->nr2Button()->setChecked(on);
                }
                if (auto* btn = sw->overlayMenu()->dspNr2Button())
                    { QSignalBlocker sb(btn); btn->setChecked(on); }
            }
        }
    };
    auto syncRn2 = [this](bool on) {
        if (m_panStack) {
            for (auto* applet : m_panStack->allApplets()) {
                auto* sw = applet->spectrumWidget();
                if (auto* vfo = sw->vfoWidget(m_activeSliceId)) {
                    QSignalBlocker sb(vfo->rn2Button());
                    vfo->rn2Button()->setChecked(on);
                }
                if (auto* btn = sw->overlayMenu()->dspRn2Button())
                    { QSignalBlocker sb(btn); btn->setChecked(on); }
            }
        }
    };
    connect(&m_audio, &AudioEngine::nr2EnabledChanged, this, syncNr2);
    connect(&m_audio, &AudioEngine::rn2EnabledChanged, this, syncRn2);
    // NR2/RN2 overlay sync is wired in wirePanadapter()
    // RxApplet NR button 3-state cycle → NR2 enable/disable
    connect(m_appletPanel->rxApplet(), &RxApplet::nr2CycleToggled,
            this, [this](bool on) {
        if (auto* vfo = spectrum()->vfoWidget())
            vfo->nr2Button()->setChecked(on);
    });
    // Sync RxApplet NR button visual when NR2 state changes
    connect(&m_audio, &AudioEngine::nr2EnabledChanged,
            this, [this](bool on) {
        auto* rx = m_appletPanel->rxApplet();
        if (on) {
            if (auto* s = activeSlice(); s && s->nrOn())
                s->setNr(false);
            rx->setNrState(2);
        } else if (rx->nrState() == 2) {
            rx->setNrState(0);
        }
    });

    // ── RxApplet RNN 3-state cycle → RN2 enable/disable ────────────────────
    connect(m_appletPanel->rxApplet(), &RxApplet::rn2CycleToggled,
            this, [this](bool on) {
        m_audio.setRn2Enabled(on);
    });
    // Sync RxApplet RNN button visual when RN2 state changes
    connect(&m_audio, &AudioEngine::rn2EnabledChanged,
            this, [this](bool on) {
        auto* rx = m_appletPanel->rxApplet();
        if (on) {
            rx->setRnnState(2);
        } else if (rx->rnnState() == 2) {
            rx->setRnnState(0);
        }
    });

#ifdef HAVE_RADE
    connect(m_appletPanel->rxApplet(), &RxApplet::radeActivated,
            this, [this](bool on, int sliceId) { if (on) activateRADE(sliceId); else deactivateRADE(); });
#endif

    // ── Tuning step size → spectrum widget ─────────────────────────────────
    connect(m_appletPanel->rxApplet(), &RxApplet::stepSizeChanged,
            spectrum(), &SpectrumWidget::setStepSize);
    connect(m_appletPanel->rxApplet(), &RxApplet::stepSizeChanged,
            this, [](int step) {
        auto& s = AppSettings::instance();
        s.setValue("TuningStepSize", QString::number(step));
        s.save();
    });
    int savedStep = AppSettings::instance().value("TuningStepSize", "100").toInt();
    for (auto* a : m_panStack->allApplets()) a->spectrumWidget()->setStepSize(savedStep);
    m_appletPanel->rxApplet()->setInitialStepSize(savedStep);

    // ── Antenna list from radio → applet panel ─────────────────────────────
    connect(&m_radioModel, &RadioModel::antListChanged,
            m_appletPanel, &AppletPanel::setAntennaList);
    connect(&m_radioModel, &RadioModel::antListChanged,
            spectrum()->overlayMenu(), &SpectrumOverlayMenu::setAntennaList);
    // Antenna list and S-meter are now wired per-widget in onSliceAdded.

    // ── Title bar: PC Audio, master volume, headphone volume ────────────────
    connect(m_titleBar, &TitleBar::pcAudioToggled, this, [this](bool on) {
        if (on) m_radioModel.createRxAudioStream();
        else    m_radioModel.removeRxAudioStream();
    });
    connect(m_titleBar, &TitleBar::masterVolumeChanged, this, [this](int pct) {
        m_audio.setRxVolume(pct / 100.0f);
        auto& s = AppSettings::instance();
        s.setValue("MasterVolume", QString::number(pct));
        s.save();
    });
    connect(m_titleBar, &TitleBar::headphoneVolumeChanged,
            &m_radioModel, &RadioModel::setHeadphoneGain);
    connect(&m_radioModel, &RadioModel::audioOutputChanged, this, [this]() {
        m_titleBar->setHeadphoneVolume(m_radioModel.headphoneGain());
    });

    // Multi-Flex: show when another client is transmitting
    connect(&m_radioModel, &RadioModel::txOwnerChanged,
            m_titleBar, &TitleBar::setOtherClientTx);

    // Multi-Flex: title bar indicator when other clients are connected
    connect(&m_radioModel, &RadioModel::otherClientsChanged,
            m_titleBar, &TitleBar::setMultiFlexStatus);

    // Apply saved master volume
    int savedMasterVol = AppSettings::instance().value("MasterVolume", "100").toInt();
    m_audio.setRxVolume(savedMasterVol / 100.0f);

    // ── S-Meter: MeterModel → SMeterWidget (active slice only) ─────────────
    connect(m_radioModel.meterModel(), &MeterModel::sLevelChanged,
            this, [this](int sliceIndex, float dbm) {
        if (sliceIndex == m_activeSliceId)
            m_appletPanel->sMeterWidget()->setLevel(dbm);
    });
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
    connect(m_radioModel.tunerModel(), &TunerModel::presenceChanged,
            this, [this](bool present) {
        m_tgxlIndicator->setVisible(present);
    });

    // Switch Fwd Power gauge scale based on radio max power and amplifier presence.
    // All three power gauges (TxApplet, TunerApplet, SMeterWidget) update together.
    auto updatePowerScale = [this]() {
        int maxW = m_radioModel.transmitModel()->maxPowerLevel();
        bool hasAmp = m_radioModel.hasAmplifier();
        m_appletPanel->txApplet()->setPowerScale(maxW, hasAmp);
        m_appletPanel->tunerApplet()->setPowerScale(maxW, hasAmp);
        m_appletPanel->sMeterWidget()->setPowerScale(maxW, hasAmp);
    };
    connect(&m_radioModel, &RadioModel::amplifierChanged, this, updatePowerScale);
    connect(&m_radioModel, &RadioModel::amplifierChanged, this, [this](bool present) {
        m_pgxlIndicator->setVisible(present);
    });
    connect(m_radioModel.transmitModel(), &TransmitModel::maxPowerLevelChanged,
            this, updatePowerScale);

    // ── TX applet: meters + model ───────────────────────────────────────────
    connect(m_radioModel.meterModel(), &MeterModel::txMetersChanged,
            m_appletPanel->txApplet(), &TxApplet::updateMeters);
    m_appletPanel->txApplet()->setTransmitModel(m_radioModel.transmitModel());
    m_appletPanel->txApplet()->setTunerModel(m_radioModel.tunerModel());
    m_appletPanel->rxApplet()->setTransmitModel(m_radioModel.transmitModel());

    // Hide APD row on radios that don't support it
    connect(m_radioModel.transmitModel(), &TransmitModel::apdStateChanged, this, [this]() {
        m_appletPanel->txApplet()->setApdVisible(
            m_radioModel.transmitModel()->apdConfigurable());
    });

    // ── Serial port PTT/CW keying ───────────────────────────────────────
#ifdef HAVE_SERIALPORT
    m_serialPort.loadSettings();
    connect(&m_serialPort, &SerialPortController::externalPttChanged,
            this, [this](bool active) {
        m_radioModel.setTransmit(active);
    });
    connect(&m_serialPort, &SerialPortController::cwKeyChanged,
            this, [this](bool down) {
        m_radioModel.sendCwKey(down);
    });
    connect(&m_serialPort, &SerialPortController::cwPaddleChanged,
            this, [this](bool dit, bool dah) {
        m_radioModel.sendCwPaddle(dit, dah);
    });
#endif

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

#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
    // DAX enable button in CatApplet → start/stop DAX bridge
    connect(m_appletPanel->catApplet(), &CatApplet::daxToggled,
            this, [this](bool on) {
        if (on)
            startDax();
        else
            stopDax();
    });
#endif

    // ── Status bar telemetry ──────────────────────────────────────────────────
    connect(&m_radioModel, &RadioModel::networkQualityChanged,
            this, [this](const QString& quality, int pingMs) {
        // Color code: Excellent/VeryGood=green, Good=cyan, Fair=amber, Poor=red
        QString color = "#00cc66";
        if (quality == "Fair") color = "#cc9900";
        else if (quality == "Poor") color = "#cc3333";
        else if (quality == "Good") color = "#00b4d8";
        m_networkLabel->setText(QString("[<span style='color:%1'>%2</span>]")
            .arg(color, quality));
        m_networkLabel->setToolTip(QString("Network: %1\nLatency (RTT): %2")
            .arg(quality, pingMs < 1 ? "< 1 ms" : QString("%1 ms").arg(pingMs)));
    });

    connect(m_radioModel.meterModel(), &MeterModel::hwTelemetryChanged,
            this, [this](float paTemp, float supplyVolts) {
        m_paTempLabel->setText(QString("PA %1\u00B0C").arg(paTemp, 0, 'f', 1));
        m_supplyVoltLabel->setText(QString("%1 V").arg(supplyVolts, 0, 'f', 2));

        // Update station label (nickname arrives via status after connect)
        const QString nick = m_radioModel.nickname();
        if (!nick.isEmpty())
            m_stationLabel->setText(nick);
    });

    connect(&m_radioModel, &RadioModel::gpsStatusChanged,
            this, [this](const QString& status, int tracked, int visible,
                         const QString& grid, const QString& /*alt*/,
                         const QString& /*lat*/, const QString& /*lon*/,
                         const QString& utcTime) {
        const bool gpsPresent = (status != "Not Present" && status != "");
        m_gpsLabel->setText(gpsPresent
            ? QString("GPS: %1/%2").arg(tracked).arg(visible)
            : "GPS: N/A");
        m_gpsStatusLabel->setText(gpsPresent
            ? QString("[%1]").arg(status)
            : "");

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
    // Clear stale 3-pane splitter state — now 2-pane layout.
    s.remove("SplitterState");
    // Force 2-pane sizing: spectrum gets all remaining width, applet panel fixed at 260px
    QTimer::singleShot(0, this, [this]() {
        m_splitter->setSizes({width() - 260, 260});
    });

    // Auto-popup connection dialog if no saved radio
    QString lastSerial = s.value("LastConnectedRadioSerial", "").toString();
    if (lastSerial.isEmpty()) {
        QTimer::singleShot(500, this, [this]() { toggleConnectionDialog(); });
    }
}

MainWindow::~MainWindow()
{
#ifdef HAVE_RADE
    if (m_radeSliceId >= 0)
        deactivateRADE();
#endif
    // Stop audio processing before members are destroyed.
    // PanadapterStream may still deliver packets on its UDP thread —
    // stopping the stream and disconnecting signals prevents
    // use-after-free in NR2/RN2 during destruction.
    m_audio.setNr2Enabled(false);
    m_audio.setRn2Enabled(false);
    m_audio.stopRxStream();
    m_audio.stopTxStream();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    auto& s = AppSettings::instance();
    s.setValue("MainWindowGeometry", saveGeometry().toBase64());
    s.setValue("MainWindowState",   saveState().toBase64());
    // SplitterState no longer saved (2-pane layout uses stretch factors)
    // ConnPanelCollapsed removed — panel is now a popup dialog

    // Save active slice frequency/mode for restore on next launch
    auto* sl = activeSlice();
    if (sl) {
        s.setValue("LastFrequency", QString::number(sl->frequency(), 'f', 6));
        s.setValue("LastMode", sl->mode());
        s.setValue("LastDaxChannel", QString::number(sl->daxChannel()));
    }

    // Save client-side DSP state before destructor disables them
    s.setValue("ClientNr2Enabled", m_audio.nr2Enabled() ? "True" : "False");
    s.setValue("ClientRn2Enabled", m_audio.rn2Enabled() ? "True" : "False");

    s.save();
    m_discovery.stopListening();
    m_radioModel.disconnectFromRadio();
    m_audio.stopRxStream();
    QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_networkLabel && event->type() == QEvent::MouseButtonDblClick) {
        NetworkDiagnosticsDialog dlg(&m_radioModel, this);
        dlg.exec();
        return true;
    }
    if (obj == m_stationNickLabel && event->type() == QEvent::MouseButtonDblClick) {
        toggleConnectionDialog();
        return true;
    }
    if (obj == m_tnfIndicator && event->type() == QEvent::MouseButtonPress) {
        m_radioModel.tnfModel()->setGlobalEnabled(!m_radioModel.tnfModel()->globalEnabled());
        return true;
    }
    if (obj == m_fdxIndicator && event->type() == QEvent::MouseButtonPress) {
        bool on = !m_radioModel.fullDuplexEnabled();
        m_radioModel.sendCommand(QString("radio set full_duplex_enabled=%1").arg(on ? 1 : 0));
        // Optimistic update — radio accepts this command (R|0) but doesn't
        // echo back a status update with the new value.
        m_radioModel.setFullDuplex(on);
        return true;
    }
    if (obj == m_addPanLabel && event->type() == QEvent::MouseButtonPress) {
        if (!m_radioModel.isConnected()) return true;
        const QString& model = m_radioModel.model();
        bool dualScu = model.contains("6600") || model.contains("6700")
                    || model.contains("8600") || model.contains("AU-520");
        int maxPans = dualScu ? 4 : 2;
        // Determine current layout from actual pan count, not saved setting
        int activePanCount = m_panStack ? m_panStack->count() : 1;
        QString currentLayout = "1";
        if (activePanCount >= 2)
            currentLayout = AppSettings::instance()
                .value("PanadapterLayout", "1").toString();
        PanLayoutDialog dlg(maxPans, currentLayout, this);
        if (dlg.exec() == QDialog::Accepted && !dlg.selectedLayout().isEmpty()) {
            const QString layoutId = dlg.selectedLayout();
            auto& s = AppSettings::instance();
            s.setValue("PanadapterLayout", layoutId);
            s.save();
            applyPanLayout(layoutId);
        }
        return true;
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::toggleConnectionDialog()
{
    if (m_connPanel->isVisible()) {
        m_connPanel->hide();
        return;
    }
    // Position above the status bar, centered on station label
    QPoint statusBarTop = statusBar()->mapToGlobal(QPoint(0, 0));
    QPoint labelCenter = m_stationNickLabel->mapToGlobal(
        QPoint(m_stationNickLabel->width() / 2, 0));
    int dlgW = m_connPanel->width();
    int dlgH = m_connPanel->height();
    QPoint pos(labelCenter.x() - dlgW / 2,
               statusBarTop.y() - dlgH - 4);
    m_connPanel->move(pos);
    m_connPanel->show();
    m_connPanel->raise();
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
        // Snapshot compression setting before dialog opens
        QString prevComp = m_radioModel.audioCompressionParam();

        auto* dlg = new RadioSetupDialog(&m_radioModel, &m_audio, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QDialog::finished, this, [this, prevComp]() {
#ifdef HAVE_SERIALPORT
            // Re-load serial port settings if changed
            m_serialPort.loadSettings();
#endif
            // Re-evaluate CW decode overlay visibility
            bool decodeOn = AppSettings::instance().value("CwDecodeOverlay", "True").toString() == "True";
            auto* s = activeSlice();
            if (s) {
                bool isCw = (s->mode() == "CW" || s->mode() == "CWL");
                m_panApplet->setCwPanelVisible(isCw && decodeOn);
            }

            // If audio compression changed, recreate the RX audio stream
            QString newComp = m_radioModel.audioCompressionParam();
            if (newComp != prevComp && m_radioModel.isConnected()) {
                qDebug() << "MainWindow: audio compression changed from" << prevComp
                         << "to" << newComp << "— recreating audio stream";
                m_radioModel.removeRxAudioStream();
                m_audio.setOpusTxEnabled(newComp == "opus");
                QTimer::singleShot(500, this, [this]() {
                    m_radioModel.createRxAudioStream();
                });
            }
        });
        dlg->show();
    });

    auto* chooseRadio = settingsMenu->addAction("Choose Radio / SmartLink Setup...");
    connect(chooseRadio, &QAction::triggered, this, [this] {
        toggleConnectionDialog();
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
        // Update total spots count in dialog
        SpotSettingsDialog dlg(&m_radioModel, this);
        dlg.setTotalSpots(m_radioModel.spotModel()->spots().size());
        dlg.exec();
        // Refresh all spot settings after dialog closes
        auto& s = AppSettings::instance();
        bool on       = s.value("IsSpotsEnabled", "True").toString() == "True";
        int fontSize  = s.value("SpotFontSize", "16").toInt();
        int levels    = s.value("SpotsMaxLevel", "3").toInt();
        int position  = s.value("SpotsStartingHeightPercentage", "50").toInt();
        bool override = s.value("IsSpotsOverrideColorsEnabled", "False").toString() == "True";
        for (auto* a : m_panStack->allApplets()) {
            auto* sw = a->spectrumWidget();
            sw->setShowSpots(on);
            sw->setSpotFontSize(fontSize);
            sw->setSpotMaxLevels(levels);
            sw->setSpotStartPct(position);
            sw->setSpotOverrideColors(override);
        }
    });
    settingsMenu->addAction("multiFLEX...");
    auto* txBandAct = settingsMenu->addAction("TX Band Settings...");
    connect(txBandAct, &QAction::triggered, this, [this] {
        if (!m_radioModel.isConnected()) {
            statusBar()->showMessage("Not connected to radio", 3000);
            return;
        }
        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(QString("TX Band Settings (Current TX Profile: %1)")
            .arg(m_radioModel.transmitModel()->activeProfile()));
        dlg->setMinimumSize(700, 450);
        dlg->setStyleSheet("QDialog { background: #0f0f1a; }");
        dlg->setAttribute(Qt::WA_DeleteOnClose);

        auto* vb = new QVBoxLayout(dlg);
        auto* headerGrid = new QGridLayout;
        headerGrid->setSpacing(1);
        const QStringList headers = {"Band", "RF PWR(%)", "Tune PWR(%)", "PTT Inhibit",
                                      "ACC TX", "RCA TX Req", "ACC TX Req",
                                      "RCA TX1", "RCA TX2", "RCA TX3", "HWALC"};
        for (int c = 0; c < headers.size(); ++c) {
            auto* lbl = new QLabel(headers[c]);
            lbl->setStyleSheet("QLabel { color: #8aa8c0; font-size: 10px; "
                                "font-weight: bold; background: #1a2a3a; "
                                "border: 1px solid #304050; padding: 2px 4px; }");
            lbl->setAlignment(Qt::AlignCenter);
            headerGrid->addWidget(lbl, 0, c);
        }

        const auto& bands = m_radioModel.txBandSettings();
        QList<int> sortedIds = bands.keys();
        std::sort(sortedIds.begin(), sortedIds.end());

        static const QString kEditStyle =
            "QLineEdit { background: #0a0a18; color: #c8d8e8; border: 1px solid #304050; "
            "padding: 2px; font-size: 11px; }";
        static const QString kCbStyle =
            "QCheckBox { spacing: 0px; }"
            "QCheckBox::indicator { width: 14px; height: 14px; }";

        int row = 1;
        for (int id : sortedIds) {
            const auto& b = bands[id];
            int col = 0;

            auto* nameLbl = new QLabel(b.bandName);
            nameLbl->setStyleSheet("QLabel { color: #c8d8e8; font-size: 11px; "
                                    "font-weight: bold; background: #0f0f1a; "
                                    "border: 1px solid #203040; padding: 2px 4px; }");
            headerGrid->addWidget(nameLbl, row, col++);

            auto* rfEdit = new QLineEdit(QString::number(b.rfPower));
            rfEdit->setStyleSheet(kEditStyle);
            rfEdit->setFixedWidth(50);
            rfEdit->setAlignment(Qt::AlignCenter);
            int bandId = id;
            connect(rfEdit, &QLineEdit::editingFinished, dlg, [this, rfEdit, bandId] {
                m_radioModel.sendCommand(
                    QString("transmit bandset %1 rfpower=%2").arg(bandId).arg(rfEdit->text()));
            });
            headerGrid->addWidget(rfEdit, row, col++);

            auto* tuneEdit = new QLineEdit(QString::number(b.tunePower));
            tuneEdit->setStyleSheet(kEditStyle);
            tuneEdit->setFixedWidth(50);
            tuneEdit->setAlignment(Qt::AlignCenter);
            connect(tuneEdit, &QLineEdit::editingFinished, dlg, [this, tuneEdit, bandId] {
                m_radioModel.sendCommand(
                    QString("transmit bandset %1 tunepower=%2").arg(bandId).arg(tuneEdit->text()));
            });
            headerGrid->addWidget(tuneEdit, row, col++);

            struct CbDef { bool val; const char* txCmd; const char* ilCmd; };
            CbDef cbs[] = {
                {b.inhibit, "inhibit", nullptr},
                {b.accTx,   nullptr, "acc_tx_enabled"},
                {b.rcaTxReq,nullptr, "rca_txreq_enable"},
                {b.accTxReq,nullptr, "acc_txreq_enable"},
                {b.tx1,     nullptr, "tx1_enabled"},
                {b.tx2,     nullptr, "tx2_enabled"},
                {b.tx3,     nullptr, "tx3_enabled"},
                {b.hwAlc,   "hwalc_enabled", nullptr},
            };

            for (const auto& cb : cbs) {
                auto* chk = new QCheckBox;
                chk->setChecked(cb.val);
                chk->setStyleSheet(kCbStyle);
                auto* w = new QWidget;
                auto* hb = new QHBoxLayout(w);
                hb->setContentsMargins(0, 0, 0, 0);
                hb->setAlignment(Qt::AlignCenter);
                hb->addWidget(chk);
                const char* txC = cb.txCmd;
                const char* ilC = cb.ilCmd;
                connect(chk, &QCheckBox::toggled, dlg, [this, bandId, txC, ilC](bool on) {
                    if (txC)
                        m_radioModel.sendCommand(
                            QString("transmit bandset %1 %2=%3").arg(bandId).arg(txC).arg(on ? 1 : 0));
                    if (ilC)
                        m_radioModel.sendCommand(
                            QString("interlock bandset %1 %2=%3").arg(bandId).arg(ilC).arg(on ? 1 : 0));
                });
                headerGrid->addWidget(w, row, col++);
            }
            ++row;
        }

        vb->addLayout(headerGrid);
        vb->addStretch();
        dlg->show();
    });

    auto* tuneInhibitAct = settingsMenu->addAction("Inhibit amplifier during TUNE");
    tuneInhibitAct->setCheckable(true);
    tuneInhibitAct->setChecked(
        AppSettings::instance().value("TuneInhibitAmp", "False").toString() == "True");
    tuneInhibitAct->setToolTip(
        "Temporarily disable ACC TX output during TUNE to protect external amplifiers.\n"
        "ACC TX is automatically restored when TUNE completes.");
    connect(tuneInhibitAct, &QAction::toggled, this, [](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("TuneInhibitAmp", on ? "True" : "False");
        s.save();
    });

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

    auto* bandPlanAct = viewMenu->addAction("Band Plan Overlay");
    bandPlanAct->setCheckable(true);
    bandPlanAct->setChecked(
        AppSettings::instance().value("ShowBandPlan", "True").toString() == "True");
    connect(bandPlanAct, &QAction::toggled, this, [this](bool on) {
        for (auto* a : m_panStack->allApplets()) a->spectrumWidget()->setShowBandPlan(on);
        AppSettings::instance().setValue("ShowBandPlan", on ? "True" : "False");
        AppSettings::instance().save();
    });

    // UI Scale submenu — sets QT_SCALE_FACTOR, requires restart
    auto* scaleMenu = viewMenu->addMenu("UI Scale");
    int savedScale = AppSettings::instance().value("UiScalePercent", "100").toInt();
    auto* scaleGroup = new QActionGroup(scaleMenu);
    for (int pct : {75, 85, 100, 110, 125, 150, 175, 200}) {
        auto* act = scaleMenu->addAction(QString("%1%").arg(pct));
        act->setCheckable(true);
        act->setChecked(pct == savedScale);
        scaleGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, pct] {
            AppSettings::instance().setValue("UiScalePercent", QString::number(pct));
            AppSettings::instance().save();
            QMessageBox::information(this, "UI Scale",
                QString("UI scale set to %1%. Restart AetherSDR for the change to take effect.").arg(pct));
        });
    }

    viewMenu->addSeparator();
    m_keyboardShortcutsEnabled = AppSettings::instance()
        .value("KeyboardShortcutsEnabled", "False").toString() == "True";
    auto* kbAct = viewMenu->addAction("Keyboard Shortcuts");
    kbAct->setCheckable(true);
    kbAct->setChecked(m_keyboardShortcutsEnabled);
    connect(kbAct, &QAction::toggled, this, [this](bool on) {
        m_keyboardShortcutsEnabled = on;
        AppSettings::instance().setValue("KeyboardShortcutsEnabled", on ? "True" : "False");
        AppSettings::instance().save();
    });

    viewMenu->addSeparator();
    auto* themeAct = viewMenu->addAction("Toggle Dark/Light Theme");
    connect(themeAct, &QAction::triggered, this, [this]{
        // Placeholder — full theme switching left as an exercise
        applyDarkTheme();
    });

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("Support...", this, [this]() {
        SupportDialog dlg(this);
        dlg.setRadioModel(&m_radioModel);
        dlg.exec();
    });
    helpMenu->addSeparator();
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
    // ── Title bar + central splitter ─────────────────────────────────────────
    m_titleBar = new TitleBar(this);
    // Embed the menu bar into the title bar (left side)
    m_titleBar->setMenuBar(menuBar());

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setHandleWidth(0);

    auto* central = new QWidget(this);
    auto* vbox = new QVBoxLayout(central);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_titleBar);
    vbox->addWidget(m_splitter, 1);
    setCentralWidget(central);
    auto* splitter = m_splitter;

    // Connection panel — floating popup (not in splitter)
    m_connPanel = new ConnectionPanel(this);
    m_connPanel->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    m_connPanel->setFixedSize(300, 420);
    m_connPanel->hide();

    // Centre — panadapter stack (one or more FFT + waterfall panes)
    m_panStack = new PanadapterStack(splitter);
    m_panApplet = m_panStack->addPanadapter("default");
    splitter->addWidget(m_panStack);

    // Sync RadioModel's active pan/wf IDs when PanadapterStack focus changes.
    // This ensures display setting commands (fps, average, black_level, etc.)
    // target the correct pan.
    // Sync RadioModel's active pan ID when PanadapterStack focus changes.
    // This ensures display setting commands (fps, average, black_level, etc.)
    // target the correct pan — activeWfId() derives from activePanadapter()
    // which uses m_activePanId.
    connect(m_panStack, &PanadapterStack::activePanChanged,
            this, [this](const QString& panId) {
        m_radioModel.setActivePanId(panId);
    });
    splitter->setStretchFactor(0, 1);

    // Right — applet panel (includes S-Meter)
    m_appletPanel = new AppletPanel(splitter);
    splitter->addWidget(m_appletPanel);
    splitter->setStretchFactor(1, 0);
    splitter->setCollapsible(1, false);

    // Set initial splitter sizes: left=260, center=stretch, right=310
    // The center pane gets whatever is left after the fixed-width sidebars.
    const int centerWidth = qMax(400, width() - 260 - 310);
    splitter->setSizes({260, centerWidth, 310});

    // ── Status bar (SmartSDR-style, double height) ─────────────────────
    statusBar()->setFixedHeight(40);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setStyleSheet(
        "QStatusBar { background: #0a0a14; border-top: 1px solid #203040; }"
        "QStatusBar::item { border: none; }"
        "QLabel { font-size: 21px; background: transparent; }");

    const QString valStyle  = "QLabel { color: #8aa8c0; font-size: 21px; }";
    const QString sepStyle  = "QLabel { color: #304050; font-size: 21px; }";
    const QString greyInd   = "QLabel { color: #404858; font-weight: bold; font-size: 21px; }";
    const QString greenInd  = "QLabel { color: #00e060; font-weight: bold; font-size: 21px; }";
    const QString redInd    = "QLabel { color: #e04040; font-weight: bold; font-size: 21px; }";
    const QString greyIndLg = "QLabel { color: #404858; font-weight: bold; font-size: 24px; }";
    const QString greenIndLg= "QLabel { color: #00e060; font-weight: bold; font-size: 24px; }";

    // Use a container with HBoxLayout for 3-section layout:
    // [left items] → stretch → [STATION centered] → stretch → [right items]
    auto* container = new QWidget(this);
    auto* hbox = new QHBoxLayout(container);
    hbox->setContentsMargins(6, 0, 6, 0);
    hbox->setSpacing(6);

    auto addSep = [&]() {
        auto* sep = new QLabel(" · ");
        sep->setStyleSheet(sepStyle);
        hbox->addWidget(sep);
    };

    // Hidden connection state label (used by connect/disconnect logic)
    m_connStatusLabel = new QLabel("");
    m_connStatusLabel->hide();

    // ── Left section ─────────────────────────────────────────────────────
    auto* addPanBtn = new QLabel("+PAN");
    addPanBtn->setStyleSheet("QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 24px; }");
    addPanBtn->setCursor(Qt::PointingHandCursor);
    addPanBtn->installEventFilter(this);
    hbox->addWidget(addPanBtn);
    m_addPanLabel = addPanBtn;

    m_tnfIndicator = new QLabel("TNF");
    m_tnfIndicator->setStyleSheet("QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 24px; }");
    m_tnfIndicator->setCursor(Qt::PointingHandCursor);
    m_tnfIndicator->installEventFilter(this);
    hbox->addWidget(m_tnfIndicator);

    m_cwxIndicator = new QLabel("CWX");
    m_cwxIndicator->setStyleSheet(greyIndLg);
    hbox->addWidget(m_cwxIndicator);

    m_dvkIndicator = new QLabel("DVK");
    m_dvkIndicator->setStyleSheet(greyIndLg);
    hbox->addWidget(m_dvkIndicator);

    m_fdxIndicator = new QLabel("FDX");
    m_fdxIndicator->setStyleSheet(greyIndLg);
    m_fdxIndicator->setCursor(Qt::PointingHandCursor);
    m_fdxIndicator->setToolTip("Full Duplex — RX stays active during TX (click to toggle)");
    m_fdxIndicator->installEventFilter(this);
    hbox->addWidget(m_fdxIndicator);

    addSep();

    // Radio model (top) + version (bottom) stacked
    m_radioInfoLabel = new QLabel("");
    auto* radioStack = new QWidget;
    auto* radioVbox = new QVBoxLayout(radioStack);
    radioVbox->setContentsMargins(0, 0, 0, 0);
    radioVbox->setSpacing(0);
    m_radioInfoLabel = new QLabel("");
    m_radioInfoLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_radioInfoLabel->setAlignment(Qt::AlignCenter);
    m_radioVersionLabel = new QLabel("");
    m_radioVersionLabel->setStyleSheet("QLabel { color: #607080; font-size: 12px; }");
    m_radioVersionLabel->setAlignment(Qt::AlignCenter);
    radioVbox->addWidget(m_radioInfoLabel);
    radioVbox->addWidget(m_radioVersionLabel);
    hbox->addWidget(radioStack);

    // ── Center stretch → STATION → stretch ───────────────────────────────
    hbox->addStretch(1);

    auto* stationPrefix = new QLabel("STATION:");
    stationPrefix->setStyleSheet(valStyle);
    hbox->addWidget(stationPrefix);

    m_stationNickLabel = new QLabel("N0CALL");
    m_stationNickLabel->setStyleSheet(
        "QLabel { color: #c8d8e8; font-size: 21px; background: #0a0a14; "
        "border: 1px solid rgba(255,255,255,128); padding: 2px 12px; }");
    m_stationNickLabel->setAlignment(Qt::AlignCenter);
    m_stationNickLabel->setCursor(Qt::PointingHandCursor);
    m_stationNickLabel->setToolTip("Double-click to connect/disconnect");
    m_stationNickLabel->installEventFilter(this);
    hbox->addWidget(m_stationNickLabel);
    m_stationLabel = m_stationNickLabel;  // alias for existing references

    hbox->addStretch(1);

    // ── Right section ────────────────────────────────────────────────────
    // GPS satellites (top) + lock status (bottom) stacked
    auto* gpsStack = new QWidget;
    auto* gpsVbox = new QVBoxLayout(gpsStack);
    gpsVbox->setContentsMargins(0, 0, 0, 0);
    gpsVbox->setSpacing(0);
    m_gpsLabel = new QLabel("");
    m_gpsLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_gpsLabel->setAlignment(Qt::AlignCenter);
    m_gpsStatusLabel = new QLabel("");
    m_gpsStatusLabel->setStyleSheet("QLabel { color: #607080; font-size: 12px; }");
    m_gpsStatusLabel->setAlignment(Qt::AlignCenter);
    gpsVbox->addWidget(m_gpsLabel);
    gpsVbox->addWidget(m_gpsStatusLabel);
    hbox->addWidget(gpsStack);

    addSep();

    // PA temp (top) + supply voltage (bottom) stacked
    auto* paStack = new QWidget;
    auto* paVbox = new QVBoxLayout(paStack);
    paVbox->setContentsMargins(0, 0, 0, 0);
    paVbox->setSpacing(0);
    m_paTempLabel = new QLabel("");
    m_paTempLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_paTempLabel->setAlignment(Qt::AlignCenter);
    m_supplyVoltLabel = new QLabel("");
    m_supplyVoltLabel->setStyleSheet("QLabel { color: #607080; font-size: 12px; }");
    m_supplyVoltLabel->setAlignment(Qt::AlignCenter);
    paVbox->addWidget(m_paTempLabel);
    paVbox->addWidget(m_supplyVoltLabel);
    hbox->addWidget(paStack);

    addSep();

    // Network label (top) + quality (bottom) stacked
    auto* netStack = new QWidget;
    netStack->setCursor(Qt::PointingHandCursor);
    auto* netVbox = new QVBoxLayout(netStack);
    netVbox->setContentsMargins(0, 0, 0, 0);
    netVbox->setSpacing(0);
    auto* netTitle = new QLabel("Network:");
    netTitle->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    netTitle->setAlignment(Qt::AlignCenter);
    netVbox->addWidget(netTitle);
    m_networkLabel = new QLabel("");
    m_networkLabel->setStyleSheet("QLabel { color: #607080; font-size: 12px; }");
    m_networkLabel->setTextFormat(Qt::RichText);
    m_networkLabel->setAlignment(Qt::AlignCenter);
    m_networkLabel->installEventFilter(this);
    netVbox->addWidget(m_networkLabel);
    hbox->addWidget(netStack);

    addSep();

    m_tgxlIndicator = new QLabel("TGXL");
    m_tgxlIndicator->setStyleSheet("QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 21px; }");
    m_tgxlIndicator->setVisible(false);
    hbox->addWidget(m_tgxlIndicator);

    m_pgxlIndicator = new QLabel("PGXL");
    m_pgxlIndicator->setStyleSheet("QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 21px; }");
    m_pgxlIndicator->setVisible(false);
    hbox->addWidget(m_pgxlIndicator);

    addSep();

    m_txIndicator = new QLabel("TX");
    m_txIndicator->setFixedSize(36, 36);
    m_txIndicator->setAlignment(Qt::AlignCenter);
    m_txIndicator->setStyleSheet("QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 21px; }");
    hbox->addWidget(m_txIndicator);

    addSep();

    // Grid square (top) + UTC time (bottom) stacked, right-aligned
    auto* timeStack = new QWidget;
    auto* timeVbox = new QVBoxLayout(timeStack);
    timeVbox->setContentsMargins(0, 0, 0, 0);
    timeVbox->setSpacing(0);
    m_gridLabel = new QLabel("");
    m_gridLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_gridLabel->setAlignment(Qt::AlignRight);
    m_gpsTimeLabel = new QLabel("");
    m_gpsTimeLabel->setStyleSheet("QLabel { color: #607080; font-size: 12px; }");
    m_gpsTimeLabel->setAlignment(Qt::AlignRight);
    timeVbox->addWidget(m_gridLabel);
    timeVbox->addWidget(m_gpsTimeLabel);
    hbox->addWidget(timeStack);

    statusBar()->addWidget(container, 1);
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
        m_radioInfoLabel->setText(m_radioModel.model());
        m_radioVersionLabel->setText(m_radioModel.version());
        m_stationLabel->setText(m_radioModel.nickname());
        m_connStatusLabel->setText("Connected");
        m_connPanel->setStatusText("Connected");
        // Show DIV button on dual-SCU radios
        {
            const QString& model = m_radioModel.model();
            bool divAllowed = model.contains("6600") || model.contains("6700")
                           || model.contains("8600") || model.contains("AU-520");
            // Set diversity allowed on all existing VFO widgets
            if (auto* vfo = spectrum()->vfoWidget())
                vfo->setDiversityAllowed(divAllowed);
        }
        m_audio.startRxStream();
        // TX audio stream will start when the radio assigns a stream ID
        // Auto-hide the connection dialog on successful connect
        m_connPanel->hide();

        // Close reconnect dialog if it was showing
        if (m_reconnectDlg) {
            m_reconnectDlg->close();
            m_reconnectDlg->deleteLater();
            m_reconnectDlg = nullptr;
        }

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

#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
        // Delay DAX bridge start until RadioModel's SmartConnect sequence
        // is fully complete (streams created, UDP bound, slices discovered).
        // Auto-start DAX bridge if enabled in settings.
        // Starting too early causes our mic_selection=PC and dax=1 to be
        // overridden by RadioModel's own setup, and DAX stream IDs won't
        // be registered in PanadapterStream yet.
        if (AppSettings::instance().value("AutoStartDAX", "False").toString() == "True") {
            QTimer::singleShot(3000, this, [this]() {
                startDax();
                if (m_appletPanel && m_appletPanel->catApplet())
                    m_appletPanel->catApplet()->setDaxEnabled(true);
            });
        }
#endif
    } else {
        m_connStatusLabel->setText("Disconnected");
        m_radioInfoLabel->setText("");
        m_radioVersionLabel->setText("");
        m_stationLabel->setText("N0CALL");
        m_tnfIndicator->setStyleSheet("QLabel { color: #404858; font-weight: bold; font-size: 24px; }");
        m_tgxlIndicator->setVisible(false);
        m_pgxlIndicator->setVisible(false);
        m_txIndicator->setStyleSheet("QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 21px; }");
        m_txIndicator->setText("TX");
        m_connPanel->setStatusText("Not connected");
#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
        stopDax();
#endif
        m_audio.stopRxStream();
        m_audio.stopTxStream();

        // Show reconnect dialog on unexpected disconnect (only one at a time)
        if (!m_userDisconnected && !m_reconnectDlg) {
            m_reconnectDlg = new QDialog(this);
            m_reconnectDlg->setWindowTitle("Radio Disconnected");
            m_reconnectDlg->setModal(false);
            m_reconnectDlg->setFixedSize(400, 150);
            m_reconnectDlg->setStyleSheet(
                "QDialog { background: #0f0f1a; border: 2px solid #205070; }"
                "QLabel { color: #c8d8e8; font-size: 14px; }"
                "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
                "border-radius: 3px; color: #c8d8e8; font-size: 12px; "
                "font-weight: bold; padding: 6px 20px; }"
                "QPushButton:hover { background: #204060; }");
            auto* layout = new QVBoxLayout(m_reconnectDlg);
            layout->setAlignment(Qt::AlignCenter);
            auto* label = new QLabel("Radio disconnected\nWaiting for reconnect...");
            label->setAlignment(Qt::AlignCenter);
            layout->addWidget(label);
            auto* dismissBtn = new QPushButton("Disconnect");
            dismissBtn->setFixedWidth(120);
            layout->addWidget(dismissBtn, 0, Qt::AlignCenter);
            connect(dismissBtn, &QPushButton::clicked, this, [this]() {
                m_userDisconnected = true;
                m_reconnectDlg->close();
                m_reconnectDlg->deleteLater();
                m_reconnectDlg = nullptr;
                auto& s = AppSettings::instance();
                s.remove("LastConnectedRadioSerial");
                s.remove("LastRoutedRadioIp");
                s.save();
                m_connPanel->show();
            });
            m_reconnectDlg->show();
        }
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
    // During layout transition, spectrums are being destroyed/recreated — skip
    if (m_applyingLayout) return;

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
            m_radioModel.createAudioStream();
        }

        // Restore saved DAX channel from last session
        int savedDax = AppSettings::instance().value("LastDaxChannel", "0").toInt();
        if (savedDax > 0)
            s->setDaxChannel(savedDax);

        // Restore client-side DSP (NR2/RN2) from last session.
        // Deferred so the VFO widget exists for button sync.
        QTimer::singleShot(500, this, [this]() {
            auto& settings = AppSettings::instance();
            if (settings.value("ClientNr2Enabled", "False").toString() == "True")
                enableNr2WithWisdom();
            else if (settings.value("ClientRn2Enabled", "False").toString() == "True")
                m_audio.setRn2Enabled(true);
        });
    }

    // Re-claim TX assignment after profile load or slice recreation (#145).
    // The radio sets tx=1 on the slice but tx_client_handle may be 0x00000000
    // if the slice was destroyed and recreated (e.g. by profile global load).
    if (s->isTxSlice())
        m_radioModel.sendCommand(QString("slice set %1 tx=1").arg(s->sliceId()));

#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
    // Update m_daxTxMode when TX slice or its mode changes.
    // Digital modes (DIGU/DIGL/RTTY) use DAX bridge; voice modes use mic.
    auto updateDaxTxMode = [this]() {
        bool isDigital = false;
        for (auto* sl : m_radioModel.slices()) {
            if (sl->isTxSlice()) {
                const QString& m = sl->mode();
                isDigital = (m == "DIGU" || m == "DIGL" || m == "RTTY"
                          || m == "DFM"  || m == "NFM");
                break;
            }
        }
        m_audio.setDaxTxMode(isDigital);
    };
    connect(s, &SliceModel::modeChanged, this, updateDaxTxMode);
    connect(s, &SliceModel::txSliceChanged, this, updateDaxTxMode);
    updateDaxTxMode();  // set initial state from current TX slice mode
#endif

    // Push overlay for this slice to the spectrum widget
    pushSliceOverlay(s);

    // Set the panadapter applet's slice label (e.g. "Slice B") based on
    // which pan this slice belongs to
    if (m_panStack && !s->panId().isEmpty()) {
        if (auto* applet = m_panStack->panadapter(s->panId()))
            applet->setSliceId(s->sliceId());
    }

    // Set initial hasTxSlice for waterfall freeze logic
    if (s->isTxSlice())
        spectrumForSlice(s)->setHasTxSlice(true);

    // Sync show-TX-in-waterfall on first slice
    spectrumForSlice(s)->setShowTxInWaterfall(
        m_radioModel.transmitModel()->showTxInWaterfall());

    // Connect slice state changes → spectrum overlay updates
    connect(s, &SliceModel::frequencyChanged, this, [this, s](double mhz) {
        m_updatingFromModel = true;
        spectrumForSlice(s)->setSliceOverlay(s->sliceId(), mhz,
            s->filterLow(), s->filterHigh(), s->isTxSlice(),
            s->sliceId() == m_activeSliceId,
            s->mode(), s->rttyMark(), s->rttyShift(),
            s->ritOn(), s->ritFreq(), s->xitOn(), s->xitFreq());
        m_updatingFromModel = false;

        // Feed frequency to Antenna Genius for band→antenna recall
        if (s->sliceId() == m_activeSliceId)
            m_antennaGenius.setRadioFrequency(mhz);
    });

    // Feed current frequency immediately (AG may connect later and reprocess).
    if (s->sliceId() == m_activeSliceId && s->frequency() > 0.0)
        m_antennaGenius.setRadioFrequency(s->frequency());

    connect(s, &SliceModel::filterChanged, this, [this, s](int lo, int hi) {
        spectrumForSlice(s)->setSliceOverlay(s->sliceId(), s->frequency(),
            lo, hi, s->isTxSlice(), s->sliceId() == m_activeSliceId,
            s->mode(), s->rttyMark(), s->rttyShift(),
            s->ritOn(), s->ritFreq(), s->xitOn(), s->xitFreq());
    });
    connect(s, &SliceModel::txSliceChanged, this, [this, s](bool tx) {
        // Update hasTxSlice on all spectrums for waterfall freeze logic
        if (tx) {
            for (auto* pan : m_radioModel.panadapters()) {
                if (auto* sw = m_panStack ? m_panStack->spectrum(pan->panId()) : nullptr)
                    sw->setHasTxSlice(sw == spectrumForSlice(s));
            }
            if (!m_panStack && m_panApplet)
                m_panApplet->spectrumWidget()->setHasTxSlice(true);
        }
        spectrumForSlice(s)->setSliceOverlay(s->sliceId(), s->frequency(),
            s->filterLow(), s->filterHigh(), tx,
            s->sliceId() == m_activeSliceId,
            s->mode(), s->rttyMark(), s->rttyShift(),
            s->ritOn(), s->ritFreq(), s->xitOn(), s->xitFreq());
        updateSplitState();
    });

    // When the radio notifies us that this slice became active, switch to it
    connect(s, &SliceModel::activeChanged, this, [this, s](bool active) {
        if (!active) return;
        // In multi-pan mode, ignore radio-side active changes — we manage
        // active slice client-side. The radio echoes active=1 on every
        // "slice m" command, which would trigger setActiveSlice and cause
        // waterfall pauses via setActiveSlice overhead.
        if (m_panStack && m_panStack->count() > 1) return;
        setActiveSlice(s->sliceId());
    });

    // Update filter limits when the active slice's mode changes
    connect(s, &SliceModel::modeChanged, this, [this, s](const QString& mode) {
        if (s->sliceId() == m_activeSliceId)
            updateFilterLimitsForMode(mode);

        // Update spectrum overlay with new mode (for RTTY mark/space lines)
        pushSliceOverlay(s);

        // Show/hide CW decode panel and start/stop decoder
        if (s->sliceId() == m_activeSliceId) {
            bool isCw = (mode == "CW" || mode == "CWL");
            bool decodeOn = AppSettings::instance().value("CwDecodeOverlay", "True").toString() == "True";
            m_panApplet->setCwPanelVisible(isCw && decodeOn);
            if (isCw && !m_cwDecoder.isRunning())
                m_cwDecoder.start();
            else if (!isCw && m_cwDecoder.isRunning())
                m_cwDecoder.stop();
        }
    });

    // Update RTTY mark/space lines on spectrum when mark/shift changes
    connect(s, &SliceModel::rttyMarkChanged, this, [this, s](int) { pushSliceOverlay(s); });
    connect(s, &SliceModel::rttyShiftChanged, this, [this, s](int) { pushSliceOverlay(s); });
    connect(s, &SliceModel::ritChanged, this, [this, s](bool, int) { pushSliceOverlay(s); });
    connect(s, &SliceModel::xitChanged, this, [this, s](bool, int) { pushSliceOverlay(s); });

    // Handle slice migration between panadapters
    connect(s, &SliceModel::panIdChanged, this, [this, s](const QString&) {
        // Remove overlay/VFO from all spectrums
        if (m_panStack) {
            for (auto* pan : m_radioModel.panadapters()) {
                if (auto* sw = m_panStack->spectrum(pan->panId())) {
                    sw->removeSliceOverlay(s->sliceId());
                    sw->removeVfoWidget(s->sliceId());
                }
            }
        }
        // Re-add on the new pan
        auto* sw = spectrumForSlice(s);
        sw->addVfoWidget(s->sliceId());
        pushSliceOverlay(s);
    });

    // Create a VfoWidget for this slice on the correct panadapter
    auto* vfo = spectrumForSlice(s)->addVfoWidget(s->sliceId());

    wireVfoWidget(vfo, s);

    // NR2/RN2/RADE are now wired permanently in wireVfoWidget — no
    // special handling needed here for active slice timing.

    // Show DIV button on dual-SCU radios
    {
        const QString& model = m_radioModel.model();
        bool divAllowed = model.contains("6600") || model.contains("6700")
                       || model.contains("8600") || model.contains("AU-520");
        vfo->setDiversityAllowed(divAllowed);
    }

    // Feed S-meter per-slice — only this VFO's slice level
    const int sid = s->sliceId();
    connect(m_radioModel.meterModel(), &MeterModel::sLevelChanged,
            vfo, [vfo, sid](int sliceIndex, float dbm) {
        if (sliceIndex == sid)
            vfo->setSignalLevel(dbm);
    });
    connect(&m_radioModel, &RadioModel::antListChanged,
            vfo, &VfoWidget::setAntennaList);

    // Direct freq label update for runtime changes
    connect(s, &SliceModel::frequencyChanged, this, [this, s]() {
        auto* sw2 = spectrumForSlice(s);
        if (!sw2) return;
        auto* v = sw2->vfoWidget(s->sliceId());
        if (!v) return;
        long long hz = static_cast<long long>(std::round(s->frequency() * 1e6));
        int mhzPart = static_cast<int>(hz / 1000000);
        int khzPart = static_cast<int>((hz / 1000) % 1000);
        int hzPart  = static_cast<int>(hz % 1000);
        v->freqLabel()->setText(QString("%1.%2.%3")
            .arg(mhzPart)
            .arg(khzPart, 3, 10, QChar('0'))
            .arg(hzPart, 3, 10, QChar('0')));

        // Diversity: client-side sync — immediately update child VFO display
        // to avoid rubber-banding from the radio round-trip delay.
        // Only for diversity parent→child, NOT split RX→TX.
        if (s->isDiversityParent()) {
            for (auto* other : m_radioModel.slices()) {
                if (other->isDiversityChild() && other->sliceId() != s->sliceId()) {
                    auto* csw = spectrumForSlice(other);
                    if (!csw) continue;
                    auto* cv = csw->vfoWidget(other->sliceId());
                    if (!cv) continue;
                    cv->freqLabel()->setText(QString("%1.%2.%3")
                        .arg(mhzPart)
                        .arg(khzPart, 3, 10, QChar('0'))
                        .arg(hzPart, 3, 10, QChar('0')));
                }
            }
        }
    });

    // If split is pending, this new slice is the TX slice
    if (m_splitActive && m_splitTxSliceId < 0 && s->sliceId() != m_splitRxSliceId) {
        m_splitTxSliceId = s->sliceId();
        s->setTxSlice(true);
        s->setAudioMute(true);  // TX slice in split has no audio output
        // Tune TX slice to match RX slice frequency
        if (auto* rxSlice = m_radioModel.slice(m_splitRxSliceId))
            s->setFrequency(rxSlice->frequency());
        spectrum()->setSplitPair(m_splitRxSliceId, m_splitTxSliceId);
        updateSplitState();
        // Auto-focus the TX VFO so the user can immediately tune the TX offset
        setActiveSlice(s->sliceId());
    }
}

void MainWindow::onSliceRemoved(int id)
{
    if (m_applyingLayout) return;

    qDebug() << "MainWindow: slice removed" << id;

    // If the split TX slice was closed, disable split
    if (m_splitActive && id == m_splitTxSliceId) {
        m_splitActive = false;
        m_splitRxSliceId = -1;
        m_splitTxSliceId = -1;
        spectrum()->setSplitPair(-1, -1);
        if (auto* s = activeSlice())
            s->setTxSlice(true);
        updateSplitState();
    }

    // Remove overlay from all panadapter spectrums (slice model is already gone,
    // so we can't look up which pan it was on)
    if (m_panStack) {
        for (auto* pan : m_radioModel.panadapters()) {
            if (auto* sw = m_panStack->spectrum(pan->panId())) {
                sw->removeSliceOverlay(id);
                sw->removeVfoWidget(id);
            }
        }
    }
    // Clean slice overlay and VFO widget from all spectrums
    for (auto* a : m_panStack->allApplets()) {
        a->spectrumWidget()->removeSliceOverlay(id);
        a->spectrumWidget()->removeVfoWidget(id);
    }

    // Reset panadapter state so display settings re-sync after profile load
    m_radioModel.resetPanState();
    m_needAudioStream = true;

    // If the removed slice was active, switch to the first remaining slice
    if (id == m_activeSliceId) {
        m_appletPanel->setSlice(nullptr);
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

    // Auto-activate the panadapter that owns this slice
    if (m_panStack && !s->panId().isEmpty())
        m_panStack->setActivePan(s->panId());

    const int prevId = m_activeSliceId;
    m_activeSliceId = sliceId;

    // In multi-pan mode, don't tell the radio about active slice changes —
    // it causes the outgoing pan's waterfall to pause for ~3-5 seconds.
    // The radio's "active" flag mainly affects CW keying target.
    // In single-pan mode, always notify the radio.
    if (sliceId != prevId && (!m_panStack || m_panStack->count() <= 1))
        s->setActive(true);

    // Update all overlay isActive flags on each slice's correct spectrum
    for (auto* sl : m_radioModel.slices()) {
        const bool isActive = (sl->sliceId() == sliceId);
        spectrumForSlice(sl)->setSliceOverlay(sl->sliceId(), sl->frequency(),
            sl->filterLow(), sl->filterHigh(), sl->isTxSlice(), isActive,
            sl->mode(), sl->rttyMark(), sl->rttyShift(),
            sl->ritOn(), sl->ritFreq(), sl->xitOn(), sl->xitFreq());
    }

    // Re-wire applet panel, overlay menu to the new active slice
    if (m_panStack) {
        if (auto* applet = m_panStack->panadapter(s->panId()))
            applet->setSliceId(sliceId);
        else if (m_panStack->activeApplet())
            m_panStack->activeApplet()->setSliceId(sliceId);
    }
    m_appletPanel->setSlice(s);
    spectrum()->overlayMenu()->setSlice(s);

    // Sync step size from the new active slice
    if (s->stepHz() > 0) {
        if (spectrum()) spectrum()->setStepSize(s->stepHz());
        m_appletPanel->rxApplet()->syncStepFromSlice(s->stepHz(), s->stepList());
    }

    // Switch active VFO widget display (NR2/RN2/RADE are wired permanently
    // in wireVfoWidget, no disconnect/reconnect needed)
    spectrum()->setActiveVfoWidget(sliceId);

    // Update filter limits for the active slice's mode
    updateFilterLimitsForMode(s->mode());

    // Show/hide CW decode panel for the active slice's current mode
    bool isCw = (s->mode() == "CW" || s->mode() == "CWL");
    bool decodeOn = AppSettings::instance().value("CwDecodeOverlay", "True").toString() == "True";
    m_panApplet->setCwPanelVisible(isCw && decodeOn);
    if (isCw && !m_cwDecoder.isRunning())
        m_cwDecoder.start();
    else if (!isCw && m_cwDecoder.isRunning())
        m_cwDecoder.stop();

    // Detect band from frequency
    if (m_bandSettings.currentBand().isEmpty())
        m_bandSettings.setCurrentBand(BandSettings::bandForFrequency(s->frequency()));


#ifdef HAVE_RADE
    // Switch RADE audio mode based on whether the active slice is the RADE slice.
    // When on the RADE slice: mic → RADEEngine, speaker blocked (muted at slice level).
    // When on a non-RADE slice: mic → normal VITA-49, speaker plays normally.
    if (m_radeSliceId >= 0 && m_radeEngine && m_radeEngine->isActive()) {
        m_audio.setRadeMode(sliceId == m_radeSliceId);
    }
#endif

    updateSplitState();

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
    if (m_applyingLayout) return;
    auto* sw = spectrumForSlice(s);
    if (!sw) return;
    sw->setSliceOverlay(s->sliceId(), s->frequency(),
        s->filterLow(), s->filterHigh(), s->isTxSlice(),
        s->sliceId() == m_activeSliceId,
        s->mode(), s->rttyMark(), s->rttyShift(),
        s->ritOn(), s->ritFreq(), s->xitOn(), s->xitFreq());
}

void MainWindow::disableSplit()
{
    if (!m_splitActive) return;

    m_splitActive = false;

    // Move TX back to the RX slice
    if (auto* rxSlice = m_radioModel.slice(m_splitRxSliceId))
        rxSlice->setTxSlice(true);

    // Destroy the split TX slice
    if (m_splitTxSliceId >= 0)
        m_radioModel.sendCommand(QString("slice remove %1").arg(m_splitTxSliceId));

    m_splitRxSliceId = -1;
    m_splitTxSliceId = -1;
    spectrum()->setSplitPair(-1, -1);

    updateSplitState();
}

void MainWindow::updateSplitState()
{
    for (auto* s : m_radioModel.slices()) {
        if (auto* w = spectrum()->vfoWidget(s->sliceId())) {
            bool isTxSlice = (m_splitActive && s->sliceId() == m_splitTxSliceId);
            bool isRxSplit = (m_splitActive && s->sliceId() == m_splitRxSliceId);
            w->updateSplitBadge(isTxSlice, isRxSplit);
        }
    }
}

// ── Per-panadapter signal wiring ──────────────────────────────────────────────
// Called once per PanadapterApplet. Connects the SpectrumWidget and its
// OverlayMenu signals to RadioModel, TnfModel, and MainWindow handlers.
// In multi-pan mode (Phase 6+), called for each new panadapter.

void MainWindow::wirePanadapter(PanadapterApplet* applet)
{
    auto* sw = applet->spectrumWidget();
    auto* menu = sw->overlayMenu();

    // ── Pan activation: clicking on this pan makes it active ─────────────
    connect(applet, &PanadapterApplet::activated,
            m_panStack, &PanadapterStack::setActivePan);

    // ── User drag actions from spectrum → radio (per-pan) ──────────────────
    connect(sw, &SpectrumWidget::bandwidthChangeRequested,
            this, [this, applet](double bw) {
        m_radioModel.sendCommand(
            QString("display pan set %1 bandwidth=%2").arg(applet->panId()).arg(bw, 0, 'f', 6));
    });
    connect(sw, &SpectrumWidget::centerChangeRequested,
            this, [this, applet](double center) {
        m_radioModel.sendCommand(
            QString("display pan set %1 center=%2").arg(applet->panId()).arg(center, 0, 'f', 6));
    });
    connect(sw, &SpectrumWidget::filterChangeRequested,
            this, [this](int lo, int hi) {
        if (auto* s = activeSlice()) s->setFilterWidth(lo, hi);
    });
    connect(sw, &SpectrumWidget::dbmRangeChangeRequested,
            this, [this, applet](float minDbm, float maxDbm) {
        m_radioModel.sendCommand(
            QString("display pan set %1 min_dbm=%2 max_dbm=%3")
                .arg(applet->panId())
                .arg(static_cast<double>(minDbm), 0, 'f', 2)
                .arg(static_cast<double>(maxDbm), 0, 'f', 2));
    });

    // ── TNF signals ──────────────────────────────────────────────────────
    auto* tnf = m_radioModel.tnfModel();
    auto rebuildTnfMarkers = [this, sw]() {
        auto* t = m_radioModel.tnfModel();
        QVector<SpectrumWidget::TnfMarker> markers;
        for (const auto& e : t->tnfs())
            markers.append({e.id, e.freqMhz, e.widthHz, e.depthDb, e.permanent});
        sw->setTnfMarkers(markers);
    };
    connect(tnf, &TnfModel::tnfChanged,  this, rebuildTnfMarkers);
    connect(tnf, &TnfModel::tnfRemoved,  this, rebuildTnfMarkers);
    connect(tnf, &TnfModel::globalEnabledChanged,
            sw, &SpectrumWidget::setTnfGlobalEnabled);
    connect(tnf, &TnfModel::globalEnabledChanged,
            this, [this](bool on) {
        m_tnfIndicator->setStyleSheet(on
            ? "QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 24px; }"
            : "QLabel { color: #404858; font-weight: bold; font-size: 24px; }");
    });

    // FDX indicator style update
    connect(&m_radioModel, &RadioModel::infoChanged, this, [this]() {
        bool fdx = m_radioModel.fullDuplexEnabled();
        m_fdxIndicator->setStyleSheet(fdx
            ? "QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 24px; }"
            : "QLabel { color: #404858; font-weight: bold; font-size: 24px; }");
    });
    connect(sw, &SpectrumWidget::tnfCreateRequested,   tnf, &TnfModel::createTnf);
    connect(sw, &SpectrumWidget::tnfMoveRequested,     tnf, &TnfModel::setTnfFreq);
    connect(sw, &SpectrumWidget::tnfRemoveRequested,   tnf, &TnfModel::requestRemoveTnf);
    connect(sw, &SpectrumWidget::tnfWidthRequested,    tnf, &TnfModel::setTnfWidth);
    connect(sw, &SpectrumWidget::tnfDepthRequested,    tnf, &TnfModel::setTnfDepth);
    connect(sw, &SpectrumWidget::tnfPermanentRequested,tnf, &TnfModel::setTnfPermanent);

    // ── Spot markers ─────────────────────────────────────────────────────
    auto* spots = m_radioModel.spotModel();
    QPointer<SpectrumWidget> swGuard(sw);
    auto rebuildSpots = [this, swGuard]() {
        if (!swGuard) return;  // widget destroyed (layout change)
        auto* s = m_radioModel.spotModel();
        QVector<SpectrumWidget::SpotMarker> markers;
        for (const auto& spot : s->spots())
            markers.append({spot.index, spot.callsign, spot.rxFreqMhz, spot.color, spot.mode});
        swGuard->setSpotMarkers(markers);
    };
    connect(spots, &SpotModel::spotAdded,   this, rebuildSpots);
    connect(spots, &SpotModel::spotUpdated, this, rebuildSpots);
    connect(spots, &SpotModel::spotRemoved, this, rebuildSpots);
    connect(spots, &SpotModel::spotsCleared,this, rebuildSpots);
    {
        auto& s = AppSettings::instance();
        sw->setShowSpots(s.value("IsSpotsEnabled", "True").toString() == "True");
        sw->setSpotFontSize(s.value("SpotFontSize", "16").toInt());
        sw->setSpotMaxLevels(s.value("SpotsMaxLevel", "3").toInt());
        sw->setSpotStartPct(s.value("SpotsStartingHeightPercentage", "50").toInt());
        sw->setSpotOverrideColors(s.value("IsSpotsOverrideColorsEnabled", "False").toString() == "True");
    }

    // ── Per-pan display controls (client-side) ───────────────────────────
    connect(menu, &SpectrumOverlayMenu::fftFillAlphaChanged,
            sw, &SpectrumWidget::setFftFillAlpha);
    connect(menu, &SpectrumOverlayMenu::fftFillColorChanged,
            sw, &SpectrumWidget::setFftFillColor);
    connect(menu, &SpectrumOverlayMenu::noiseFloorPositionChanged,
            sw, &SpectrumWidget::setNoiseFloorPosition);
    connect(menu, &SpectrumOverlayMenu::noiseFloorEnableChanged,
            sw, &SpectrumWidget::setNoiseFloorEnable);

    // ── Per-pan display controls → radio commands ────────────────────────
    // Each pan's overlay sends commands with its own panId/wfId, not the
    // global active pan. This ensures display settings work independently.
    connect(menu, &SpectrumOverlayMenu::fftAverageChanged,
            this, [this, applet, sw](int v) {
        sw->setFftAverage(v);
        m_radioModel.sendCommand(
            QString("display pan set %1 average=%2").arg(applet->panId()).arg(v));
    });
    connect(menu, &SpectrumOverlayMenu::fftFpsChanged,
            this, [this, applet, sw](int v) {
        sw->setFftFps(v);
        m_radioModel.sendCommand(
            QString("display pan set %1 fps=%2").arg(applet->panId()).arg(v));
    });
    connect(menu, &SpectrumOverlayMenu::fftWeightedAverageChanged,
            this, [this, applet, sw](bool on) {
        sw->setFftWeightedAvg(on);
        m_radioModel.sendCommand(
            QString("display pan set %1 weighted_average=%2").arg(applet->panId()).arg(on ? 1 : 0));
    });
    connect(menu, &SpectrumOverlayMenu::wfColorGainChanged,
            this, [this, applet, sw](int v) {
        sw->setWfColorGain(v);
        auto* pan = m_radioModel.panadapter(applet->panId());
        if (pan && !pan->waterfallId().isEmpty())
            m_radioModel.sendCommand(
                QString("display panafall set %1 color_gain=%2").arg(pan->waterfallId()).arg(v));
    });
    connect(menu, &SpectrumOverlayMenu::wfBlackLevelChanged,
            this, [this, applet, sw](int v) {
        sw->setWfBlackLevel(v);
        auto* pan = m_radioModel.panadapter(applet->panId());
        if (pan && !pan->waterfallId().isEmpty())
            m_radioModel.sendCommand(
                QString("display panafall set %1 black_level=%2").arg(pan->waterfallId()).arg(v));
    });
    connect(menu, &SpectrumOverlayMenu::wfAutoBlackChanged,
            this, [this, sw](bool on) {
        sw->setWfAutoBlack(on);
    });
    connect(menu, &SpectrumOverlayMenu::wfLineDurationChanged,
            this, [this, applet, sw](int ms) {
        sw->setWfLineDuration(ms);
        auto* pan = m_radioModel.panadapter(applet->panId());
        if (pan && !pan->waterfallId().isEmpty())
            m_radioModel.sendCommand(
                QString("display panafall set %1 line_duration=%2").arg(pan->waterfallId()).arg(ms));
    });

    // ── Click-to-tune ────────────────────────────────────────────────────
    // Uses "slice m <freq> pan=<panId>" (matches SmartSDR protocol).
    // The radio routes the tune to the correct slice for that pan.
    connect(sw, &SpectrumWidget::frequencyClicked,
            this, [this, sw](double mhz) {
        // Find the panId for this spectrum widget
        QString panId;
        for (auto* applet : m_panStack->allApplets()) {
            if (applet->spectrumWidget() == sw) {
                panId = applet->panId();
                break;
            }
        }

        if (!panId.isEmpty()) {
            // Check if we need to switch pans
            bool switchedPan = false;
            for (auto* s : m_radioModel.slices()) {
                if (s->panId() == panId && s->sliceId() != m_activeSliceId) {
                    setActiveSlice(s->sliceId());
                    switchedPan = true;
                    break;
                }
            }
            if (switchedPan)
                m_panStack->setActivePan(panId);

            // Use slice m with pan= for cross-pan tuning,
            // standard onFrequencyChanged for same-pan (updates model immediately)
            if (switchedPan) {
                if (auto* s = activeSlice(); s && !s->isLocked()) {
                    m_radioModel.sendCommand(
                        QString("slice m %1 pan=%2").arg(mhz, 0, 'f', 6).arg(panId));
                    sw->setVfoFrequency(mhz);
                }
            } else {
                onFrequencyChanged(mhz);
            }
        } else {
            onFrequencyChanged(mhz);
        }
    });

    // ── +RX / +TNF buttons ───────────────────────────────────────────────
    connect(menu, &SpectrumOverlayMenu::addRxClicked,
            this, [this]() {
        if (m_radioModel.slices().size() < m_radioModel.maxSlices())
            m_radioModel.addSlice();
    });
    connect(menu, &SpectrumOverlayMenu::addTnfClicked,
            this, [this]() {
        auto* s = activeSlice();
        if (!s) return;
        double tnfFreq = s->frequency()
            + (s->filterLow() + s->filterHigh()) / 2.0 / 1.0e6;
        m_radioModel.tnfModel()->createTnf(tnfFreq);
    });

    // ── Slice marker clicks ──────────────────────────────────────────────
    connect(sw, &SpectrumWidget::sliceClicked,
            this, &MainWindow::setActiveSlice);
    connect(sw, &SpectrumWidget::sliceTxRequested,
            this, [this](int sliceId) {
        if (auto* s = m_radioModel.slice(sliceId))
            s->setTxSlice(true);
    });
    connect(sw, &SpectrumWidget::sliceCloseRequested,
            this, [this](int sliceId) {
        if (m_radioModel.slices().size() <= 1) return;
        m_radioModel.sendCommand(QString("slice remove %1").arg(sliceId));
    });

    // ── Band selection ───────────────────────────────────────────────────
    connect(menu, &SpectrumOverlayMenu::bandSelected,
            this, [this, applet](const QString& bandName, double freqMhz, const QString& mode) {
        // Guard against double-fire from multiple pan overlays
        if (bandName == m_bandSettings.currentBand()) return;

        qDebug() << "MainWindow: switching to band" << bandName
                 << "freq:" << freqMhz << "mode:" << mode;

        auto& settings = AppSettings::instance();

        // Find the slice that belongs to THIS pan, not the global active slice
        SliceModel* s = nullptr;
        for (auto* sl : m_radioModel.slices()) {
            if (sl->panId() == applet->panId()) { s = sl; break; }
        }
        if (!s) s = activeSlice();  // fallback
        qDebug() << "BandStack: panId=" << applet->panId()
                 << "sliceId=" << (s ? s->sliceId() : -1)
                 << "panCount=" << (m_panStack ? m_panStack->count() : 0);

        // ── Save current band state before switching ──────────────────
        if (s) {
            const QString curBand = m_bandSettings.currentBand();
            if (!curBand.isEmpty()) {
                const QString pfx = "BandStack_" + curBand + "_";
                // Only save if the slice's frequency belongs to this band
                // (prevents cross-band contamination in multi-pan mode)
                const QString actualBand = BandSettings::bandForFrequency(s->frequency());
                const bool freqMatchesBand = (actualBand == curBand
                                              || curBand == "GEN" || curBand == "WWV");
                if (!freqMatchesBand) {
                    qDebug() << "BandStack: skipping save — freq" << s->frequency()
                             << "belongs to" << actualBand << "not" << curBand;
                } else {
                settings.setValue(pfx + "Freq",     QString::number(s->frequency(), 'f', 6));
                settings.setValue(pfx + "Mode",     s->mode());
                settings.setValue(pfx + "FilterLo", QString::number(s->filterLow()));
                settings.setValue(pfx + "FilterHi", QString::number(s->filterHigh()));
                // Step: save from SpectrumWidget (client truth — reflects user's
                // last manual step change, not the radio's mode-change default)
                settings.setValue(pfx + "Step",     QString::number(
                    spectrum() ? spectrum()->stepSize() : s->stepHz()));
                // DSP flags — save each independently
                settings.setValue(pfx + "NR",   s->nrOn()   ? "True" : "False");
                settings.setValue(pfx + "NB",   s->nbOn()   ? "True" : "False");
                settings.setValue(pfx + "ANF",  s->anfOn()  ? "True" : "False");
                settings.setValue(pfx + "NRL",  s->nrlOn()  ? "True" : "False");
                settings.setValue(pfx + "NRS",  s->nrsOn()  ? "True" : "False");
                settings.setValue(pfx + "RNN",  s->rnnOn()  ? "True" : "False");
                settings.setValue(pfx + "NRF",  s->nrfOn()  ? "True" : "False");
                settings.setValue(pfx + "ANFL", s->anflOn() ? "True" : "False");
                settings.setValue(pfx + "ANFT", s->anftOn() ? "True" : "False");
                settings.setValue(pfx + "APF",  s->apfOn()  ? "True" : "False");
                // Client-side DSP
                settings.setValue(pfx + "NR2",  m_audio.nr2Enabled() ? "True" : "False");
                settings.setValue(pfx + "RN2",  m_audio.rn2Enabled() ? "True" : "False");
                // AGC
                settings.setValue(pfx + "AgcMode",      s->agcMode());
                settings.setValue(pfx + "AgcThreshold",  QString::number(s->agcThreshold()));
                // Antennas
                settings.setValue(pfx + "RxAnt", s->rxAntenna());
                settings.setValue(pfx + "TxAnt", s->txAntenna());
                // Squelch
                settings.setValue(pfx + "SqlOn",    s->squelchOn() ? "True" : "False");
                settings.setValue(pfx + "SqlLevel", QString::number(s->squelchLevel()));
                // Pan display: bandwidth and dBm scale
                // Read from SpectrumWidget (client-side truth) not PanadapterModel
                if (auto* sw = spectrum()) {
                    float top = sw->refLevel();
                    float bot = top - sw->dynamicRange();
                    settings.setValue(pfx + "MinDbm",    QString::number(bot, 'f', 1));
                    settings.setValue(pfx + "MaxDbm",    QString::number(top, 'f', 1));
                }
                if (auto* pan = m_radioModel.activePanadapter()) {
                    settings.setValue(pfx + "Bandwidth", QString::number(pan->bandwidthMhz(), 'f', 6));
                }
                settings.save();
                qDebug() << "BandStack: saved" << curBand << s->frequency() << s->mode()
                         << "bw:" << settings.value(pfx + "Bandwidth", "?").toString()
                         << "dBm:" << settings.value(pfx + "MinDbm", "?").toString()
                         << settings.value(pfx + "MaxDbm", "?").toString();
                } // end if (freqMatchesBand)
            }
        }

        // ── Switch band tracking ──────────────────────────────────────
        m_bandSettings.setCurrentBand(bandName);

        // ── Recall saved state or use defaults ────────────────────────
        const QString pfx = "BandStack_" + bandName + "_";
        const QString savedFreq = settings.value(pfx + "Freq", "").toString();

        if (!savedFreq.isEmpty() && s) {
            // Recall saved band state
            double recallFreq = savedFreq.toDouble();
            QString recallMode = settings.value(pfx + "Mode", mode).toString();
            int step     = settings.value(pfx + "Step", "100").toInt();

            s->setMode(recallMode);
            // Tune this pan's slice and recenter the pan on the new frequency
            if (m_panStack && m_panStack->count() > 1 && !applet->panId().isEmpty()
                && applet->panId() != "default") {
                m_radioModel.sendCommand(
                    QString("slice m %1 pan=%2").arg(recallFreq, 0, 'f', 6).arg(applet->panId()));
                // Defer pan recenter — radio needs time to process band change
                const QString panId = applet->panId();
                QTimer::singleShot(200, this, [this, panId, recallFreq]() {
                    m_radioModel.sendCommand(
                        QString("display pan set %1 center=%2").arg(panId).arg(recallFreq, 0, 'f', 6));
                });
            } else {
                onFrequencyChanged(recallFreq);
            }
            // Filter offsets: let the radio apply the correct default for
            // the recalled mode. Recalling saved filter widths across mode
            // changes produces wrong results (e.g. 3kHz USB filter on CW).
            // Send step to radio after a short delay (mode change resets step,
            // so we wait for the mode echo before sending our step)
            if (step > 0) {
                QTimer::singleShot(300, this, [this, s, step]() {
                    m_radioModel.sendCommand(
                        QString("slice set %1 step=%2").arg(s->sliceId()).arg(step));
                    if (spectrum()) spectrum()->setStepSize(step);
                    m_appletPanel->rxApplet()->syncStepFromSlice(step, s->stepList());
                });
            }

            // Radio-side DSP flags
            auto setDsp = [&](const QString& key, const QString& cmd, bool cur) {
                bool saved = settings.value(pfx + key, cur ? "True" : "False").toString() == "True";
                if (saved != cur)
                    m_radioModel.sendCommand(
                        QString("slice set %1 %2=%3").arg(s->sliceId()).arg(cmd).arg(saved ? 1 : 0));
            };
            setDsp("NR",   "nr",   s->nrOn());
            setDsp("NB",   "nb",   s->nbOn());
            setDsp("ANF",  "anf",  s->anfOn());
            setDsp("NRL",  "nrl",  s->nrlOn());
            setDsp("NRS",  "nrs",  s->nrsOn());
            setDsp("RNN",  "rnnoise", s->rnnOn());
            setDsp("NRF",  "nrf",  s->nrfOn());
            setDsp("ANFL", "anfl", s->anflOn());
            setDsp("ANFT", "anft", s->anftOn());
            setDsp("APF",  "apf",  s->apfOn());

            // Client-side DSP
            bool nr2Saved = settings.value(pfx + "NR2", "False").toString() == "True";
            bool rn2Saved = settings.value(pfx + "RN2", "False").toString() == "True";
            if (nr2Saved != m_audio.nr2Enabled()) m_audio.setNr2Enabled(nr2Saved);
            if (rn2Saved != m_audio.rn2Enabled()) m_audio.setRn2Enabled(rn2Saved);

            // AGC
            QString agcMode = settings.value(pfx + "AgcMode", "").toString();
            int agcThresh   = settings.value(pfx + "AgcThreshold", "-999").toInt();
            if (!agcMode.isEmpty() && agcMode != s->agcMode())
                m_radioModel.sendCommand(
                    QString("slice set %1 agc_mode=%2").arg(s->sliceId()).arg(agcMode));
            if (agcThresh != -999 && agcThresh != s->agcThreshold())
                m_radioModel.sendCommand(
                    QString("slice set %1 agc_threshold=%2").arg(s->sliceId()).arg(agcThresh));

            // Antennas
            QString rxAnt = settings.value(pfx + "RxAnt", "").toString();
            QString txAnt = settings.value(pfx + "TxAnt", "").toString();
            if (!rxAnt.isEmpty() && rxAnt != s->rxAntenna())
                m_radioModel.sendCommand(
                    QString("slice set %1 rxant=%2").arg(s->sliceId()).arg(rxAnt));
            if (!txAnt.isEmpty() && txAnt != s->txAntenna())
                m_radioModel.sendCommand(
                    QString("slice set %1 txant=%2").arg(s->sliceId()).arg(txAnt));

            // Squelch
            bool sqlOn  = settings.value(pfx + "SqlOn", "False").toString() == "True";
            int sqlLvl  = settings.value(pfx + "SqlLevel", "20").toInt();
            if (sqlOn != s->squelchOn())
                m_radioModel.sendCommand(
                    QString("slice set %1 squelch=%2").arg(s->sliceId()).arg(sqlOn ? 1 : 0));
            if (sqlLvl != s->squelchLevel())
                m_radioModel.sendCommand(
                    QString("slice set %1 squelch_level=%2").arg(s->sliceId()).arg(sqlLvl));

            // Pan display: bandwidth and dBm scale
            double bw = settings.value(pfx + "Bandwidth", "0").toDouble();
            float minDbm = settings.value(pfx + "MinDbm", "0").toFloat();
            float maxDbm = settings.value(pfx + "MaxDbm", "0").toFloat();
            if (auto* pan = m_radioModel.activePanadapter()) {
                if (bw > 0.0)
                    m_radioModel.sendCommand(
                        QString("display pan set %1 bandwidth=%2")
                            .arg(pan->panId()).arg(bw, 0, 'f', 6));
                if (minDbm != 0.0f && maxDbm != 0.0f)
                    m_radioModel.sendCommand(
                        QString("display pan set %1 min_dbm=%2 max_dbm=%3")
                            .arg(pan->panId())
                            .arg(minDbm, 0, 'f', 1)
                            .arg(maxDbm, 0, 'f', 1));
            }

            qDebug() << "BandStack: recalled" << bandName << recallFreq << recallMode;
        } else {
            // First time on this band — use static defaults
            if (s) s->setMode(mode);
            if (m_panStack && m_panStack->count() > 1 && !applet->panId().isEmpty()
                && applet->panId() != "default") {
                m_radioModel.sendCommand(
                    QString("slice m %1 pan=%2").arg(freqMhz, 0, 'f', 6).arg(applet->panId()));
                const QString panId2 = applet->panId();
                QTimer::singleShot(200, this, [this, panId2, freqMhz]() {
                    m_radioModel.sendCommand(
                        QString("display pan set %1 center=%2").arg(panId2).arg(freqMhz, 0, 'f', 6));
                });
            } else {
                onFrequencyChanged(freqMhz);
            }
            qDebug() << "BandStack: first visit to" << bandName << "using defaults";
        }
    });

    // ── WNB / RF Gain ────────────────────────────────────────────────────
    connect(menu, &SpectrumOverlayMenu::wnbToggled,
            this, [this, sw](bool on) {
        m_radioModel.setPanWnb(on);
        sw->setWnbActive(on);
        auto& s = AppSettings::instance();
        s.setValue(sw->settingsKey("DisplayWnbEnabled"), on ? "True" : "False");
        s.save();
    });
    connect(menu, &SpectrumOverlayMenu::wnbLevelChanged,
            this, [this, sw](int level) {
        m_radioModel.setPanWnbLevel(level);
        auto& s = AppSettings::instance();
        s.setValue(sw->settingsKey("DisplayWnbLevel"), QString::number(level));
        s.save();
    });
    connect(menu, &SpectrumOverlayMenu::rfGainChanged,
            this, [this, sw](int gain) {
        m_radioModel.setPanRfGain(gain);
        sw->setRfGain(gain);
        auto& s = AppSettings::instance();
        s.setValue(sw->settingsKey("DisplayRfGain"), QString::number(gain));
        s.save();
    });

    // ── NR2/RN2 overlay toggle → AudioEngine ───────────────────────────
    // Button sync back to overlay + VFO handled by nr2/rn2EnabledChanged above.
    connect(menu, &SpectrumOverlayMenu::nr2Toggled,
            this, [this](bool on) {
        if (on) {
            m_audio.setRn2Enabled(false);
            enableNr2WithWisdom();
        } else {
            m_audio.setNr2Enabled(false);
        }
        // VFO button sync happens via AudioEngine::nr2EnabledChanged signal
    });
    connect(menu, &SpectrumOverlayMenu::rn2Toggled,
            this, [this](bool on) {
        if (on) {
            m_audio.setNr2Enabled(false);
            m_audio.setRn2Enabled(true);
        } else {
            m_audio.setRn2Enabled(false);
        }
        // VFO button sync happens via AudioEngine::rn2EnabledChanged signal
    });
}

void MainWindow::wireVfoWidget(VfoWidget* w, SliceModel* s)
{
    const int sliceId = s->sliceId();

    // Note: w->setSlice(s) is called at the end of this method (line ~1895)

    // Per-slice signals — these are specific to the slice this widget represents
    connect(w, &VfoWidget::closeSliceRequested, this, [this, sliceId]() {
        if (m_radioModel.slices().size() <= 1) return;
        m_radioModel.sendCommand(QString("slice remove %1").arg(sliceId));
    });
    connect(w, &VfoWidget::lockToggled, this, [this, sliceId](bool locked) {
        if (auto* sl = m_radioModel.slice(sliceId))
            sl->setLocked(locked);
    });
    connect(w, &VfoWidget::afGainChanged, this, [this, sliceId](int v) {
        if (auto* sl = m_radioModel.slice(sliceId))
            sl->setAudioGain(v);
    });
    connect(w, &VfoWidget::autotuneRequested, this, [this, sliceId](bool intermittent) {
        if (m_radioModel.slice(sliceId))
            m_radioModel.cwAutoTune(sliceId, intermittent);
    });

    // Clicking an inactive VfoWidget activates that slice
    connect(w, &VfoWidget::sliceActivationRequested, this, [this](int id) {
        if (id != m_activeSliceId)
            setActiveSlice(id);
    });

    // SWAP — swap RX and TX frequencies (keep TX/RX assignments)
    connect(w, &VfoWidget::swapRequested, this, [this]() {
        if (!m_splitActive || m_splitRxSliceId < 0 || m_splitTxSliceId < 0) return;
        auto* rx = m_radioModel.slice(m_splitRxSliceId);
        auto* tx = m_radioModel.slice(m_splitTxSliceId);
        if (!rx || !tx) return;
        double rxFreq = rx->frequency();
        double txFreq = tx->frequency();
        rx->setFrequency(txFreq);
        tx->setFrequency(rxFreq);
    });

    // Split toggle — per-widget, slice-aware
    connect(w, &VfoWidget::splitToggled, this, [this, sliceId]() {
        if (!m_splitActive) {
            // Entering split: this slice becomes RX, create a new TX slice
            if (m_radioModel.slices().size() >= m_radioModel.maxSlices())
                return;
            m_splitActive = true;
            m_splitRxSliceId = sliceId;
            m_radioModel.addSlice();
        } else if (sliceId == m_splitRxSliceId) {
            // Clicking SPLIT on the RX VFO again → disable split, destroy TX slice
            disableSplit();
        }
    });

    // NR2 toggle with FFTW wisdom generation — wired once per VFO, never disconnected
    connect(w, &VfoWidget::nr2Toggled, this, [this](bool on) {
        if (!on) { m_audio.setNr2Enabled(false); return; }
        enableNr2WithWisdom();
    });

    // RN2 toggle
    connect(w, &VfoWidget::rn2Toggled, this, [this](bool on) {
        m_audio.setRn2Enabled(on);
    });

#ifdef HAVE_RADE
    connect(w, &VfoWidget::radeActivated, this, [this](bool on, int sliceId) {
        if (on) activateRADE(sliceId); else deactivateRADE();
    });
#endif

    // Wire slice data into widget
    w->setSlice(s);
    w->setAntennaList(m_radioModel.antennaList());
    w->setTransmitModel(m_radioModel.transmitModel());
}

// wireActiveVfoSignals removed — NR2/RN2/RADE are now wired permanently
// in wireVfoWidget() so connections survive focus switches (#227).

void MainWindow::enableNr2WithWisdom()
{
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
                        dlg->setLabelText(d + "\n\n"
                            "This window will automatically close when wisdom generation is complete.");
                        if (dlg->value() >= 90 && breathe->state() != QAbstractAnimation::Running)
                            breathe->start();
                    } else {
                        dlg->setValue(pct);
                    }
                });
            });
        });
        connect(thread, &QThread::finished, this, [this, dlg, breathe, thread]() {
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
}

SpectrumWidget* MainWindow::spectrum() const
{
    return m_panStack ? m_panStack->activeSpectrum()
                      : (m_panApplet ? m_panApplet->spectrumWidget() : nullptr);
}

SpectrumWidget* MainWindow::spectrumForSlice(SliceModel* s) const
{
    if (s && m_panStack) {
        auto* sw = m_panStack->spectrum(s->panId());
        if (sw) return sw;
    }
    return spectrum();  // fallback to active pan
}

// ─── Pan layout application ───────────────────────────────────────────────────

// ─── Keyboard Shortcuts ───────────────────────────────────────────────────────

static bool isTextInputFocused()
{
    auto* w = QApplication::focusWidget();
    if (!w) return false;
    return qobject_cast<QLineEdit*>(w) || qobject_cast<QTextEdit*>(w)
        || qobject_cast<QPlainTextEdit*>(w) || qobject_cast<QSpinBox*>(w)
        || qobject_cast<QComboBox*>(w);
}

void MainWindow::setupKeyboardShortcuts()
{
    // Helper: nudge active slice frequency by N steps on the active pan
    auto nudgeFreq = [this](int steps) {
        if (!m_keyboardShortcutsEnabled || isTextInputFocused() || !m_radioModel.isConnected()) return;
        auto* s = activeSlice();
        if (!s || s->isLocked()) return;
        int stepHz = spectrum() ? spectrum()->stepSize() : 100;
        double newMhz = s->frequency() + steps * stepHz / 1e6;
        QString panId = m_panStack ? m_panStack->activePanId() : m_radioModel.panId();
        if (!panId.isEmpty())
            m_radioModel.sendCommand(
                QString("slice m %1 pan=%2").arg(newMhz, 0, 'f', 6).arg(panId));
        if (spectrum()) spectrum()->setVfoFrequency(newMhz);
    };

    // Left/Right arrow — nudge frequency by step size
    auto* left = new QShortcut(Qt::Key_Left, this);
    connect(left, &QShortcut::activated, this, [nudgeFreq]() { nudgeFreq(-1); });
    auto* right = new QShortcut(Qt::Key_Right, this);
    connect(right, &QShortcut::activated, this, [nudgeFreq]() { nudgeFreq(1); });

    // Shift+Left/Right — nudge by 10 steps
    auto* shiftLeft = new QShortcut(Qt::SHIFT | Qt::Key_Left, this);
    connect(shiftLeft, &QShortcut::activated, this, [nudgeFreq]() { nudgeFreq(-10); });
    auto* shiftRight = new QShortcut(Qt::SHIFT | Qt::Key_Right, this);
    connect(shiftRight, &QShortcut::activated, this, [nudgeFreq]() { nudgeFreq(10); });

    // Up/Down arrow — AF gain ±5
    auto* up = new QShortcut(Qt::Key_Up, this);
    connect(up, &QShortcut::activated, this, [this]() {
        if (!m_keyboardShortcutsEnabled || isTextInputFocused()) return;
        auto* s = activeSlice();
        if (s) s->setAudioGain(std::min(100.0f, s->audioGain() + 5.0f));
    });
    auto* down = new QShortcut(Qt::Key_Down, this);
    connect(down, &QShortcut::activated, this, [this]() {
        if (!m_keyboardShortcutsEnabled || isTextInputFocused()) return;
        auto* s = activeSlice();
        if (s) s->setAudioGain(std::max(0.0f, s->audioGain() - 5.0f));
    });

    // Spacebar — push-to-talk (TX while held)
    auto* pttOn = new QShortcut(Qt::Key_Space, this);
    pttOn->setAutoRepeat(false);
    connect(pttOn, &QShortcut::activated, this, [this]() {
        if (!m_keyboardShortcutsEnabled || isTextInputFocused() || !m_radioModel.isConnected()) return;
        m_radioModel.sendCommand("xmit 1");
    });
    // Note: QShortcut doesn't have a "released" signal. For PTT release,
    // we use keyReleaseEvent override. For now, spacebar toggles MOX.
    // TODO: implement held-spacebar PTT via keyPressEvent/keyReleaseEvent

    // T — toggle MOX
    auto* mox = new QShortcut(Qt::Key_T, this);
    connect(mox, &QShortcut::activated, this, [this]() {
        if (!m_keyboardShortcutsEnabled || isTextInputFocused() || !m_radioModel.isConnected()) return;
        bool tx = m_radioModel.transmitModel()->isTransmitting();
        m_radioModel.sendCommand(QString("xmit %1").arg(tx ? 0 : 1));
    });

    // M — toggle mute
    auto* mute = new QShortcut(Qt::Key_M, this);
    connect(mute, &QShortcut::activated, this, [this]() {
        if (!m_keyboardShortcutsEnabled || isTextInputFocused()) return;
        auto* s = activeSlice();
        if (s) s->setAudioMute(!s->audioMute());
    });

    // [ / ] — cycle step size down / up
    auto* stepDown = new QShortcut(Qt::Key_BracketLeft, this);
    connect(stepDown, &QShortcut::activated, this, [this]() {
        if (!m_keyboardShortcutsEnabled || isTextInputFocused()) return;
        auto* sw = spectrum();
        if (!sw) return;
        static const int steps[] = {10, 50, 100, 250, 500, 1000, 2500, 5000, 10000};
        int cur = sw->stepSize();
        for (int i = std::size(steps) - 1; i >= 0; --i) {
            if (steps[i] < cur) { sw->setStepSize(steps[i]); return; }
        }
    });
    auto* stepUp = new QShortcut(Qt::Key_BracketRight, this);
    connect(stepUp, &QShortcut::activated, this, [this]() {
        if (!m_keyboardShortcutsEnabled || isTextInputFocused()) return;
        auto* sw = spectrum();
        if (!sw) return;
        static const int steps[] = {10, 50, 100, 250, 500, 1000, 2500, 5000, 10000};
        int cur = sw->stepSize();
        for (int i = 0; i < static_cast<int>(std::size(steps)); ++i) {
            if (steps[i] > cur) { sw->setStepSize(steps[i]); return; }
        }
    });

    // L — toggle tune lock
    auto* lock = new QShortcut(Qt::Key_L, this);
    connect(lock, &QShortcut::activated, this, [this]() {
        if (!m_keyboardShortcutsEnabled || isTextInputFocused()) return;
        auto* s = activeSlice();
        if (s) s->setLocked(!s->isLocked());
    });
}

void MainWindow::applyPanLayout(const QString& layoutId)
{
    if (!m_radioModel.isConnected()) return;

    static const QMap<QString, int> kPanCounts = {
        {"1", 1}, {"2v", 2}, {"2h", 2}, {"2h1", 3}, {"12h", 3}, {"2x2", 4}
    };
    const int needed = kPanCounts.value(layoutId, 1);
    const int existing = m_panStack->count();

    if (needed < existing) {
        qDebug() << "applyPanLayout: reducing from" << existing << "to" << needed << "pans";

        // Close extra pans from the end (keep the first N)
        auto allApplets = m_panStack->allApplets();
        int toRemove = existing - needed;
        for (int i = allApplets.size() - 1; i >= 0 && toRemove > 0; --i) {
            auto* applet = allApplets[i];
            QString panId = applet->panId();
            if (panId == "default") continue;
            qDebug() << "applyPanLayout: closing pan" << panId;
            m_radioModel.sendCommand(
                QString("display pan remove %1").arg(panId));
            // Radio will send "removed" status → panadapterRemoved signal
            // → PanadapterStack::removePanadapter()
            --toRemove;
        }

        // Rearrange remaining pans after a short delay for radio to process
        QTimer::singleShot(500, this, [this, layoutId]() {
            m_panStack->rearrangeLayout(layoutId);
        });
        return;
    }
    if (needed == existing) {
        // Same count, just rearrange
        m_panStack->rearrangeLayout(layoutId);
        return;
    }

    // Create additional pans to reach the needed count.
    // Keep existing pan(s) alive — no tear-down, no dangling signals.
    const int toCreate = needed - existing;
    auto panIds = std::make_shared<QStringList>();

    // Collect existing pan IDs first (they'll be part of the layout)
    for (auto* applet : m_panStack->allApplets())
        panIds->append(applet->panId());

    qDebug() << "applyPanLayout: have" << existing << "pans, creating"
             << toCreate << "more for layout" << layoutId;

    createPansSequentially(layoutId, toCreate, panIds, 0);
}

void MainWindow::createPansSequentially(const QString& layoutId, int total,
                                        std::shared_ptr<QStringList> panIds, int created)
{
    if (created >= total) {
        // All new pans created — wait for radio status to establish PanadapterModels
        qDebug() << "applyPanLayout: all" << total << "new pans created:" << *panIds;
        QTimer::singleShot(800, this, [this, panIds, layoutId]() {
            // The new pans were added to the stack via panadapterAdded handler.
            // Wire any that aren't already wired.
            for (auto* applet : m_panStack->allApplets()) {
                const QString pid = applet->panId();
                auto* pan = m_radioModel.panadapter(pid);
                if (pan) {
                    // Push current state to the spectrum widget
                    applet->spectrumWidget()->setDbmRange(pan->minDbm(), pan->maxDbm());
                    applet->spectrumWidget()->setFrequencyRange(
                        pan->centerMhz(), pan->bandwidthMhz());
                }
            }

            // Rearrange splitter structure for the selected layout
            m_panStack->rearrangeLayout(layoutId);

            m_panApplet = m_panStack->activeApplet();

            qDebug() << "applyPanLayout: layout" << layoutId
                     << "complete, total pans:" << m_panStack->count();
        });
        return;
    }

    m_radioModel.sendCmdPublic(
        "display panafall create x=100 y=100",
        [this, panIds, layoutId, total, created](int code, const QString& body) {
            if (code != 0) {
                qWarning() << "applyPanLayout: panafall create failed, code"
                           << Qt::hex << code << body;
                return;
            }
            const auto kvs = CommandParser::parseKVs(body);
            QString panId;
            if (kvs.contains("pan"))       panId = kvs["pan"];
            else if (kvs.contains("id"))   panId = kvs["id"];
            else                           panId = body.trimmed();

            qDebug() << "applyPanLayout: created pan" << (created + 1) << "of" << total
                     << "id:" << panId;

            if (!panId.isEmpty()) {
                panIds->append(panId);
                // Configure the pan
                m_radioModel.sendCommand(
                    QString("display pan set %1 xpixels=1024 ypixels=700").arg(panId));
                m_radioModel.sendCommand(
                    QString("display pan set %1 fps=25").arg(panId));
            }

            // Create next pan after a brief delay
            QTimer::singleShot(200, this, [this, layoutId, total, panIds, created]() {
                createPansSequentially(layoutId, total, panIds, created + 1);
            });
        });
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
    if (auto* pan = m_radioModel.activePanadapter()) {
        m_radioModel.sendCommand(
            QString("display pan set %1 center=%2").arg(pan->panId()).arg(snap.panCenterMhz, 0, 'f', 6));
        m_radioModel.sendCommand(
            QString("display pan set %1 bandwidth=%2").arg(pan->panId()).arg(snap.panBandwidthMhz, 0, 'f', 6));
        m_radioModel.sendCommand(
            QString("display pan set %1 min_dbm=%2 max_dbm=%3").arg(pan->panId())
                .arg(static_cast<double>(snap.minDbm), 0, 'f', 2)
                .arg(static_cast<double>(snap.maxDbm), 0, 'f', 2));
    }
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
        if (auto* s = activeSlice()) {
            s->setFrequency(mhz);

            // Diversity: immediately mirror freq to child VFO (no radio round-trip)
            if (s->isDiversityParent()) {
                long long hz = static_cast<long long>(std::round(mhz * 1e6));
                QString freqStr = QString("%1.%2.%3")
                    .arg(static_cast<int>(hz / 1000000))
                    .arg(static_cast<int>((hz / 1000) % 1000), 3, 10, QChar('0'))
                    .arg(static_cast<int>(hz % 1000), 3, 10, QChar('0'));
                for (auto* other : m_radioModel.slices()) {
                    if (other->isDiversityChild() && other->sliceId() != s->sliceId()) {
                        auto* csw = spectrumForSlice(other);
                        if (!csw) continue;
                        auto* cv = csw->vfoWidget(other->sliceId());
                        if (cv) cv->freqLabel()->setText(freqStr);
                        // Also update the overlay marker position
                        csw->setSliceOverlay(other->sliceId(), mhz,
                            other->filterLow(), other->filterHigh(),
                            other->isTxSlice(), false,
                            other->mode(), other->rttyMark(), other->rttyShift(),
                            other->ritOn(), other->ritFreq(),
                            other->xitOn(), other->xitFreq());
                    }
                }
            }
        }
    }
}

#ifdef HAVE_RADE
void MainWindow::activateRADE(int sliceId)
{
    // Guard against duplicate activation (VfoWidget + RxApplet both selecting RADE)
    if (m_radeSliceId == sliceId && m_radeEngine && m_radeEngine->isActive())
        return;

    // If RADE is already active on a different slice, deactivate it first
    if (m_radeSliceId >= 0 && m_radeSliceId != sliceId)
        deactivateRADE();

    auto* s = m_radioModel.slice(sliceId);
    if (!s) return;

    // Set radio mode to DIGU/DIGL (passthrough for OFDM modem)
    double freqMhz = s->frequency();
    QString mode = (freqMhz < 10.0) ? "DIGL" : "DIGU";
    s->setMode(mode);
    if (mode == "DIGL")
        s->setFilterWidth(-3500, 0);
    else
        s->setFilterWidth(0, 3500);

    // Remember which slice and its previous mute state
    m_radeSliceId = sliceId;
    m_radePrevMute = s->audioMute();
    s->setAudioMute(true);

    // Create RADE engine on a worker thread for multi-core utilization
    if (!m_radeEngine) {
        m_radeEngine = new RADEEngine;  // no parent — will be moved to worker thread
        m_radeThread = new QThread(this);
        m_radeThread->setObjectName("RADEEngine");
        m_radeEngine->moveToThread(m_radeThread);
        connect(m_radeThread, &QThread::finished, m_radeEngine, &QObject::deleteLater);
        m_radeThread->start();
    }
    // start() must be invoked on the worker thread
    bool ok = false;
    QMetaObject::invokeMethod(m_radeEngine, [this, &ok]() {
        ok = m_radeEngine->start();
    }, Qt::BlockingQueuedConnection);
    if (!ok) {
        qWarning() << "MainWindow: failed to start RADE engine";
        return;
    }

    m_audio.setRadeMode(true);

    // TX path: mic -> RADEEngine (worker) -> sendModemTxAudio (main)
    connect(&m_audio, &AudioEngine::txRawPcmReady,
            m_radeEngine, &RADEEngine::feedTxAudio, Qt::QueuedConnection);
    connect(m_radeEngine, &RADEEngine::txModemReady,
            &m_audio, &AudioEngine::sendModemTxAudio, Qt::QueuedConnection);

    // RX path: DAX RX audio -> RADEEngine (worker) -> decoded speech -> speaker (main)
    // Filter by the RADE slice's DAX channel so other slices' DAX audio is ignored.
    // Look up the channel live so it tracks if the user changes DAX assignment.
    int sid = sliceId;
    connect(m_radioModel.panStream(), &PanadapterStream::daxAudioReady,
            m_radeEngine, [this, sid](int channel, const QByteArray& pcm) {
        auto* s = m_radioModel.slice(sid);
        if (s && channel == s->daxChannel())
            m_radeEngine->feedRxAudio(channel, pcm);
    }, Qt::QueuedConnection);
    connect(m_radeEngine, &RADEEngine::rxSpeechReady,
            &m_audio, &AudioEngine::feedDecodedSpeech, Qt::QueuedConnection);

    // Start mic capture if not already running
    if (!m_audio.isTxStreaming()) {
        m_audio.startTxStream(
            m_radioModel.connection()->radioAddress(), 4991);
    }

    // RADE status indicator in VFO widget
    if (auto* vfo = spectrum()->vfoWidget()) {
        vfo->setRadeActive(true);
        connect(m_radeEngine, &RADEEngine::syncChanged,
                vfo, &VfoWidget::setRadeSynced);
        connect(m_radeEngine, &RADEEngine::snrChanged,
                vfo, &VfoWidget::setRadeSnr);
        connect(m_radeEngine, &RADEEngine::freqOffsetChanged,
                vfo, &VfoWidget::setRadeFreqOffset);
    }

    qInfo() << "MainWindow: RADE mode activated on slice" << sliceId;
}

void MainWindow::deactivateRADE()
{
    // Restore audio mute state on the RADE slice
    if (m_radeSliceId >= 0) {
        if (auto* s = m_radioModel.slice(m_radeSliceId))
            s->setAudioMute(m_radePrevMute);
        m_radeSliceId = -1;
    }

    if (auto* vfo = spectrum()->vfoWidget())
        vfo->setRadeActive(false);

    m_audio.setRadeMode(false);
    m_audio.clearTxAccumulators();  // flush stale RADE modem data

    if (m_radeEngine) {
        disconnect(&m_audio, &AudioEngine::txRawPcmReady,
                   m_radeEngine, nullptr);
        disconnect(m_radeEngine, &RADEEngine::txModemReady,
                   &m_audio, nullptr);
        disconnect(m_radioModel.panStream(), &PanadapterStream::daxAudioReady,
                   m_radeEngine, nullptr);
        disconnect(m_radeEngine, &RADEEngine::rxSpeechReady,
                   &m_audio, nullptr);
        // Stop on the worker thread, then shut down the thread
        QMetaObject::invokeMethod(m_radeEngine, &RADEEngine::stop,
                                  Qt::BlockingQueuedConnection);
        if (m_radeThread) {
            m_radeThread->quit();
            m_radeThread->wait(2000);
            m_radeThread->deleteLater();
            m_radeThread = nullptr;
        }
        m_radeEngine = nullptr;  // deleteLater handles actual deletion
    }

    qInfo() << "MainWindow: RADE mode deactivated";
}
#endif

#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
void MainWindow::startDax()
{
    if (m_daxBridge) return;

#ifdef Q_OS_MAC
    // Only start if HAL plugin is installed
    if (!QFileInfo::exists("/Library/Audio/Plug-Ins/HAL/AetherSDRDAX.driver/Contents/MacOS/AetherSDRDAX")) {
        qInfo() << "MainWindow: DAX HAL plugin not installed, skipping DAX bridge";
        return;
    }
#endif

    m_daxBridge = new DaxBridge(this);
    if (!m_daxBridge->open()) {
        qWarning() << "MainWindow: failed to open DAX audio bridge";
        delete m_daxBridge;
        m_daxBridge = nullptr;
        return;
    }

    // Listen for DAX stream status messages to register them in PanadapterStream.
    // The radio sends "stream 0xNNNNNNNN type=dax_rx dax_channel=N" status lines
    // in response to our stream create commands.
    connect(&m_radioModel, &RadioModel::statusReceived,
            m_daxBridge, [this](const QString& obj, const QMap<QString,QString>& kvs) {
        if (!obj.startsWith("stream ")) return;
        QString type = kvs.value("type");
        if (type == "dax_rx") {
            quint32 streamId = obj.mid(7).toUInt(nullptr, 16);
            int ch = kvs.value("dax_channel").toInt();
            if (streamId && ch >= 1 && ch <= 4) {
                m_radioModel.panStream()->registerDaxStream(streamId, ch);
                qDebug() << "MainWindow: registered DAX RX ch" << ch
                         << "stream" << Qt::hex << streamId;
            }
        }
    });

    // Create DAX RX streams (4 channels)
    for (int ch = 1; ch <= 4; ++ch)
        m_radioModel.sendCommand(QString("stream create type=dax_rx dax_channel=%1").arg(ch));

    // Wire DAX RX: PanadapterStream routes registered DAX streams here
    connect(m_radioModel.panStream(), &PanadapterStream::daxAudioReady,
            m_daxBridge, &DaxBridge::feedDaxAudio);

    // Wire DAX level meters
    connect(m_daxBridge, &DaxBridge::daxRxLevel,
            m_appletPanel->catApplet(), &CatApplet::setDaxRxLevel);
    connect(m_daxBridge, &DaxBridge::daxTxLevel,
            m_appletPanel->catApplet(), &CatApplet::setDaxTxLevel);

    // Wire DAX gain sliders
    connect(m_appletPanel->catApplet(), &CatApplet::daxRxGainChanged,
            m_daxBridge, &DaxBridge::setChannelGain);
    connect(m_appletPanel->catApplet(), &CatApplet::daxTxGainChanged,
            m_daxBridge, &DaxBridge::setTxGain);

    // Apply saved gains to the bridge
    auto& ss = AppSettings::instance();
    for (int i = 1; i <= 4; ++i)
        m_daxBridge->setChannelGain(i, ss.value(QStringLiteral("DaxRxGain%1").arg(i), "0.5").toString().toFloat());
    m_daxBridge->setTxGain(ss.value("DaxTxGain", "0.5").toString().toFloat());

    // Wire DAX TX: apps → bridge → AudioEngine → VITA-49
    // feedDaxTxAudio blocks only when mic is actively sending (voice TX),
    // preventing dual-source jitter. During RX and digital TX, DAX flows
    // freely — this keeps VARAC/WSJT-X pre-PTT audio in the radio's
    // buffer so TX starts instantly with no delay.
    connect(m_daxBridge, &DaxBridge::txAudioReady,
            this, [this](const QByteArray& pcm) {
        if (m_audio.isRadeMode()) return;
        m_audio.feedDaxTxAudio(pcm);
    });

    // Save current mic selection and switch to PC + dax=1
    m_savedMicSelection = m_radioModel.transmitModel()->micSelection();
    m_radioModel.sendCommand("transmit set mic_selection=PC");
    m_radioModel.sendCommand("transmit set dax=1");

    qInfo() << "MainWindow: starting DAX audio bridge";
}

void MainWindow::stopDax()
{
    if (!m_daxBridge) return;

    m_audio.setDaxTxMode(false);

    disconnect(m_radioModel.panStream(), &PanadapterStream::daxAudioReady,
               m_daxBridge, nullptr);
    disconnect(m_daxBridge, &DaxBridge::txAudioReady,
               this, nullptr);

    // Restore original mic selection
    if (!m_savedMicSelection.isEmpty() && m_savedMicSelection != "PC")
        m_radioModel.sendCommand(QString("transmit set mic_selection=%1").arg(m_savedMicSelection));

    m_daxBridge->close();
    delete m_daxBridge;
    m_daxBridge = nullptr;
    qInfo() << "MainWindow: stopping DAX audio bridge";
}
#endif

} // namespace AetherSDR
