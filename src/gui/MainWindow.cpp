#include "MainWindow.h"
#ifdef HAVE_MQTT
#include "MqttApplet.h"
#endif
#include "ConnectionPanel.h"
#include "ClientDisconnectDialog.h"
#include "TitleBar.h"
#include "PanadapterApplet.h"
#include "PanadapterStack.h"
#include "PanLayoutDialog.h"
#include "core/CommandParser.h"
#include "core/LogManager.h"
#include "models/PanadapterModel.h"
#include "SpectrumWidget.h"
#ifdef AETHER_GPU_SPECTRUM
#include <QRhiWidget>
#endif
#include "SpectrumOverlayMenu.h"
#include "VfoWidget.h"
#include "AppletPanel.h"
#include "containers/ContainerManager.h"
#include "RxApplet.h"
#include "SMeterWidget.h"
#include "TunerApplet.h"
#include "TxApplet.h"
#include "PhoneCwApplet.h"
#include "PhoneApplet.h"
#include "EqApplet.h"
#include "WaveApplet.h"
#include "ClientEqApplet.h"
#include "ClientEqEditor.h"
#include "ClientCompApplet.h"
#include "ClientCompEditor.h"
#include "ClientGateApplet.h"
#include "ClientGateEditor.h"
#include "ClientDeEssApplet.h"
#include "ClientDeEssEditor.h"
#include "ClientTubeApplet.h"
#include "ClientTubeEditor.h"
#include "ClientPuduApplet.h"
#include "ClientPuduEditor.h"
#include "ClientReverbApplet.h"
#include "ClientReverbEditor.h"
#include "ClientChainApplet.h"
#include "core/ClientComp.h"
#include "core/ClientEq.h"
#include "core/ClientGate.h"
#include "core/ClientDeEss.h"
#include "core/ClientTube.h"
#include "core/ClientPudu.h"
#include "core/ClientReverb.h"
#include "core/CwSidetoneGenerator.h"
#include "core/CwxLocalKeyer.h"
#include "CatControlApplet.h"
#include "DaxApplet.h"
#include "TciApplet.h"
#include "DaxIqApplet.h"
#include "AntennaGeniusApplet.h"
#include "RadioSetupDialog.h"
#include "NetworkDiagnosticsDialog.h"
#include "PropDashboardDialog.h"
#include "MemoryCommands.h"
#include "MemoryDialog.h"
#include "DxClusterDialog.h"
#include "CwxPanel.h"
#include "DvkPanel.h"
#include "core/DvkWavTransfer.h"
#include "AmpApplet.h"
#include "MeterApplet.h"
#include "ProfileManagerDialog.h"
#include "SupportDialog.h"
#include "SliceTroubleshootingDialog.h"
#include "ShortcutDialog.h"
#include "MultiFlexDialog.h"
#include "HelpDialog.h"
#include "WhatsNewDialog.h"
#include "models/SliceModel.h"
#include "models/MeterModel.h"
#include "models/BandDefs.h"
#include "models/BandPlanManager.h"
#include "models/XvtrPolicy.h"
#include "core/BandStackSettings.h"
#include "gui/BandStackPanel.h"
#include "models/TunerModel.h"
#include "models/TransmitModel.h"
#include "models/EqualizerModel.h"
#ifdef HAVE_MIDI
#include "core/MidiSettings.h"
#include "MidiMappingDialog.h"
#endif
#include "AetherDspDialog.h"
#include "DspParamPopup.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <functional>
#include <QApplication>
#include <QProcess>
#include <QScreen>
#include <QTimer>
#include <QDateTime>
#include <QPropertyAnimation>
#include <QIcon>
#include <QKeyEvent>
#include <QWindow>
#include <QPixmap>
#include <QWidgetAction>
#include <QPainter>
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
#include <QAbstractSlider>
#include <QLabel>
#include <QCloseEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QScrollArea>
#include <QSizeGrip>
#include <QStatusBar>
#include <QFrame>
#include <QFileDialog>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "core/VersionNumber.h"
#include <QPointer>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QProgressDialog>
#include <QThread>
#include "core/AppSettings.h"
#include "core/SpotCommandPolicy.h"
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
#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif
#include <QLocale>

namespace AetherSDR {

namespace {

constexpr double kIncrementalTriggerEdgeMarginFrac = 0.05;
constexpr double kIncrementalSettleEdgeMarginFrac = 0.06;
constexpr double kRevealComfortEdgeMarginFrac = 0.18;
constexpr double kSpectrumClickEdgeMarginFrac = 0.05;
constexpr int kPanFollowAnimationDurationMs = 110;
constexpr int kSliderShortcutLeaseMs = 2000;
constexpr qint64 kXvtrWaterfallDecisionLogIntervalMs = 20000;

QVector<XvtrPolicy::Transverter> xvtrPolicyBandsFrom(
    const QMap<int, RadioModel::XvtrInfo>& xvtrs)
{
    QVector<XvtrPolicy::Transverter> bands;
    bands.reserve(xvtrs.size());
    for (auto it = xvtrs.cbegin(); it != xvtrs.cend(); ++it) {
        const auto& x = it.value();
        bands.append({x.index, x.order, x.name, x.rfFreq, x.ifFreq, x.isValid});
    }
    return bands;
}

QString xvtrSummary(const XvtrPolicy::Transverter& xvtr)
{
    return QStringLiteral("%1[idx=%2 order=%3 valid=%4 rf=%5 if=%6 offset=%7]")
        .arg(xvtr.name.isEmpty() ? QStringLiteral("(unnamed)") : xvtr.name)
        .arg(xvtr.index)
        .arg(xvtr.order)
        .arg(xvtr.isValid ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(xvtr.rfFreqMhz, 0, 'f', 6)
        .arg(xvtr.ifFreqMhz, 0, 'f', 6)
        .arg(xvtr.rfFreqMhz - xvtr.ifFreqMhz, 0, 'f', 6);
}

QString xvtrListSummary(const QVector<XvtrPolicy::Transverter>& xvtrs)
{
    QStringList entries;
    entries.reserve(xvtrs.size());
    for (const auto& xvtr : xvtrs)
        entries << xvtrSummary(xvtr);
    return entries.isEmpty() ? QStringLiteral("(none)") : entries.join(QStringLiteral("; "));
}

QString xvtrForBandSummary(const QString& bandName,
                           const QVector<XvtrPolicy::Transverter>& xvtrs)
{
    for (const auto& xvtr : xvtrs) {
        if (xvtr.name == bandName)
            return xvtrSummary(xvtr);
    }
    return QStringLiteral("(none)");
}

void logXvtrWaterfallDecision(quint32 streamId,
                              const QString& panId,
                              double panCenterMhz,
                              double originalLowMhz,
                              double originalHighMhz,
                              const XvtrPolicy::WaterfallTileRange& mapped,
                              const XvtrPolicy::WaterfallTileMatch& match,
                              bool hasXvtrSliceAntenna,
                              const QVector<XvtrPolicy::Transverter>& xvtrs)
{
    if (!lcConnection().isDebugEnabled())
        return;

    const QString reason = mapped.shifted
        ? (match.matched ? QStringLiteral("matched_xvtr_offset")
                         : QStringLiteral("xvt_slice_antenna_fallback"))
        : QStringLiteral("no_xvtr_evidence");
    const QString key = QStringLiteral("%1:%2").arg(streamId).arg(panId);
    const QString signature = QStringLiteral("%1:%2:%3:%4")
        .arg(reason)
        .arg(mapped.shifted ? QStringLiteral("shifted") : QStringLiteral("unchanged"))
        .arg(match.matched ? match.name : QStringLiteral("(none)"))
        .arg(hasXvtrSliceAntenna ? QStringLiteral("xvt_ant") : QStringLiteral("no_xvt_ant"));

    struct LogState {
        QString signature;
        qint64 lastLoggedMs{0};
    };
    static QHash<QString, LogState> logStateByStream;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    auto& state = logStateByStream[key];
    if (state.signature == signature &&
        state.lastLoggedMs > 0 &&
        now - state.lastLoggedMs < kXvtrWaterfallDecisionLogIntervalMs) {
        return;
    }
    state.signature = signature;
    state.lastLoggedMs = now;

    qCDebug(lcConnection).noquote().nospace()
        << "WaterfallXVTR: stream=0x" << QString::number(streamId, 16)
        << " pan=" << panId
        << " reason=" << reason
        << " shifted=" << mapped.shifted
        << " pan_center_mhz=" << QString::number(panCenterMhz, 'f', 6)
        << " tile_mhz=" << QString::number(originalLowMhz, 'f', 6)
        << ".." << QString::number(originalHighMhz, 'f', 6)
        << " mapped_mhz=" << QString::number(mapped.lowMhz, 'f', 6)
        << ".." << QString::number(mapped.highMhz, 'f', 6)
        << " observed_offset_mhz=" << QString::number(match.observedOffsetMhz, 'f', 6)
        << " expected_offset_mhz=" << (match.matched
               ? QString::number(match.expectedOffsetMhz, 'f', 6)
               : QStringLiteral("n/a"))
        << " tolerance_mhz=" << QString::number(match.toleranceMhz, 'f', 6)
        << " matched_xvtr=" << (match.matched
               ? QStringLiteral("%1[idx=%2 order=%3]")
                     .arg(match.name.isEmpty() ? QStringLiteral("(unnamed)") : match.name)
                     .arg(match.index)
                     .arg(match.order)
               : QStringLiteral("(none)"))
        << " has_xvt_slice_antenna=" << hasXvtrSliceAntenna
        << " candidates=" << xvtrListSummary(xvtrs);
}

double quantizeIncrementalFollowDelta(double overshootMhz, double stepMhz)
{
    if (overshootMhz <= 0.0)
        return 0.0;
    if (stepMhz <= 0.0)
        return overshootMhz;
    return std::ceil((overshootMhz - 1e-12) / stepMhz) * stepMhz;
}

}  // namespace

static bool macDaxDriverInstalled()
{
#ifdef Q_OS_MAC
    const QFileInfo driverBundle("/Library/Audio/Plug-Ins/HAL/AetherSDRDAX.driver");
    if (!driverBundle.exists() || !driverBundle.isDir())
        return false;

    const QString bundlePath = driverBundle.absoluteFilePath();
    const QFileInfo driverExec(bundlePath + "/Contents/MacOS/AetherSDRDAX");
    const QFileInfo infoPlist(bundlePath + "/Contents/Info.plist");
    return driverExec.exists() && driverExec.isFile() && infoPlist.exists() && infoPlist.isFile();
#else
    return true;
#endif
}

static QString formatNetworkMs(int ms)
{
    return ms < 1 ? "< 1 ms" : QString("%1 ms").arg(ms);
}

static QString formatNetworkSeqErrors(int errors, int packets)
{
    if (packets == 0) {
        return "0 / 0 packets";
    }

    const double pct = (errors * 100.0) / packets;
    return QString("%1 / %2 packets (%3%)")
        .arg(errors)
        .arg(packets)
        .arg(pct, 0, 'f', 2);
}

static QString formatNetworkSeqErrors(const PanadapterStream::CategoryStats& stats)
{
    return formatNetworkSeqErrors(stats.errors, stats.packets);
}

static QString buildNetworkTooltip(const RadioModel& model)
{
    const PanadapterStream::CategoryStats audioStats =
        model.categoryStats(PanadapterStream::CatAudio);
    const PanadapterStream::CategoryStats fftStats =
        model.categoryStats(PanadapterStream::CatFFT);
    const PanadapterStream::CategoryStats waterfallStats =
        model.categoryStats(PanadapterStream::CatWaterfall);
    const PanadapterStream::CategoryStats meterStats =
        model.categoryStats(PanadapterStream::CatMeter);
    const PanadapterStream::CategoryStats daxStats =
        model.categoryStats(PanadapterStream::CatDAX);

    QStringList lines;
    lines
        << QString("Network: %1").arg(model.networkQuality())
        << QString("Latency (RTT): %1").arg(formatNetworkMs(model.lastPingRtt()))
        << QString("Max RTT (session): %1").arg(formatNetworkMs(model.maxPingRtt()))
        << QString("Packet loss (%1s): %2")
               .arg(model.packetLossWindowSeconds())
               .arg(formatNetworkSeqErrors(model.packetLossWindowDrops(),
                                           model.packetLossWindowPackets()))
        << QString("Network jitter: %1").arg(formatNetworkMs(model.audioPacketJitterMs()))
        << QString("Audio gap: %1 (max %2)")
               .arg(formatNetworkMs(model.audioPacketGapMs()),
                    formatNetworkMs(model.audioPacketGapMaxMs()))
        << QString("Total sequence gaps: %1")
               .arg(formatNetworkSeqErrors(model.packetDropCount(), model.packetTotalCount()))
        << QString("Audio: %1").arg(formatNetworkSeqErrors(audioStats))
        << QString("FFT: %1").arg(formatNetworkSeqErrors(fftStats))
        << QString("Waterfall: %1").arg(formatNetworkSeqErrors(waterfallStats))
        << QString("Meters: %1").arg(formatNetworkSeqErrors(meterStats))
        << QString("DAX: %1").arg(formatNetworkSeqErrors(daxStats))
        << QString("UDP RX bytes: %1").arg(QLocale().formattedDataSize(model.rxBytes()))
        << QString("UDP TX bytes: %1").arg(QLocale().formattedDataSize(model.txBytes()))
        << "Double-click for full diagnostics";
    return lines.join('\n');
}

// ─── Shortcut guard (file-scope for use as std::function<bool()>) ───────────

static constexpr const char* kPaTempUnitSettingKey = "PaTempDisplayUnit";
static constexpr int kMemorySpotIdBase = 1000000;
static constexpr int kPassiveSpotIdBase = 2000000;

static bool s_keyboardShortcutsEnabled = false;
static bool s_sliderShortcutLeaseActive = false;

static int memorySpotId(int memoryIndex)
{
    return -(kMemorySpotIdBase + memoryIndex);
}

static int memoryIndexFromSpotId(int spotIndex)
{
    if (spotIndex > -kMemorySpotIdBase)
        return -1;
    return -spotIndex - kMemorySpotIdBase;
}

static bool isPassiveLocalSpotId(int spotIndex)
{
    return spotIndex <= -kPassiveSpotIdBase;
}

static QString memorySpotLabel(const MemoryEntry& memory)
{
    if (!memory.name.trimmed().isEmpty())
        return memory.name.trimmed();
    if (!memory.group.trimmed().isEmpty())
        return memory.group.trimmed();
    return QString("Memory %1").arg(memory.index);
}

static QString memorySpotComment(const MemoryEntry& memory)
{
    QStringList parts;
    if (!memory.group.trimmed().isEmpty())
        parts << QString("Group: %1").arg(memory.group.trimmed());
    if (!memory.owner.trimmed().isEmpty())
        parts << QString("Owner: %1").arg(memory.owner.trimmed());
    if (!memory.mode.trimmed().isEmpty())
        parts << QString("Mode: %1").arg(memory.mode.trimmed());
    if (memory.rxFilterLow != 0 || memory.rxFilterHigh != 0) {
        parts << QString("Filter: %1..%2 Hz")
                    .arg(memory.rxFilterLow)
                    .arg(memory.rxFilterHigh);
    }
    return parts.join(" | ");
}

static QPixmap buildBandStackIndicatorPixmap(bool active)
{
    QPixmap pixmap(10, 22);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(active ? QColor(0x00, 0xb4, 0xd8) : QColor(0x40, 0x48, 0x58));
    painter.drawEllipse(2, 1, 6, 6);
    painter.drawEllipse(2, 8, 6, 6);
    painter.drawEllipse(2, 15, 6, 6);
    return pixmap;
}

static bool shortcutInputCaptured()
{
    if (s_sliderShortcutLeaseActive)
        return true;

    auto* w = QApplication::focusWidget();
    if (!w) return false;
    return qobject_cast<QLineEdit*>(w) || qobject_cast<QTextEdit*>(w)
        || qobject_cast<QPlainTextEdit*>(w) || qobject_cast<QSpinBox*>(w)
        || qobject_cast<QComboBox*>(w);
}

static bool shortcutGuard() {
    return s_keyboardShortcutsEnabled && !shortcutInputCaptured();
}

static QStringList splitClientField(const QString& raw)
{
    QString cleaned = raw;
    cleaned.replace(QChar(0x7f), QLatin1Char(' '));

    QStringList values;
    for (const QString& value : cleaned.split(',', Qt::SkipEmptyParts))
        values << value.trimmed();
    return values;
}

static quint32 parseClientHandle(QString text)
{
    text = text.trimmed();
    if (text.startsWith("0x", Qt::CaseInsensitive))
        text = text.mid(2);

    bool ok = false;
    const quint32 handle = text.toUInt(&ok, 16);
    return ok ? handle : 0;
}

static QList<ClientDisconnectDialog::Client> buildDisconnectClients(const QStringList& handles,
                                                                    const QStringList& programs,
                                                                    const QStringList& stations)
{
    QList<ClientDisconnectDialog::Client> clients;
    for (int i = 0; i < handles.size(); ++i) {
        const quint32 handle = parseClientHandle(handles[i]);
        if (handle == 0)
            continue;

        if (std::any_of(clients.cbegin(), clients.cend(), [handle](const auto& client) {
                return client.handle == handle;
            })) {
            continue;
        }

        ClientDisconnectDialog::Client client;
        client.handle = handle;
        if (i < programs.size())
            client.program = programs[i];
        if (i < stations.size())
            client.station = stations[i];
        clients.append(client);
    }
    return clients;
}

static QList<ClientDisconnectDialog::Client> buildDisconnectClients(const RadioInfo& info)
{
    return buildDisconnectClients(info.guiClientHandles,
                                  info.guiClientPrograms,
                                  info.guiClientStations);
}

static QList<ClientDisconnectDialog::Client> buildDisconnectClients(const WanRadioInfo& info)
{
    return buildDisconnectClients(splitClientField(info.guiClientHandles),
                                  splitClientField(info.guiClientPrograms),
                                  splitClientField(info.guiClientStations));
}

static QString cleanClientDisplayText(QString value)
{
    value.replace(QChar(0x7f), QLatin1Char(' '));
    return value.trimmed();
}

static QString clientConnectionStatusMessage(quint32 handle,
                                             const QString& source,
                                             const QString& station,
                                             const QString& program)
{
    QString from = cleanClientDisplayText(source);
    const QString stationText = cleanClientDisplayText(station);
    const QString programText = cleanClientDisplayText(program);
    QString detail = stationText;

    if (detail.isEmpty() || detail.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0)
        detail = programText;
    if (detail.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0)
        detail.clear();

    if (from.isEmpty())
        from = detail;
    if (from.isEmpty())
        from = QStringLiteral("client 0x%1").arg(handle, 8, 16, QChar('0')).toUpper();

    if (!detail.isEmpty() && detail.compare(from, Qt::CaseInsensitive) != 0)
        return QObject::tr("New client connection from %1 (%2)").arg(from, detail);

    return QObject::tr("New client connection from %1").arg(from);
}

bool MainWindow::confirmClientSlotAvailability(const RadioInfo& info,
                                               QList<quint32>* disconnectHandles)
{
    if (disconnectHandles)
        disconnectHandles->clear();

    const auto clients = buildDisconnectClients(info);
    const int maxSlices = RadioModel::maxSlicesForModel(info.model);
    if (clients.isEmpty() || clients.size() < maxSlices)
        return true;

    ClientDisconnectDialog dialog(clients, maxSlices, this);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (disconnectHandles)
        *disconnectHandles = dialog.selectedHandles();
    return !dialog.selectedHandles().isEmpty();
}

bool MainWindow::confirmClientSlotAvailability(const WanRadioInfo& info,
                                               QList<quint32>* disconnectHandles)
{
    if (disconnectHandles)
        disconnectHandles->clear();

    const auto clients = buildDisconnectClients(info);
    const int maxSlices = RadioModel::maxSlicesForModel(info.model);
    if (clients.isEmpty() || clients.size() < maxSlices)
        return true;

    ClientDisconnectDialog dialog(clients, maxSlices, this);
    if (dialog.exec() != QDialog::Accepted)
        return false;

    if (disconnectHandles)
        *disconnectHandles = dialog.selectedHandles();
    return !dialog.selectedHandles().isEmpty();
}

void MainWindow::disconnectWanRadioClients(const WanRadioInfo& info)
{
    if (!m_smartLink.isConnected()) {
        m_connPanel->setStatusText("SmartLink is not connected");
        return;
    }

    const auto clients = buildDisconnectClients(info);
    if (clients.isEmpty()) {
        m_connPanel->setStatusText("No remote clients to disconnect");
        statusBar()->showMessage("No remote clients are currently reported for that radio.", 4000);
        return;
    }

    ClientDisconnectDialog dialog(clients,
                                  RadioModel::maxSlicesForModel(info.model),
                                  this,
                                  ClientDisconnectDialog::Mode::RemoteClientDisconnect);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const QList<quint32> handles = dialog.selectedHandles();
    if (handles.isEmpty())
        return;

    m_smartLink.disconnectRadioClients(info.serial, handles);
    m_connPanel->setStatusText("Disconnect request sent");
    statusBar()->showMessage("Remote client disconnect request sent through SmartLink.", 4000);
}

void MainWindow::startWanRadioConnect(const WanRadioInfo& info)
{
    QList<quint32> disconnectHandles;
    if (!confirmClientSlotAvailability(info, &disconnectHandles)) {
        m_connPanel->setStatusText("Connection canceled");
        setPanadapterConnectionAnimation(false);
        return;
    }

    m_userDisconnected = false;
    m_radioModel.setKnownGuiClients(splitClientField(info.guiClientHandles),
                                    splitClientField(info.guiClientPrograms),
                                    splitClientField(info.guiClientStations),
                                    splitClientField(info.guiClientIps),
                                    splitClientField(info.guiClientHosts));
    m_radioModel.setPendingClientDisconnects(disconnectHandles);
    m_connPanel->setStatusText("Requesting SmartLink connection…");
    setPanadapterConnectionAnimation(true, "Connecting to remote radio…");
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
        auto* ps = m_radioModel.panStream();
        QMetaObject::invokeMethod(ps, [ps, info, radioUdpPort]() {
            ps->startWan(QHostAddress(info.publicIp), radioUdpPort);
        }, Qt::BlockingQueuedConnection);
        udpPort = ps->localPort();
    }
    qDebug() << "MainWindow: pre-bound UDP port" << udpPort << "for WAN hole punch";
    auto requestSmartLinkConnect = [this, serial = info.serial, udpPort] {
        if (serial != m_pendingWanRadio.serial)
            return;
        m_smartLink.requestConnect(serial, udpPort);
    };
    if (!disconnectHandles.isEmpty()) {
        m_smartLink.disconnectRadioClients(info.serial, disconnectHandles);
        QTimer::singleShot(350, this, requestSmartLinkConnect);
    } else {
        requestSmartLinkConnect();
    }
}

void MainWindow::showForcedDisconnectDialog(bool wasWan,
                                            const RadioInfo& radioInfo,
                                            const WanRadioInfo& wanInfo)
{
    if (m_reconnectDlg) {
        m_reconnectDlg->close();
        m_reconnectDlg->deleteLater();
        m_reconnectDlg = nullptr;
    }

    auto* dialog = new QDialog(this);
    m_reconnectDlg = dialog;
    dialog->setWindowTitle(tr("Radio Disconnected"));
    dialog->setModal(true);
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setWindowFlags(dialog->windowFlags() & ~Qt::WindowContextHelpButtonHint);
    dialog->setFixedWidth(460);
    dialog->setStyleSheet(
        "QDialog { background: #0f0f1a; }"
        "QFrame#forcedDisconnectHeader {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #103626, stop:1 #10283a);"
        "  border-bottom: 1px solid #00b4d8;"
        "}"
        "QLabel { color: #c8d8e8; background: transparent; }"
        "QLabel#eyebrow { color: #40ff80; background: transparent; font-weight: bold; }"
        "QLabel#title { color: #ffffff; background: transparent; font-size: 18px; font-weight: bold; }"
        "QLabel#body { color: #c8d8e8; background: transparent; }"
        "QPushButton {"
        "  background: #1a2a3a;"
        "  border: 1px solid #304050;"
        "  border-radius: 3px;"
        "  color: #c8d8e8;"
        "  padding: 7px 16px;"
        "}"
        "QPushButton:hover { background: #203040; border-color: #00b4d8; }"
        "QPushButton#primaryButton {"
        "  background: #00b4d8;"
        "  border-color: #00c8f0;"
        "  color: #0f0f1a;"
        "  font-weight: bold;"
        "}"
        "QPushButton#primaryButton:hover { background: #00c8f0; }");

    auto* outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* header = new QFrame(dialog);
    header->setObjectName("forcedDisconnectHeader");
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(18, 14, 18, 14);
    headerLayout->setSpacing(4);

    auto* eyebrow = new QLabel(tr("CONNECTION ENDED"), header);
    eyebrow->setObjectName("eyebrow");
    headerLayout->addWidget(eyebrow);

    auto* title = new QLabel(tr("Disconnected by another client"), header);
    title->setObjectName("title");
    headerLayout->addWidget(title);
    outer->addWidget(header);

    auto* content = new QWidget(dialog);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 16, 18, 18);
    layout->setSpacing(14);

    auto* body = new QLabel(
        tr("Another client requested this AetherSDR session to disconnect. "
           "Automatic reconnect has been paused so the other operator can use the radio safely."),
        content);
    body->setObjectName("body");
    body->setWordWrap(true);
    layout->addWidget(body);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);

    auto* quit = new QPushButton(tr("Quit"), content);
    quit->setCursor(Qt::PointingHandCursor);
    quit->setMinimumHeight(34);
    buttons->addWidget(quit);

    auto* reconnect = new QPushButton(tr("Reconnect"), content);
    reconnect->setObjectName("primaryButton");
    reconnect->setCursor(Qt::PointingHandCursor);
    reconnect->setMinimumHeight(34);
    buttons->addWidget(reconnect);
    layout->addLayout(buttons);
    outer->addWidget(content);

    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (m_reconnectDlg == dialog)
            m_reconnectDlg = nullptr;
    });

    connect(quit, &QPushButton::clicked, this, [this, dialog] {
        m_userDisconnected = true;
        if (m_reconnectDlg == dialog)
            m_reconnectDlg = nullptr;
        dialog->close();
        dialog->deleteLater();
        QApplication::quit();
    });

    connect(reconnect, &QPushButton::clicked, this, [this, dialog, wasWan, radioInfo, wanInfo] {
        if (m_reconnectDlg == dialog)
            m_reconnectDlg = nullptr;
        dialog->close();
        dialog->deleteLater();

        m_userDisconnected = false;
        m_connPanel->setStatusText("Reconnecting…");
        setPanadapterConnectionAnimation(true, "Reconnecting to radio…");

        QTimer::singleShot(300, this, [this, wasWan, radioInfo, wanInfo] {
            if (wasWan && !wanInfo.serial.isEmpty()) {
                startWanRadioConnect(wanInfo);
                return;
            }

            if (radioInfo.address.isNull()) {
                setPanadapterConnectionAnimation(false);
                m_connPanel->setStatusText("Select a radio to reconnect");
                m_connPanel->show();
                return;
            }

            QList<quint32> disconnectHandles;
            if (!confirmClientSlotAvailability(radioInfo, &disconnectHandles)) {
                m_userDisconnected = true;
                m_connPanel->setStatusText("Connection canceled");
                setPanadapterConnectionAnimation(false);
                return;
            }

            m_radioModel.setPendingClientDisconnects(disconnectHandles);
            m_radioModel.connectToRadio(radioInfo);
        });
    });

    dialog->adjustSize();
    if (QScreen* screen = windowHandle() ? windowHandle()->screen() : QApplication::primaryScreen()) {
        const QRect area = screen->availableGeometry();
        dialog->move(area.center() - dialog->rect().center());
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QString("AetherSDR v%1").arg(QCoreApplication::applicationVersion()));
    setWindowIcon(QIcon(":/icon.png"));
    setMinimumSize(1024, 400);
    resize(1400, 800);

    // Apply frameless flag before first show() so the window is created
    // without chrome from the start — avoids the flash + re-create that
    // setWindowFlags() after show would cause (#framleess via View menu).
    // Default ON: TitleBar provides drag, double-click-maximize, and the
    // min/max/close trio at the far right.  View → Frameless Window can
    // still toggle it off as an escape hatch.
    {
        auto& s = AppSettings::instance();
        // One-shot migration: existing installs have FramelessWindow=False
        // saved from when frameless was opt-in.  Force ON for the
        // transition so users see the new chrome by default; the View
        // menu toggle still lets them flip it off afterwards.
        if (!s.contains("FramelessMigratedV0823")) {
            s.setValue("FramelessWindow", "True");
            s.setValue("FramelessMigratedV0823", "True");
            s.save();
        }
        if (s.value("FramelessWindow", "True").toString() == "True")
            setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    }

    applyDarkTheme();

    // Audio worker thread (#502) — AudioEngine runs on its own thread so
    // audio processing never competes with paintEvent for main thread CPU.
    m_audioThread = new QThread(this);
    m_audioThread->setObjectName("AudioEngine");
    m_audio = new AudioEngine;  // no parent — will be moved to thread
    m_audio->setRxBoost(
        AppSettings::instance().value("AudioBoost", "False").toString() == "True");
    m_audio->setRxBufferCapMs(
        AppSettings::instance().value("AudioBufferMs", "200").toInt());
    m_audio->moveToThread(m_audioThread);
    m_audioThread->start();

    // QSO audio recorder (#1297) — lives on main thread, audio feeds are thread-safe
    m_qsoRecorder = new QsoRecorder(this);
    connect(m_qsoRecorder, &QsoRecorder::playbackAudio,
            m_audio, &AudioEngine::feedDecodedSpeech);
    // During playback, block live RX audio from entering the buffer
    connect(m_qsoRecorder, &QsoRecorder::muteRxRequested, this, [this](bool mute) {
        if (mute) {
            disconnect(m_radioModel.panStream(), &PanadapterStream::audioDataReady,
                       m_audio, &AudioEngine::feedAudioData);
        } else {
            connect(m_radioModel.panStream(), &PanadapterStream::audioDataReady,
                    m_audio, &AudioEngine::feedAudioData);
        }
    });

    // PUDU TX monitor — captures post-PooDoo TX int16 audio, plays
    // back through the RX sink so the user can hear what their chain
    // is producing without keying the radio.  Registered with
    // AudioEngine so its tap hook picks it up on the audio thread.
    m_puduMonitor = new ClientPuduMonitor(this);
    m_audio->setTxPostDspMonitor(m_puduMonitor);

    // Local CW sidetone — every key source (serial, MIDI, TCI, CWX, HID)
    // funnels through RadioModel::sendCwKey/sendCwPaddle, which emits
    // cwKeyDownChanged.  Auto-queued connection so the audio thread sees
    // the state change via atomic without any blocking.
    connect(&m_radioModel, &RadioModel::cwKeyDownChanged,
            this, [this](bool down) {
        if (m_audio && m_audio->cwSidetone())
            m_audio->cwSidetone()->setKeyDown(down);
    });
    // Monitor owns a dedicated QAudioSink in pull mode — no
    // feedDecodedSpeech routing, no timer pacing.  Keeps playback
    // glitch-free on macOS/Windows where QTimer jitter was starving
    // the shared RX sink.
    // Disconnect live RX audio while the monitor is recording or
    // playing so the user hears ONLY the captured PooDoo audio.
    // Mirrors QsoRecorder's muteRxRequested handling — merely setting
    // the sink volume to 0 would mute our playback too.
    connect(m_puduMonitor, &ClientPuduMonitor::muteRxRequested,
            this, [this](bool mute) {
        if (mute) {
            disconnect(m_radioModel.panStream(),
                       &PanadapterStream::audioDataReady,
                       m_audio, &AudioEngine::feedAudioData);
        } else {
            connect(m_radioModel.panStream(),
                    &PanadapterStream::audioDataReady,
                    m_audio, &AudioEngine::feedAudioData);
        }
    });

    // Band plan manager — must be created before buildMenuBar() which references it
    m_bandPlanMgr = new BandPlanManager(this);
    m_bandPlanMgr->loadPlans();

    buildMenuBar();
    buildUI();
    registerShortcutActions();

    if (auto* wave = m_appletPanel ? m_appletPanel->waveApplet() : nullptr) {
        connect(m_audio, &AudioEngine::scopeSamplesReady,
                wave, &WaveApplet::appendScopeSamples,
                Qt::QueuedConnection);
        connect(m_audio, &AudioEngine::radioTransmittingChanged,
                wave, &WaveApplet::setTransmitting,
                Qt::QueuedConnection);
    }

    m_paTempUseFahrenheit =
        AppSettings::instance().value(kPaTempUnitSettingKey, "Fahrenheit").toString() != "Celsius";
    updatePaTempLabel();

    // DXCC spot coloring (#330)
    m_dxccProvider.loadCtyDat(":/cty.dat");
    {
        auto& s = AppSettings::instance();
        m_dxccProvider.setEnabled(s.value("IsDxccColoringEnabled", "False").toString() == "True");
        m_dxccProvider.colorNewDxcc = QColor(s.value("DxccColorNewEntity", "#FF3030").toString());
        m_dxccProvider.colorNewBand = QColor(s.value("DxccColorNewBand", "#FF8C00").toString());
        m_dxccProvider.colorNewMode = QColor(s.value("DxccColorNewMode", "#FFD700").toString());
        m_dxccProvider.colorWorked  = QColor(s.value("DxccColorWorked", "#606060").toString());
        const QString adifPath = s.value("DxccAdifFilePath", "").toString();
        if (!adifPath.isEmpty()) {
            m_dxccProvider.importAdifFile(adifPath);
            // Always watch the file so spot colours update automatically when
            // the user exports a new log — no manual reload step needed.
            m_dxccProvider.setAutoReload(true, adifPath);
        }
    }
    connect(&m_dxccProvider, &DxccColorProvider::importFinished,
            this, [this](int, int) { m_radioModel.spotModel().refresh(); });

    // Install event filter on the application to intercept Space PTT
    // before child widgets (buttons, combos) consume the key event.
    qApp->installEventFilter(this);

    // Ctrl+M toggle — must be a QShortcut on MainWindow, not a menu action,
    // because the menu bar is hidden in minimal mode.
    auto* minimalShortcut = new QShortcut(QKeySequence("Ctrl+M"), this);
    connect(minimalShortcut, &QShortcut::activated, this, [this]() {
        bool next = !m_minimalMode;
        // Sync the menu action (with blocker to avoid double-toggle)
        if (m_minimalModeAction) {
            QSignalBlocker b(m_minimalModeAction);
            m_minimalModeAction->setChecked(next);
        }
        toggleMinimalMode(next);
    });

    // Restore minimal mode if it was active on last exit
    if (AppSettings::instance().value("MinimalModeEnabled", "False").toString() == "True")
        toggleMinimalMode(true);

    // ── Wire up discovery ──────────────────────────────────────────────────
    connect(&m_discovery, &RadioDiscovery::radioDiscovered,
            m_connPanel, &ConnectionPanel::onRadioDiscovered);
    connect(&m_discovery, &RadioDiscovery::radioUpdated,
            m_connPanel, &ConnectionPanel::onRadioUpdated);
    connect(&m_discovery, &RadioDiscovery::radioUpdated,
            this, [this](const RadioInfo& info) {
        if (!m_radioModel.isConnected() || m_radioModel.serial() != info.serial)
            return;

        m_radioModel.mergeKnownGuiClients(info.guiClientHandles,
                                          info.guiClientPrograms,
                                          info.guiClientStations,
                                          info.guiClientIps,
                                          info.guiClientHosts);
    });
    connect(&m_discovery, &RadioDiscovery::radioLost,
            m_connPanel, &ConnectionPanel::onRadioLost);
    connect(m_connPanel, &ConnectionPanel::retryDiscoveryRequested, this, [this] {
        m_connPanel->setStatusText("Searching your local network…");
        if (m_titleBar) m_titleBar->setDiscovering(true);
        m_discovery.stopListening();
        m_discovery.startListening();
    });
    connect(m_connPanel, &ConnectionPanel::networkDiagnosticsRequested,
            this, &MainWindow::showNetworkDiagnosticsDialog);

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
        QList<quint32> disconnectHandles;
        if (!confirmClientSlotAvailability(info, &disconnectHandles)) {
            m_connPanel->setStatusText("Connection canceled");
            setPanadapterConnectionAnimation(false);
            return;
        }
        m_radioModel.setPendingClientDisconnects(disconnectHandles);
        m_connPanel->setStatusText("Connecting…");
        m_userDisconnected = false;
        setPanadapterConnectionAnimation(true, "Connecting to radio…");
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
            QList<quint32> disconnectHandles;
            if (!confirmClientSlotAvailability(info, &disconnectHandles)) {
                m_userDisconnected = true;
                m_connPanel->setStatusText("Connection canceled");
                setPanadapterConnectionAnimation(false);
                return;
            }
            m_radioModel.setPendingClientDisconnects(disconnectHandles);
            qDebug() << "Auto-connecting to" << info.displayName();
            m_connPanel->setStatusText("Auto-connecting…");
            setPanadapterConnectionAnimation(true, "Connecting to radio…");
            m_radioModel.connectToRadio(info);
        }
    });
    connect(m_connPanel, &ConnectionPanel::disconnectRequested,
            this, [this]{
        m_userDisconnected = true;
        setPanadapterConnectionAnimation(false);
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
        startWanRadioConnect(info);
    });
    connect(m_connPanel, &ConnectionPanel::wanDisconnectClientsRequested,
            this, [this](const WanRadioInfo& info) {
        disconnectWanRadioClients(info);
    });
    connect(&m_smartLink, &SmartLinkClient::radioListReceived,
            this, [this](const QList<WanRadioInfo>& radios) {
        if (!m_radioModel.isConnected() || !m_radioModel.isWan())
            return;

        for (const auto& info : radios) {
            if (info.serial != m_pendingWanRadio.serial)
                continue;

            m_radioModel.mergeKnownGuiClients(splitClientField(info.guiClientHandles),
                                              splitClientField(info.guiClientPrograms),
                                              splitClientField(info.guiClientStations),
                                              splitClientField(info.guiClientIps),
                                              splitClientField(info.guiClientHosts));
            break;
        }
    });

    // SmartLink server says radio is ready — connect via TLS
    connect(&m_smartLink, &SmartLinkClient::connectReady,
            this, [this](const QString& handle, const QString& serial) {
        if (serial != m_pendingWanRadio.serial) return;
        m_connPanel->setStatusText("TLS connecting to radio…");
        setPanadapterConnectionAnimation(true, "Connecting to remote radio…");
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
        setPanadapterConnectionAnimation(false);
    });
    connect(&m_wanConnection, &WanConnection::errorOccurred, this, [this](const QString& err) {
        m_connPanel->setStatusText("SmartLink error: " + err);
        if (!m_reconnectDlg)
            setPanadapterConnectionAnimation(false);
    });

    // ── DX Cluster — forward parsed spots to radio ──────────────────────
    // ── Spot clients on worker thread ─────────────────────────────────────
    m_dxCluster = new DxClusterClient;
    m_rbnClient = new DxClusterClient;
    m_rbnClient->setLogFileName("rbn.log");
    m_wsjtxClient = new WsjtxClient;
    m_spotCollectorClient = new SpotCollectorClient;
    m_potaClient = new PotaClient;
#ifdef HAVE_WEBSOCKETS
    m_freedvClient = new FreeDvClient;
#endif
#ifdef HAVE_MQTT
    m_mqttClient = new MqttClient(this);
    m_appletPanel->mqttApplet()->setMqttClient(m_mqttClient);

    connect(m_appletPanel->mqttApplet(), &MqttApplet::connectRequested,
            this, [this](const QString& host, quint16 port,
                         const QString& user, const QString& pass,
                         const QStringList& topics,
                         bool useTls, const QString& caFile) {
        m_mqttClient->connectToBroker(host, port, user, pass, useTls, caFile);
        for (const QString& t : topics) {
            m_mqttClient->subscribe(t);
        }
    });
    connect(m_appletPanel->mqttApplet(), &MqttApplet::disconnectRequested,
            this, [this] { m_mqttClient->disconnect(); });

    // MQTT → panadapter overlay display
    connect(m_appletPanel->mqttApplet(), &MqttApplet::displayValueChanged,
            this, [this](const QString& key, const QString& value) {
        if (auto* sw = m_panStack->activeSpectrum()) {
            sw->setMqttDisplayValue(key, value);
        }
    });
    connect(m_appletPanel->mqttApplet(), &MqttApplet::displayCleared,
            this, [this] {
        if (auto* sw = m_panStack->activeSpectrum()) {
            sw->clearMqttDisplay();
        }
    });
#endif

    m_spotThread = new QThread(this);
    m_spotThread->setObjectName("SpotClients");
    m_dxCluster->moveToThread(m_spotThread);
    m_rbnClient->moveToThread(m_spotThread);
    m_wsjtxClient->moveToThread(m_spotThread);
    m_spotCollectorClient->moveToThread(m_spotThread);
    m_potaClient->moveToThread(m_spotThread);
#ifdef HAVE_WEBSOCKETS
    m_freedvClient->moveToThread(m_spotThread);
#endif
    m_spotThread->start();

    // ── HF Propagation Forecast ────────────────────────────────────────────
    m_propForecast = new PropForecastClient(this);
    connect(m_propForecast, &PropForecastClient::forecastUpdated,
            this, [this](const PropForecast& fc) {
        for (PanadapterApplet* applet : m_panStack->allApplets()) {
            applet->spectrumWidget()->setPropForecast(fc.kIndex, fc.aIndex, fc.sfi);
        }
    });
    // Restore persisted setting — timer only arms if enabled
    if (AppSettings::instance().value("PropForecastEnabled", "False").toString() == "True") {
        m_propForecast->setEnabled(true);
    }

    // ── Spot forwarding: dedup + batch queue + 1/sec flush ────────────────

    // Dedup helper — returns true if spot should be skipped
    auto isDuplicateSpot = [this](const DxSpot& spot) -> bool {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        auto& as = AppSettings::instance();
        int lifetimeMs;
        if (spot.lifetimeSec > 0)
            lifetimeMs = spot.lifetimeSec * 1000;                      // source-provided
        else if (spot.source == "WSJT-X")
            lifetimeMs = as.value("WsjtxSpotLifetime", 120).toInt() * 1000;
        else if (spot.source == "FreeDV")
            lifetimeMs = as.value("FreeDvSpotLifetime", 120).toInt() * 1000;  // HAVE_WEBSOCKETS
        else
        {
            int sec = as.value("DxClusterSpotLifetimeSec", 0).toInt();
            if (sec <= 0) sec = as.value("DxClusterSpotLifetime", 30).toInt() * 60;
            lifetimeMs = sec * 1000;
        }
        auto it = m_spotDedup.find(spot.dxCall);
        if (it != m_spotDedup.end()) {
            bool sameFreq = std::abs(it->freqMhz - spot.freqMhz) < 0.001;
            bool expired = (now - it->addedMs) > lifetimeMs;
            if (sameFreq && !expired)
                return true;
        }
        m_spotDedup[spot.dxCall] = {spot.freqMhz, now};
        return false;
    };

    auto spotLifetimeSeconds = [](const DxSpot& spot, const QString& source) {
        if (spot.lifetimeSec > 0)
            return spot.lifetimeSec;

        auto& as = AppSettings::instance();
        if (source == "WSJT-X")
            return as.value("WsjtxSpotLifetime", 120).toInt();
        if (source == "FreeDV")
            return as.value("FreeDvSpotLifetime", 120).toInt();

        int sec = as.value("DxClusterSpotLifetimeSec", 0).toInt();
        if (sec <= 0)
            sec = as.value("DxClusterSpotLifetime", 30).toInt() * 60;
        return sec;
    };

    auto spotColorForSource = [](const DxSpot& spot, const QString& source) {
        QString spotColor = spot.color;
        if (spotColor.isEmpty()) {
            auto& as = AppSettings::instance();
            if (source == "DXCluster")
                spotColor = as.value("DxClusterSpotColor", "#D2B48C").toString();
            else if (source == "RBN")
                spotColor = as.value("RbnSpotColor", "#4488FF").toString();
            else if (source == "SpotCollector")
                spotColor = as.value("SpotCollectorSpotColor", "#FFD700").toString();
            else if (source == "FreeDV")
                spotColor = as.value("FreeDvSpotColor", "#FF8C00").toString();  // HAVE_WEBSOCKETS
        }
        if (spotColor.length() == 7)
            spotColor = "#FF" + spotColor.mid(1);
        return spotColor;
    };

    auto addPassiveSpotToModel = [this](const DxSpot& spot, const QString& source,
                                        const QString& color, int lifetimeSec) {
        if (spot.dxCall.trimmed().isEmpty() || spot.freqMhz <= 0.0)
            return;

        QMap<QString, QString> kvs;
        kvs["callsign"] = QString(spot.dxCall).replace(' ', QChar(0x7f));
        kvs["rx_freq"] = QString::number(spot.freqMhz, 'f', 6);
        kvs["tx_freq"] = QString::number(spot.freqMhz, 'f', 6);
        kvs["source"] = source;
        kvs["spotter_callsign"] = spot.spotterCall;
        kvs["lifetime_seconds"] = QString::number(lifetimeSec);
        kvs["timestamp"] = QString::number(QDateTime::currentSecsSinceEpoch());
        if (!spot.comment.isEmpty())
            kvs["comment"] = QString(spot.comment).replace(' ', QChar(0x7f));
        if (!color.isEmpty())
            kvs["color"] = color;

        const int spotId = m_nextPassiveSpotId--;
        m_radioModel.spotModel().applySpotStatus(spotId, kvs);
        if (lifetimeSec > 0) {
            const qint64 expiresAt = QDateTime::currentMSecsSinceEpoch()
                                   + qint64(lifetimeSec) * 1000;
            m_passiveSpotExpiryMs.insert(spotId, expiresAt);
        }
    };

    // Build spot add command and queue for batch send
    auto queueSpotCmd = [this, isDuplicateSpot, spotLifetimeSeconds,
                         spotColorForSource, addPassiveSpotToModel]
                        (const DxSpot& spot, const QString& source) {
        if (!m_radioModel.isConnected()) return;
        if (isDuplicateSpot(spot)) return;
        const int lifetimeSec = spotLifetimeSeconds(spot, source);
        const QString spotColor = spotColorForSource(spot, source);
        if (!SpotCommandPolicy::shouldSendSpotAddCommands()) {
            addPassiveSpotToModel(spot, source, spotColor, lifetimeSec);
            return;
        }

        QString call = QString(spot.dxCall).replace(' ', QChar(0x7f));
        QString freq = QString::number(spot.freqMhz, 'f', 6);
        QString cmd = "spot add callsign=" + call + " rx_freq=" + freq
                     + " tx_freq=" + freq
                     + " source=" + source
                     + " spotter_callsign=" + spot.spotterCall
                     + " lifetime_seconds=" + QString::number(lifetimeSec);
        if (!spot.comment.isEmpty())
            cmd += " comment=" + QString(spot.comment).replace(' ', QChar(0x7f));
        if (!spotColor.isEmpty())
            cmd += " color=" + spotColor;
        m_spotCmdBatch.append(cmd);
    };

    // Flush batch: send queued spot commands to radio (1/sec)
    auto* spotCmdTimer = new QTimer(this);
    spotCmdTimer->start(1000);
    connect(spotCmdTimer, &QTimer::timeout, this, [this] {
        if (m_spotCmdBatch.isEmpty() || !m_radioModel.isConnected()) return;
        if (!SpotCommandPolicy::shouldSendSpotAddCommands()) {
            m_spotCmdBatch.clear();
            return;
        }
        // Send up to RbnRateLimit commands per tick
        int limit = AppSettings::instance().value("RbnRateLimit", 10).toInt();
        int count = std::min(static_cast<int>(m_spotCmdBatch.size()), limit);
        for (int i = 0; i < count; ++i)
            m_radioModel.sendCommand(m_spotCmdBatch[i]);
        m_spotCmdBatch.remove(0, count);
    });

    auto* passiveSpotExpiryTimer = new QTimer(this);
    passiveSpotExpiryTimer->start(1000);
    connect(passiveSpotExpiryTimer, &QTimer::timeout, this, [this] {
        if (m_passiveSpotExpiryMs.isEmpty())
            return;

        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        QVector<int> expired;
        for (auto it = m_passiveSpotExpiryMs.cbegin(); it != m_passiveSpotExpiryMs.cend(); ++it) {
            if (it.value() <= now)
                expired.append(it.key());
        }

        for (int spotId : expired) {
            m_passiveSpotExpiryMs.remove(spotId);
            m_radioModel.spotModel().removeSpot(spotId);
        }
    });
    connect(&m_radioModel.spotModel(), &SpotModel::spotsCleared,
            this, [this] { m_passiveSpotExpiryMs.clear(); });

    connect(m_dxCluster, &DxClusterClient::spotReceived,
            this, [queueSpotCmd](const DxSpot& spot) {
        queueSpotCmd(spot, "DXCluster");
    });

    connect(m_rbnClient, &DxClusterClient::spotReceived,
            this, [queueSpotCmd](const DxSpot& spot) {
        queueSpotCmd(spot, "RBN");
    });

    connect(m_wsjtxClient, &WsjtxClient::spotReceived,
            this, [this, isDuplicateSpot, spotLifetimeSeconds, addPassiveSpotToModel](const DxSpot& spot) {
        if (!m_radioModel.isConnected()) return;
        if (isDuplicateSpot(spot)) return;

        auto& as = AppSettings::instance();
        const QString& msg = spot.comment;
        bool isCQ = msg.startsWith("CQ ");
        bool isPOTA = msg.contains("CQ POTA");
        bool isCallingMe = false;
        {
            QString myCall = as.value("DxClusterCallsign").toString();
            if (!myCall.isEmpty()) {
                QStringList parts = msg.split(' ', Qt::SkipEmptyParts);
                if (parts.size() >= 2 && parts[0] == myCall)
                    isCallingMe = true;
            }
        }

        // Filter
        bool fCQ   = as.value("WsjtxFilterCQ", "True").toString() == "True";
        bool fPOTA = as.value("WsjtxFilterPOTA", "True").toString() == "True";
        bool fMe   = as.value("WsjtxFilterCallingMe", "True").toString() == "True";
        bool anyFilter = fCQ || fPOTA || fMe;
        if (anyFilter) {
            bool pass = false;
            if (fCQ && isCQ) pass = true;
            if (fPOTA && isPOTA) pass = true;
            if (fMe && isCallingMe) pass = true;
            if (!pass) return;
        }

        // Color
        DxSpot colored = spot;
        if (isCallingMe)
            colored.color = as.value("WsjtxColorCallingMe", "#FF0000").toString();
        else if (isPOTA)
            colored.color = as.value("WsjtxColorPOTA", "#00FFFF").toString();
        else if (isCQ)
            colored.color = as.value("WsjtxColorCQ", "#00FF00").toString();
        else
            colored.color = as.value("WsjtxColorDefault", "#FFFFFF").toString();

        // Compute alpha from SNR: -24→64, 0→192, +10→255 (linear interpolation)
        int alpha;
        if (colored.snr <= -24)
            alpha = 64;
        else if (colored.snr >= 10)
            alpha = 255;
        else if (colored.snr <= 0)
            alpha = 64 + (colored.snr + 24) * (192 - 64) / 24;   // -24..0 → 64..192
        else
            alpha = 192 + colored.snr * (255 - 192) / 10;         // 0..+10 → 192..255

        // Convert to #AARRGGBB format for radio
        if (colored.color.length() == 7)  // #RRGGBB → #AARRGGBB
            colored.color = QString("#%1%2").arg(alpha, 2, 16, QChar('0')).arg(colored.color.mid(1));

        QString call = QString(colored.dxCall).replace(' ', QChar(0x7f));
        QString freq = QString::number(colored.freqMhz, 'f', 6);
        QString cmd = "spot add callsign=" + call + " rx_freq=" + freq
                     + " tx_freq=" + freq
                     + " source=WSJT-X"
                     + " spotter_callsign=" + colored.spotterCall
                     + " lifetime_seconds=" + QString::number(
                           spotLifetimeSeconds(colored, "WSJT-X"));
        if (!colored.comment.isEmpty())
            cmd += " comment=" + QString(colored.comment).replace(' ', QChar(0x7f));
        if (!colored.color.isEmpty())
            cmd += " color=" + colored.color;
        if (!SpotCommandPolicy::shouldSendSpotAddCommands()) {
            addPassiveSpotToModel(colored, "WSJT-X", colored.color,
                                  spotLifetimeSeconds(colored, "WSJT-X"));
            return;
        }
        m_spotCmdBatch.append(cmd);
    });

    connect(m_spotCollectorClient, &SpotCollectorClient::spotReceived,
            this, [queueSpotCmd](const DxSpot& spot) {
        queueSpotCmd(spot, "SpotCollector");
    });

    connect(m_potaClient, &PotaClient::spotReceived,
            this, [queueSpotCmd](const DxSpot& spot) {
        queueSpotCmd(spot, "POTA");
    });

#ifdef HAVE_WEBSOCKETS
    connect(m_freedvClient, &FreeDvClient::spotReceived,
            this, [queueSpotCmd](const DxSpot& spot) {
        queueSpotCmd(spot, "FreeDV");
    });
#endif

    // ── Wire up radio model ────────────────────────────────────────────────
    connect(&m_radioModel, &RadioModel::connectionStateChanged,
            this, &MainWindow::onConnectionStateChanged);
    connect(&m_radioModel, &RadioModel::connectionError,
            this, &MainWindow::onConnectionError);
    connect(&m_radioModel, &RadioModel::forcedDisconnectRequested,
            this, [this] {
        const bool wasWan = m_radioModel.isWan();
        const RadioInfo radioInfo = m_radioModel.lastRadioInfo();
        const WanRadioInfo wanInfo = m_pendingWanRadio;

        m_userDisconnected = true;
        m_connPanel->setStatusText("Disconnected by another client");
        setPanadapterConnectionAnimation(false);
        showForcedDisconnectDialog(wasWan, radioInfo, wanInfo);
    });
    connect(&m_radioModel, &RadioModel::sliceAdded,
            this, &MainWindow::onSliceAdded);
    connect(&m_radioModel, &RadioModel::sliceRemoved,
            this, &MainWindow::onSliceRemoved);
    connect(&m_radioModel, &RadioModel::memoryChanged,
            this, &MainWindow::syncMemorySpot);
    connect(&m_radioModel, &RadioModel::memoryRemoved,
            this, &MainWindow::removeMemorySpot);
    connect(&m_radioModel, &RadioModel::memoriesCleared,
            this, &MainWindow::clearMemorySpotFeed);
    connect(&m_radioModel, &RadioModel::memoryChanged,
            this, [this](int) { refreshMemoryBrowsePanel(); });
    connect(&m_radioModel, &RadioModel::memoryRemoved,
            this, [this](int) { refreshMemoryBrowsePanel(); });
    connect(&m_radioModel, &RadioModel::memoriesCleared,
            this, [this]() { refreshMemoryBrowsePanel(); });
    // Keep the MEM button target-slice badge in sync with slice topology
    // changes so the displayed letter always matches which slice a
    // save/recall will route to (#1781).  Active-slice changes are
    // handled inside setActiveSlice() itself since RadioModel has no
    // activeSliceChanged signal.
    connect(&m_radioModel, &RadioModel::sliceAdded,
            this, [this](SliceModel*) { refreshMemoryBrowsePanel(); });
    connect(&m_radioModel, &RadioModel::sliceRemoved,
            this, [this](int) { refreshMemoryBrowsePanel(); });
    connect(&m_radioModel, &RadioModel::panadapterLimitReached,
            this, [this](int limit, const QString& model) {
        statusBar()->showMessage(
            QString("%1 supports a maximum of %2 panadapters")
                .arg(model).arg(limit), 4000);
    });
    connect(&m_radioModel, &RadioModel::sliceCreateFailed,
            this, [this](int limit, const QString& model) {
        statusBar()->showMessage(
            QString("%1 supports a maximum of %2 slices across all connected clients")
                .arg(model).arg(limit), 4000);
    });
    connect(&m_radioModel.spotModel(), &SpotModel::spotsCleared,
            this, &MainWindow::rebuildMemorySpotFeed);

    // ── TX audio stream: set stream ID for DAX TX path ──────────────────
    // DAX TX audio is sent via PanadapterStream::sendToRadio() (the
    // registered VITA-49 socket).  We do NOT start a separate mic TX
    // stream — that would open a QAudioSource and an unregistered UDP
    // socket, wasting resources and corrupting the shared packet counter.
    // Route TX VITA-49 packets through the registered UDP socket
    connect(m_audio, &AudioEngine::txPacketReady,
            m_radioModel.panStream(), &PanadapterStream::sendToRadio);

    connect(&m_radioModel, &RadioModel::txAudioStreamReady,
            this, [this](quint32 streamId) {
        m_audio->setTxStreamId(streamId);
        // TX audio on remote_audio_tx always requires Opus (radio enforces compression=OPUS)
        m_audio->setOpusTxEnabled(true);
        qDebug() << "MainWindow: DAX TX stream ID set to" << Qt::hex << streamId;
        // Start PC audio TX if mic_selection is PC
        if (m_radioModel.transmitModel().micSelection() == "PC") {
            audioStartTx(m_radioModel.radioAddress(), 4991);
        }
    });
    connect(&m_radioModel, &RadioModel::remoteTxStreamReady,
            this, [this](quint32 streamId) {
        m_audio->setRemoteTxStreamId(streamId);
        // Radio always forces Opus for remote_audio_tx (confirmed v1.4.0.0)
        m_audio->setOpusTxEnabled(true);
        // Only the PC mic path needs local audio capture. For radio-side mic
        // selections, remote_audio_tx still exists for VOX/met_in_rx, but the
        // radio owns the actual input path. Starting QAudioSource here on
        // macOS pins Bluetooth output in telephony mode.
        if (m_radioModel.transmitModel().micSelection() == "PC") {
            // Restore PC mic gain from client-side settings (radio has no
            // hardware gain stage for PC input — client-authoritative)
            int gain = AppSettings::instance().value("PcMicGain", 100).toInt();
            m_audio->setPcMicGain(gain);
            if (!m_audio->isTxStreaming()) {
                audioStartTx(m_radioModel.radioAddress(), 4991);
            }
        } else if (m_audio->isTxStreaming()) {
            audioStopTx();
        }
        qDebug() << "MainWindow: remote audio TX stream ID set to" << Qt::hex << streamId;
    });
    // Start/stop PC audio TX when mic_selection changes
    connect(&m_radioModel.transmitModel(), &TransmitModel::micStateChanged,
            this, [this]() {
#ifdef Q_OS_MAC
        const bool allowBluetoothTelephonyOutput =
            m_radioModel.transmitModel().micSelection() == "PC";
        QMetaObject::invokeMethod(m_audio, [this, allowBluetoothTelephonyOutput]() {
            m_audio->setAllowBluetoothTelephonyOutput(allowBluetoothTelephonyOutput);
        }, Qt::QueuedConnection);
#endif
        if (m_radioModel.transmitModel().micSelection() == "PC") {
            // Restore PC mic gain from client-side settings
            int gain = AppSettings::instance().value("PcMicGain", 100).toInt();
            m_audio->setPcMicGain(gain);
            // Only start if TX stream ID is already assigned (avoid streamId=0)
            if (!m_audio->isTxStreaming() && m_audio->txStreamId() != 0) {
                audioStartTx(m_radioModel.radioAddress(), 4991);
            }
        } else {
            // Reset to full gain — radio handles hardware mic gain
            m_audio->setPcMicGain(100);
            audioStopTx();
        }
        // PooDoo Audio readiness indicator — turn the chain widget's
        // MIC endpoint green when the TX input is actually flowing
        // through the client DSP chain: mic source = PC AND radio
        // DAX TX is off.  Any other combination routes audio around
        // our DSP, so the green cue would mislead.  The TX pulse
        // is gated on the same readiness so it only fires when
        // PooDoo is actually doing something during transmit.
        if (m_appletPanel && m_appletPanel->clientChainApplet()) {
            const auto& tx = m_radioModel.transmitModel();
            const bool ready = (tx.micSelection() == "PC") && !tx.daxOn();
            m_appletPanel->clientChainApplet()->setMicInputReady(ready);
            m_appletPanel->clientChainApplet()->setTxActive(
                ready && tx.isTransmitting());

            // If the user pulls the plug on readiness mid-recording
            // (mic source away from PC, or DAX back on), stop the
            // recording — auto-play kicks in via recordingStopped.
            if (!ready && m_puduMonitor && m_puduMonitor->isRecording()) {
                m_puduMonitor->stopRecording();
            }
        }
    });
#ifdef Q_OS_MAC
    const bool allowBluetoothTelephonyOutput =
        m_radioModel.transmitModel().micSelection() == "PC";
    QMetaObject::invokeMethod(m_audio, [this, allowBluetoothTelephonyOutput]() {
        m_audio->setAllowBluetoothTelephonyOutput(allowBluetoothTelephonyOutput);
    }, Qt::QueuedConnection);
#endif
    // Sync PC mic gain directly from slider (radio ignores mic_level for PC input)
    connect(m_appletPanel->phoneCwApplet(), &PhoneCwApplet::micLevelChanged,
            this, [this](int level) {
        if (m_radioModel.transmitModel().micSelection() == "PC") {
            m_audio->setPcMicGain(level);
            auto& s = AppSettings::instance();
            s.setValue("PcMicGain", level);
            s.save();
        }
    });

    // Local CW sidetone — wire UI controls to the AudioEngine generator.
    // Initial state is loaded into the generator from AppSettings on first
    // connect; UI signals push subsequent changes live (atomic in DSP).
    {
        auto* pca = m_appletPanel->phoneCwApplet();
        connect(pca, &PhoneCwApplet::localSidetoneEnabledChanged,
                this, [this](bool on) {
            if (m_audio && m_audio->cwSidetone())
                m_audio->cwSidetone()->setEnabled(on);
        });
        connect(pca, &PhoneCwApplet::localSidetoneVolumeChanged,
                this, [this](int pct) {
            if (m_audio && m_audio->cwSidetone())
                m_audio->cwSidetone()->setVolume(pct / 100.0f);
        });
        connect(pca, &PhoneCwApplet::localSidetonePitchFollowChanged,
                this, [this](bool follow) {
            if (!m_audio || !m_audio->cwSidetone()) return;
            if (follow) {
                m_audio->cwSidetone()->setPitchHz(
                    static_cast<float>(m_radioModel.transmitModel().cwPitch()));
            } else {
                auto& s = AppSettings::instance();
                m_audio->cwSidetone()->setPitchHz(static_cast<float>(
                    s.value("CwLocalSidetonePitchHz", "600").toInt()));
            }
        });
        connect(pca, &PhoneCwApplet::localSidetonePitchChanged,
                this, [this](int hz) {
            // Manual pitch slider only — only applied when follow is off.
            auto& s = AppSettings::instance();
            const bool follow =
                s.value("CwLocalSidetonePitchFollow", "True").toString() == "True";
            if (!follow && m_audio && m_audio->cwSidetone())
                m_audio->cwSidetone()->setPitchHz(static_cast<float>(hz));
        });

        // Push persisted state into the generator at startup so the user's
        // saved enable/volume/pitch take effect without toggling the UI.
        if (m_audio && m_audio->cwSidetone()) {
            auto& s = AppSettings::instance();
            auto* gen = m_audio->cwSidetone();
            gen->setEnabled(
                s.value("CwLocalSidetoneEnabled", "False").toString() == "True");
            gen->setVolume(
                s.value("CwLocalSidetoneVolume", "50").toInt() / 100.0f);
            const bool follow =
                s.value("CwLocalSidetonePitchFollow", "True").toString() == "True";
            const int manualHz = s.value("CwLocalSidetonePitchHz", "600").toInt();
            gen->setPitchHz(static_cast<float>(
                follow ? m_radioModel.transmitModel().cwPitch() : manualHz));
        }

        // Pitch-follow: when the radio updates cw_pitch, propagate to the
        // generator if the user has follow enabled.  TransmitModel emits
        // phoneStateChanged on any CW property change including pitch —
        // cheap to re-pull on each since cwPitch() is just an int read.
        connect(&m_radioModel.transmitModel(), &TransmitModel::phoneStateChanged,
                this, [this]() {
            if (!m_audio || !m_audio->cwSidetone()) return;
            auto& ss = AppSettings::instance();
            const bool follow =
                ss.value("CwLocalSidetonePitchFollow", "True").toString() == "True";
            if (follow) {
                m_audio->cwSidetone()->setPitchHz(
                    static_cast<float>(m_radioModel.transmitModel().cwPitch()));
            }
        });

        // CWX local keyer — when the user fires text/macros via CWX, this
        // generates a matching dit-dah pattern locally and routes it
        // through the same sidetone generator.  Keeps in sync with the
        // radio's keyer because both run at the configured WPM.  Drift
        // tolerance is high — we're not transmitting, just providing the
        // operator with audible feedback of what they sent.
        m_cwxLocalKeyer = new CwxLocalKeyer(this);
        connect(&m_radioModel.cwxModel(), &CwxModel::transmissionRequested,
                m_cwxLocalKeyer, &CwxLocalKeyer::start);
        connect(&m_radioModel.cwxModel(), &CwxModel::transmissionCancelled,
                m_cwxLocalKeyer, &CwxLocalKeyer::stop);
        connect(m_cwxLocalKeyer, &CwxLocalKeyer::keyStateChanged,
                this, [this](bool down) {
            if (m_audio && m_audio->cwSidetone())
                m_audio->cwSidetone()->setKeyDown(down);
        });
    }

    // TX/RX transition → waterfall tile source switching
    connect(&m_radioModel.transmitModel(), &TransmitModel::moxChanged,
            this, [this](bool tx) {
        // Set transmitting on ALL spectrums (multi-pan aware).
        // Each spectrum's m_hasTxSlice flag determines whether it freezes.
        for (auto* pan : m_radioModel.panadapters()) {
            if (auto* sw = m_panStack ? m_panStack->spectrum(pan->panId()) : nullptr)
                sw->setTransmitting(tx);
        }
        if (!m_panStack && m_panApplet)
            m_panApplet->spectrumWidget()->setTransmitting(tx);
        // Keep TX audio source strictly aligned with the local MOX edge for all
        // modes (SSB + DAX). Waiting for interlock introduces audible lag.
        m_audio->setTransmitting(tx);
#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
        if (m_daxBridge)
            m_daxBridge->setTransmitting(tx);
#endif

        // TX indicator is driven by radioTransmittingChanged (see below) so that
        // it lights up for external PTT and Multi-Flex TX, not just local MOX.
        // On RX resume, native tiles will restart and m_hasNativeWaterfall
        // will be set again by the first arriving tile.
#ifdef HAVE_RADE
        if (m_radeSliceId >= 0 && m_radeEngine && m_radeEngine->isActive()) {
            m_audio->setRadeMode(tx);
            if (!tx) {
                m_radeEngine->resetTx();
            }
        }
#endif
#ifdef HAVE_SERIALPORT
        QMetaObject::invokeMethod(m_serialPort, [this, tx] { m_serialPort->setTransmitting(tx); });
#endif
    });

    // Interlock fallback gate:
    // we only consume TX-off here, as a safety net if local edge updates
    // are missed while interlock transitions.
    connect(&m_radioModel, &RadioModel::txAudioGateChanged,
            this, [this](bool tx) {
        if (!tx) {
            m_audio->setTransmitting(false);
#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
            if (m_daxBridge)
                m_daxBridge->setTransmitting(false);
#endif
        }
    });

    // Raw radio TX state: fired for every interlock state=TRANSMITTING regardless
    // of TX ownership. Used for DAX passthrough (#752) and the TX status bar
    // indicator — moxChanged is ownership-gated so it misses external PTT and
    // Multi-Flex TX from other clients.
    connect(&m_radioModel, &RadioModel::radioTransmittingChanged,
            this, [this](bool tx) {
        m_audio->setRadioTransmitting(tx);
        // S-Meter: use raw interlock state so Level/Compression modes work
        // during VOX/hardware CW without the effectiveTx power threshold (#877)
        m_appletPanel->sMeterWidget()->setTransmitting(tx);
        if (tx) {
            m_txIndicator->setStyleSheet(
                "QLabel { color: white; background: #c03030; font-weight: bold; "
                "font-size: 21px; border-radius: 4px; padding: 0px 1px; }");
        } else {
            m_txIndicator->setStyleSheet(
                "QLabel { color: rgba(255,255,255,128); font-weight: bold; "
                "font-size: 21px; }");
        }
    });

    // Sync show-TX-in-waterfall setting to all spectrum widgets
    auto syncShowTxWf = [this]() {
        bool show = m_radioModel.transmitModel().showTxInWaterfall();
        for (auto* pan : m_radioModel.panadapters()) {
            if (auto* sw = m_panStack ? m_panStack->spectrum(pan->panId()) : nullptr)
                sw->setShowTxInWaterfall(show);
        }
        if (!m_panStack && m_panApplet)
            m_panApplet->spectrumWidget()->setShowTxInWaterfall(show);
    };
    connect(&m_radioModel.transmitModel(), &TransmitModel::stateChanged,
            this, syncShowTxWf);

    // ── Panadapter stream → spectrum widget ───────────────────────────────
    // Route FFT/waterfall data to the correct SpectrumWidget by stream ID
    connect(m_radioModel.panStream(), &PanadapterStream::spectrumReady,
            this, [this](quint32 streamId, const QVector<float>& bins) {
        for (auto* pan : m_radioModel.panadapters()) {
            if (pan->panStreamId() == streamId) {
                if (auto* sw = m_panStack->spectrum(pan->panId())) {
                    sw->updateSpectrum(bins);
                    finishPanadapterConnectionAnimation();
                }
                return;
            }
        }
        // Fallback: active spectrum (covers "default" pan before radio connects)
        if (auto* sw = spectrum()) {
            sw->updateSpectrum(bins);
            finishPanadapterConnectionAnimation();
        }
    });
    connect(m_radioModel.panStream(), &PanadapterStream::waterfallRowReady,
            this, [this](quint32 streamId, const QVector<float>& bins,
                         double low, double high, quint32 tc) {
        for (auto* pan : m_radioModel.panadapters()) {
            if (pan->wfStreamId() == streamId) {
                const double panCenter = pan->centerMhz();
                if (XvtrPolicy::isWaterfallTileOutsidePan(low, high, panCenter)) {
                    // Only reinterpret non-overlapping tile ranges for real XVTR
                    // IF/RF translation. Ordinary HF pans can briefly see stale
                    // tile centers while dragging; shifting those corrupts rows.
                    const auto xvtrs = xvtrPolicyBandsFrom(m_radioModel.xvtrList());
                    const auto match = XvtrPolicy::matchWaterfallTileTransverterOffset(
                        low, high, panCenter, xvtrs);
                    bool hasXvtrSliceAntenna = false;
                    if (!match.matched) {
                        for (auto* slice : m_radioModel.slices()) {
                            if (!slice || slice->panId() != pan->panId())
                                continue;
                            if (slice->rxAntenna().startsWith(QStringLiteral("XVT"),
                                                               Qt::CaseInsensitive)) {
                                hasXvtrSliceAntenna = true;
                                break;
                            }
                        }
                    }
                    const auto mapped = XvtrPolicy::mapWaterfallTileRange(
                        low, high, panCenter, xvtrs, hasXvtrSliceAntenna);
                    logXvtrWaterfallDecision(streamId, pan->panId(), panCenter,
                                             low, high, mapped, match,
                                             hasXvtrSliceAntenna, xvtrs);
                    low = mapped.lowMhz;
                    high = mapped.highMhz;
                }
                if (auto* sw = m_panStack->spectrum(pan->panId())) {
                    sw->updateWaterfallRow(bins, low, high, tc);
                    finishPanadapterConnectionAnimation();
                }
                return;
            }
        }
        if (auto* sw = spectrum()) {
            sw->updateWaterfallRow(bins, low, high, tc);
            finishPanadapterConnectionAnimation();
        }
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
            auto* sw = spectrum();
            if (!sw) return;  // pan not yet available
            m_displaySettingsPushed = true;
            m_radioModel.setPanAverage(sw->fftAverage());
            m_radioModel.setPanFps(sw->fftFps());
            m_radioModel.setPanWeightedAverage(sw->fftWeightedAvg());
            m_radioModel.setWaterfallColorGain(sw->wfColorGain());
            m_radioModel.setWaterfallBlackLevel(sw->wfBlackLevel());
            m_radioModel.setWaterfallAutoBlack(sw->wfAutoBlack());
            int rate = sw->wfLineDuration();
            m_radioModel.setWaterfallLineDuration(rate);
            // Restore saved WNB and RF gain
            auto& s = AppSettings::instance();
            bool wnbOn = s.value(sw->settingsKey("DisplayWnbEnabled"), "False").toString() == "True";
            int wnbLevel = s.value(sw->settingsKey("DisplayWnbLevel"), "50").toInt();
            int rfGain = s.value(sw->settingsKey("DisplayRfGain"), "0").toInt();
            m_radioModel.setPanWnb(wnbOn);
            m_radioModel.setPanWnbLevel(wnbLevel);
            m_radioModel.setPanRfGain(rfGain);
            sw->setWnbActive(wnbOn);
            sw->setRfGain(rfGain);
            sw->overlayMenu()->setWnbState(wnbOn, wnbLevel);
            sw->overlayMenu()->setRfGain(rfGain);
            QString bgPath = s.value(sw->settingsKey("BackgroundImage")).toString();
            if (!bgPath.isEmpty())
                sw->setBackgroundImage(bgPath);
            int bgOpacity = s.value(sw->settingsKey("BackgroundOpacity"), "80").toInt();
            sw->setBackgroundOpacity(bgOpacity);
            // Nudge rate to force waterfall tile re-sync
            QTimer::singleShot(500, this, [this, rate]() {
                m_radioModel.setWaterfallLineDuration(rate + 1);
                m_radioModel.setWaterfallLineDuration(rate);
            });
        }
    });
    // NOTE: panadapterLevelChanged → spectrum()::setDbmRange has been removed.
    // Level updates are routed per-pan via PanadapterModel::levelChanged in
    // wirePanadapter() so that PanadapterModel's change-guard prevents stale
    // echo-backs from overwriting in-flight user changes.
    // ── Multi-panadapter lifecycle ──────────────────────────────────────────
    connect(&m_radioModel, &RadioModel::panadapterAdded,
            this, [this](PanadapterModel* pan) {
        // During layout application, applyLayout/createPansSequentially handles
        // applet creation and wiring — don't duplicate here.
        if (m_applyingLayout) return;

        // Skip if this pan already has an applet
        if (m_panStack->panadapter(pan->panId())) {
            if (auto* sw = m_panStack->spectrum(pan->panId())) {
                connect(pan, &PanadapterModel::infoChanged,
                        sw, &SpectrumWidget::setFrequencyRange);
                connect(pan, &PanadapterModel::levelChanged,
                        sw, &SpectrumWidget::setDbmRange);
                connect(pan, &PanadapterModel::wideChanged,
                        sw, &SpectrumWidget::setWideActive);
                sw->setWideActive(pan->wideActive());
            }
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
        setActivePanApplet(applet);
        wirePanadapter(applet);
        if (m_panadapterConnectionAnimationVisible) {
            applet->spectrumWidget()->setConnectionAnimationVisible(
                true, m_panadapterConnectionAnimationLabel);
        }
        connect(pan, &PanadapterModel::infoChanged,
                applet->spectrumWidget(), &SpectrumWidget::setFrequencyRange);
        // NOTE: levelChanged → setDbmRange is wired in wirePanadapter() above;
        // don't connect it here again or setDbmRange fires twice per level change.
        connect(pan, &PanadapterModel::rfGainInfoChanged,
                applet->spectrumWidget()->overlayMenu(),
                &SpectrumOverlayMenu::setRfGainRange);
        connect(pan, &PanadapterModel::rfGainChanged,
                this, [applet](int gain) {
            applet->spectrumWidget()->setRfGain(gain);
            applet->spectrumWidget()->overlayMenu()->setRfGain(gain);
        });

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

        // Tell PanadapterStream the ypixels for FFT bin→dBm conversion
        if (pan->panStreamId())
            m_radioModel.panStream()->setYPixels(pan->panStreamId(), ypix);

        qDebug() << "MainWindow: added panadapter applet for" << pan->panId();

        // Debounced layout restore: after all pans are added on connect,
        // rearrange to the saved layout (e.g. 2h instead of default vertical).
        if (!m_layoutRestoreTimer) {
            m_layoutRestoreTimer = new QTimer(this);
            m_layoutRestoreTimer->setSingleShot(true);
            m_layoutRestoreTimer->setInterval(1000);
            connect(m_layoutRestoreTimer, &QTimer::timeout, this, [this]() {
                // The radio restores pans from the GUIClientID session.
                // Accept whatever the radio gives and arrange based on count.
                int panCount = m_panStack->count();
                if (panCount <= 1) return;  // single pan, nothing to arrange
                // Pick a layout based on the number of pans the radio restored
                const QString saved = AppSettings::instance()
                    .value("PanadapterLayout", "1").toString();
                // Only rearrange if the saved layout matches the pan count
                static const QMap<QString, int> layoutPanCount = {
                    {"1", 1}, {"2v", 2}, {"2h", 2}, {"2h1", 3}, {"12h", 3}, {"3v", 3}, {"2x2", 4}, {"4v", 4}
                };
                if (layoutPanCount.value(saved, 1) == panCount)
                    m_panStack->rearrangeLayout(saved);
                else if (panCount == 2)
                    m_panStack->rearrangeLayout("2v");  // default 2-pan to vertical
                else if (panCount == 3)
                    m_panStack->rearrangeLayout("2h1"); // default 3-pan
                else if (panCount >= 4)
                    m_panStack->rearrangeLayout("2x2"); // default 4-pan

                // Optimistically set local yPixels immediately so FFT frames
                // arriving before the radio echoes back use correct scaling (#1511).
                for (auto* a : m_panStack->allApplets()) {
                    auto* s = a->spectrumWidget();
                    auto* p = m_radioModel.panadapter(a->panId());
                    if (!s || !p || !p->panStreamId()) continue;
                    int y = s->height();
                    if (y >= 100)
                        m_radioModel.panStream()->setYPixels(p->panStreamId(), y);
                }

                // Defensive re-push xpixels for all pans after layout settles.
                // Covers race where radio hadn't finished pan init when first push arrived.
                QTimer::singleShot(500, this, [this]() {
                    for (auto* applet : m_panStack->allApplets()) {
                        auto* sw = applet->spectrumWidget();
                        auto* pan = m_radioModel.panadapter(applet->panId());
                        if (!sw || !pan) continue;
                        int xpix = qMax(sw->width(), 1024);
                        int ypix = qMax(sw->height(), 200);
                        m_radioModel.sendCommand(
                            QString("display pan set %1 xpixels=%2 ypixels=%3")
                                .arg(pan->panId()).arg(xpix).arg(ypix));
                        if (pan->panStreamId())
                            m_radioModel.panStream()->setYPixels(pan->panStreamId(), ypix);
                    }
                });
            });
        }
        m_layoutRestoreTimer->start();  // restart on each new pan
    });
    // Re-push xpixels/ypixels when the radio requests it (profile change, reconnect, etc.)
    connect(&m_radioModel, &RadioModel::panDimensionsNeeded,
            this, [this](const QString& panId) {
        auto* applet = m_panStack->panadapter(panId);
        if (!applet) return;
        auto* sw = applet->spectrumWidget();
        auto* pan = m_radioModel.panadapter(panId);
        if (!sw || !pan) return;
        int xpix = sw->width();
        int ypix = sw->height();
        if (xpix < 100) xpix = 1024;
        if (ypix < 100) ypix = 700;
        m_radioModel.sendCommand(
            QString("display pan set %1 xpixels=%2 ypixels=%3")
                .arg(panId).arg(xpix).arg(ypix));
        if (pan->panStreamId())
            m_radioModel.panStream()->setYPixels(pan->panStreamId(), ypix);
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

        // Rearrange remaining pans to a sensible layout
        int remaining = m_panStack->count();
        if (remaining == 1)
            AppSettings::instance().setValue("PanadapterLayout", "1");
        else if (remaining == 2)
            m_panStack->rearrangeLayout("2v");
        else if (remaining == 3)
            m_panStack->rearrangeLayout("2h1");
    });

    // ── Per-panadapter signal wiring (extracted for multi-pan support) ──────
    wirePanadapter(m_panApplet);

    // Display overlay connections are now per-pan in wirePanadapter().

    // ── Panadapter stream → audio engine ──────────────────────────────────
    // All VITA-49 traffic arrives on the single client udpport socket owned
    // by PanadapterStream. It strips the header from IF-Data packets and emits
    // audioDataReady(); we feed that directly to the QAudioSink.
    connect(m_radioModel.panStream(), &PanadapterStream::audioDataReady,
            m_audio, &AudioEngine::feedAudioData);

    // ── QSO recorder: tap RX audio, trigger on MOX (#1297) ────────────
    // Only RX audio is recorded — TX audio (txRawPcmReady) is int16 and
    // would need separate handling to avoid format mismatch.
    connect(m_radioModel.panStream(), &PanadapterStream::audioDataReady,
            m_qsoRecorder, &QsoRecorder::feedRxAudio);
    connect(&m_radioModel.transmitModel(), &TransmitModel::moxChanged,
            m_qsoRecorder, &QsoRecorder::onMoxChanged);

    // ── BNR container autostart ─────────────────────────────────────────
#ifdef HAVE_BNR
    if (AppSettings::instance().value("BnrAutostart", "False").toString() == "True") {
        QString container = AppSettings::instance().value("BnrContainerName", "maxine-bnr").toString();
        qDebug() << "BNR: autostarting container" << container;
        QProcess::startDetached("docker", {"start", container});
    }
#endif

    // ── CW decoder: feed audio ──────────────────────────────────────────
    // Audio feed is global (same audio for all pans).
    // Text/stats output is routed to the pan owning the active slice
    // via routeCwDecoderOutput(), which re-wires on active slice change (#864).
    connect(m_radioModel.panStream(), &PanadapterStream::audioDataReady,
            &m_cwDecoder, &CwDecoder::feedAudio);

    // ── AF gain from applet panel → radio per-slice audio_level ─────────
    connect(m_appletPanel->rxApplet(), &RxApplet::afGainChanged, this, [this](int v) {
        if (auto* s = activeSlice()) s->setAudioGain(v);
    });

    // ── Slice tab toggle: click A/B/C/D → switch active slice (#1278) ──
    connect(m_appletPanel->rxApplet(), &RxApplet::sliceActivationRequested,
            this, &MainWindow::setActiveSlice);
    // Initialize slice tab buttons once the radio reports its actual capacity
    connect(&m_radioModel, &RadioModel::infoChanged, this, [this]() {
        m_appletPanel->setMaxSlices(m_radioModel.maxSlices());
    });

    // software_ver arrives after onConnectionStateChanged, so refresh the label
    connect(&m_radioModel, &RadioModel::infoChanged, this, [this]() {
        if (m_radioVersionLabel && !m_radioModel.version().isEmpty())
            m_radioVersionLabel->setText(m_radioModel.version());
    });

    // Propagate late-arriving SmartSDR+ subscription + dual-SCU diversity
    // eligibility to all existing VFOs. Both depend on radio info (license
    // subscription / model) that can arrive after a slice is created — in
    // which case onSliceAdded reads empty strings and the VFO gets stuck in
    // the wrong state (#1356 for SmartSDR+, #1503 for DIV).
    connect(&m_radioModel, &RadioModel::infoChanged, this, [this]() {
        const bool hasPlus = m_radioModel.licenseSubscription().contains("SmartSDR+");
        const QString& model = m_radioModel.model();
        const bool divAllowed = model.contains("6500") || model.contains("6600")
                             || model.contains("6700") || model.contains("8600")
                             || model.contains("AU-520");
        if (m_panStack) {
            for (auto* applet : m_panStack->allApplets()) {
                auto* sw = applet->spectrumWidget();
                for (auto* vfo : sw->findChildren<VfoWidget*>()) {
                    vfo->setSmartSdrPlus(hasPlus);
                    vfo->setDiversityAllowed(divAllowed);
                }
            }
        }
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
    auto syncBnr = [this](bool on) {
        if (m_panStack) {
            for (auto* applet : m_panStack->allApplets()) {
                auto* sw = applet->spectrumWidget();
                if (auto* vfo = sw->vfoWidget(m_activeSliceId)) {
                    QSignalBlocker sb(vfo->bnrButton());
                    vfo->bnrButton()->setChecked(on);
                }
                if (auto* btn = sw->overlayMenu()->dspBnrButton())
                    { QSignalBlocker sb(btn); btn->setChecked(on); }
            }
        }
    };
    auto syncNr4 = [this](bool on) {
        if (m_panStack) {
            for (auto* applet : m_panStack->allApplets()) {
                auto* sw = applet->spectrumWidget();
                if (auto* vfo = sw->vfoWidget(m_activeSliceId)) {
                    QSignalBlocker sb(vfo->nr4Button());
                    vfo->nr4Button()->setChecked(on);
                }
                if (auto* btn = sw->overlayMenu()->dspNr4Button())
                    { QSignalBlocker sb(btn); btn->setChecked(on); }
            }
        }
    };
    auto syncMnr = [this](bool on) {
        if (m_panStack) {
            for (auto* applet : m_panStack->allApplets()) {
                auto* sw = applet->spectrumWidget();
                if (auto* vfo = sw->vfoWidget(m_activeSliceId)) {
                    QSignalBlocker sb(vfo->mnrButton());
                    vfo->mnrButton()->setChecked(on);
                }
                if (auto* btn = sw->overlayMenu()->dspMnrButton())
                    { QSignalBlocker sb(btn); btn->setChecked(on); }
            }
        }
    };
    auto syncDfnr = [this](bool on) {
        if (m_panStack) {
            for (auto* applet : m_panStack->allApplets()) {
                auto* sw = applet->spectrumWidget();
                if (auto* vfo = sw->vfoWidget(m_activeSliceId)) {
                    QSignalBlocker sb(vfo->dfnrButton());
                    vfo->dfnrButton()->setChecked(on);
                }
                if (auto* btn = sw->overlayMenu()->dspDfnrButton())
                    { QSignalBlocker sb(btn); btn->setChecked(on); }
            }
        }
    };
    connect(m_audio, &AudioEngine::nr2EnabledChanged, this, syncNr2);
    connect(m_audio, &AudioEngine::rn2EnabledChanged, this, syncRn2);
    connect(m_audio, &AudioEngine::bnrEnabledChanged, this, syncBnr);
    connect(m_audio, &AudioEngine::nr4EnabledChanged, this, syncNr4);
    connect(m_audio, &AudioEngine::mnrEnabledChanged, this, syncMnr);
    connect(m_audio, &AudioEngine::dfnrEnabledChanged, this, syncDfnr);
    // NR2/RN2/BNR/NR4/DFNR DSP controls now only in VFO DSP tab and spectrum overlay.
    // RxApplet DSP buttons removed — no sync wiring needed.

#ifdef HAVE_RADE
    connect(m_appletPanel->rxApplet(), &RxApplet::radeActivated,
            this, [this](bool on, int sliceId) { if (on) activateRADE(sliceId); else deactivateRADE(); });
#endif

    // ── Tuning step size → AppSettings + radio command ─────────────────────
    // Per-pan SpectrumWidget::setStepSize connections are made in wirePanadapter()
    // so all pans (including new ones added at runtime) stay in sync.
    connect(m_appletPanel->rxApplet(), &RxApplet::stepSizeChanged,
            this, [this](int step) {
        // Send step to radio for the active slice
        if (auto* s = m_radioModel.slice(m_activeSliceId))
            m_radioModel.sendCommand(QString("slice set %1 step=%2").arg(s->sliceId()).arg(step));
        // Also save to AppSettings for SpectrumWidget scroll-to-tune
        auto& settings = AppSettings::instance();
        settings.setValue("TuningStepSize", QString::number(step));
        settings.save();
    });
    int savedStep = AppSettings::instance().value("TuningStepSize", "100").toInt();
    for (auto* a : m_panStack->allApplets()) a->spectrumWidget()->setStepSize(savedStep);
    m_appletPanel->rxApplet()->setInitialStepSize(savedStep);

    // ── Antenna list from radio → applet panel ─────────────────────────────
    connect(&m_radioModel, &RadioModel::antListChanged,
            m_appletPanel, &AppletPanel::setAntennaList);
    // Overlay-menu antenna wiring is now per-pan in wirePanadapter() (#1260).
    // Antenna list and S-meter are now wired per-widget in onSliceAdded.

    // ── Title bar: PC Audio, master volume, headphone volume ────────────────
    // The remote_audio_rx stream controls the radio's audio routing:
    // stream exists → audio to PC; stream removed → audio to radio speakers.
    // Keep the stream alive when TCI clients need it (#1014).
    connect(m_titleBar, &TitleBar::pcAudioToggled, this, [this](bool on) {
        if (on) {
            // Restart the local audio sink to recover from stale WASAPI sessions
            // (e.g. after Teams/Zoom reconfigures the audio endpoint). (#1569)
            QMetaObject::invokeMethod(m_audio, [this]() {
                m_audio->stopRxStream();
                m_audio->startRxStream();
            });
            m_radioModel.createRxAudioStream();
        } else {
            // Stop the local sink so audio isn't played locally even when
            // the remote_audio_rx stream is still alive for a TCI client
            // (#1571 follow-up).
            QMetaObject::invokeMethod(m_audio, [this]() {
                m_audio->stopRxStream();
            });
            m_radioModel.removeRxAudioStream();
        }
    });
    connect(m_titleBar, &TitleBar::masterVolumeChanged, this, [this](int pct) {
        bool pcAudio = AppSettings::instance().value("PcAudioEnabled", "True").toString() == "True";
        if (pcAudio)
            m_audio->setRxVolume(pct / 100.0f);
        else
            m_radioModel.setLineoutGain(pct);
        auto& s = AppSettings::instance();
        s.setValue("MasterVolume", QString::number(pct));
        s.save();
    });
    connect(m_titleBar, &TitleBar::headphoneVolumeChanged,
            &m_radioModel, &RadioModel::setHeadphoneGain);
    connect(m_titleBar, &TitleBar::lineoutMuteChanged, this, [this](bool muted) {
        m_audio->setMuted(muted);
        m_radioModel.sendCommand(QString("mixer lineout mute %1").arg(muted ? 1 : 0));
        auto& s = AppSettings::instance();
        s.setValue("PcAudioMuted", muted ? "True" : "False");
        s.save();
    });

    // ── PooDoo RX chain status tiles (Phase 0 chassis) ─────────────────────
    // RADIO tile = PC Audio enabled (standard SSB / remote_audio_rx stream).
    // DSP tile   = any client-side NR active (NR4 / DFNR / BNR).
    // SPEAK tile = AudioEngine output unmuted.
    if (auto* chain = m_appletPanel->clientChainApplet()) {
        // RADIO — driven by the existing PC Audio toggle in TitleBar.
        connect(m_titleBar, &TitleBar::pcAudioToggled,
                chain, &ClientChainApplet::setRxPcAudioEnabled);

        // DSP — aggregate of every client-side NR module.  These are
        // mutually exclusive in AudioEngine::processRx (chained
        // if/else), so at most one is active at a time.  The tile
        // greens whenever any is on, and its label rotates to the
        // active module's short name (NR2 / RN2 / NR4 / DFNR / MNR /
        // BNR).  Shared state lives in a struct held by shared_ptr
        // captured into each lambda so lifetime ends with the last
        // connected slot.
        struct DspState {
            bool nr2{false}, rn2{false}, nr4{false},
                 dfnr{false}, mnr{false}, bnr{false};
        };
        auto dspState = std::make_shared<DspState>();
        auto pushDsp = [chain, dspState]() {
            // Priority order picks the most "specific" / most-recent
            // module if more than one is somehow on at once.  Same
            // order as the audio-thread dispatcher so the displayed
            // label matches what's actually processing.
            QString label;
            if      (dspState->bnr)  label = "BNR";
            else if (dspState->mnr)  label = "MNR";
            else if (dspState->dfnr) label = "DFNR";
            else if (dspState->nr4)  label = "NR4";
            else if (dspState->rn2)  label = "RN2";
            else if (dspState->nr2)  label = "NR2";
            const bool anyOn = !label.isEmpty();
            chain->setRxClientDspActive(anyOn, label);
        };
        connect(m_audio, &AudioEngine::nr2EnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->nr2 = on; pushDsp(); });
        connect(m_audio, &AudioEngine::rn2EnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->rn2 = on; pushDsp(); });
        connect(m_audio, &AudioEngine::nr4EnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->nr4 = on; pushDsp(); });
        connect(m_audio, &AudioEngine::dfnrEnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->dfnr = on; pushDsp(); });
        connect(m_audio, &AudioEngine::mnrEnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->mnr = on; pushDsp(); });
        connect(m_audio, &AudioEngine::bnrEnabledChanged, chain,
                [dspState, pushDsp](bool on) { dspState->bnr = on; pushDsp(); });

        // SPEAK — AudioEngine emits mutedChanged on every setMuted() flip.
        connect(m_audio, &AudioEngine::mutedChanged, chain,
                [chain](bool muted) { chain->setRxOutputUnmuted(!muted); });

        // Seed initial state — settings and engine values are already
        // loaded by the time we reach this wiring code.  We pull from
        // the engine (not signals) so an already-on NR module shows up
        // immediately instead of waiting for the next toggle.
        const bool pcOn = AppSettings::instance()
            .value("PcAudioEnabled", "True").toString() == "True";
        chain->setRxPcAudioEnabled(pcOn);
        dspState->nr2  = m_audio->nr2Enabled();
        dspState->rn2  = m_audio->rn2Enabled();
        dspState->nr4  = m_audio->nr4Enabled();
        dspState->dfnr = m_audio->dfnrEnabled();
        dspState->mnr  = m_audio->mnrEnabled();
        dspState->bnr  = m_audio->bnrEnabled();
        pushDsp();
        chain->setRxOutputUnmuted(!m_audio->isMuted());
    }
    connect(m_titleBar, &TitleBar::headphoneMuteChanged, this, [this](bool muted) {
        m_radioModel.sendCommand(QString("mixer headphone mute %1").arg(muted ? 1 : 0));
    });
    connect(&m_radioModel, &RadioModel::audioOutputChanged, this, [this]() {
        m_titleBar->setHeadphoneVolume(m_radioModel.headphoneGain());
    });

    // Multi-Flex: show when another client is transmitting
    connect(&m_radioModel, &RadioModel::txOwnerChanged,
            m_titleBar, &TitleBar::setOtherClientTx);

    // Multi-Flex: title bar indicator when other clients are connected
    connect(&m_radioModel, &RadioModel::otherClientsChanged,
            m_titleBar, &TitleBar::setMultiFlexStatus);
    connect(&m_radioModel, &RadioModel::clientConnected,
            this, [this](quint32 handle,
                         const QString& source,
                         const QString& station,
                         const QString& program) {
        statusBar()->showMessage(
            clientConnectionStatusMessage(handle, source, station, program),
            3000);
    });

    // Apply saved master volume
    int savedMasterVol = AppSettings::instance().value("MasterVolume", "100").toInt();
    m_audio->setRxVolume(savedMasterVol / 100.0f);

    // Restore saved mute state (#1571)
    bool savedMute = AppSettings::instance().value("PcAudioMuted", "False").toString() == "True";
    if (savedMute) {
        m_audio->setMuted(true);
        m_titleBar->setLineoutMuted(true);
    }

    // ── S-Meter: MeterModel → SMeterWidget (active slice only) ─────────────
    connect(&m_radioModel.meterModel(), &MeterModel::sLevelChanged,
            this, [this](int sliceIndex, float dbm) {
        if (sliceIndex == m_activeSliceId)
            m_appletPanel->sMeterWidget()->setLevel(dbm);
    });
    connect(&m_radioModel.meterModel(), &MeterModel::txMetersChanged,
            m_appletPanel->sMeterWidget(), &SMeterWidget::setTxMeters);
    connect(&m_radioModel.meterModel(), &MeterModel::micMetersChanged,
            m_appletPanel->sMeterWidget(), &SMeterWidget::setMicMeters);
    connect(&m_radioModel.transmitModel(), &TransmitModel::moxChanged,
            m_appletPanel->sMeterWidget(), &SMeterWidget::setTransmitting);

    // ── Tuner: MeterModel TX meters → TunerApplet gauges ────────────────
    // Use TGXL-specific meters when available (disambiguated from PGXL by handle)
    connect(&m_radioModel.meterModel(), &MeterModel::tgxlMetersChanged,
            m_appletPanel->tunerApplet(), &TunerApplet::updateMeters);
    // Note: txMetersChanged NOT connected to TunerApplet — exciter power
    // would overwrite TGXL readings. TGXL meters come from TunerModel
    // via the direct TCP connection (port 9010). (#625)
    m_appletPanel->tunerApplet()->setTunerModel(&m_radioModel.tunerModel());
    m_appletPanel->tunerApplet()->setMeterModel(&m_radioModel.meterModel());

    // Show/hide TUNE button + applet based on TGXL presence
    connect(&m_radioModel.tunerModel(), &TunerModel::presenceChanged,
            m_appletPanel, &AppletPanel::setTunerVisible);
    connect(&m_radioModel.tunerModel(), &TunerModel::presenceChanged,
            this, [this](bool present) {
        m_tgxlIndicator->setVisible(present);
        // Auto-connect/disconnect direct TGXL connection for manual relay control (#469)
        if (present) {
            QString ip = m_radioModel.tunerModel().tgxlIp();
            if (!ip.isEmpty() && !m_tgxlConn.isConnected()) {
                m_tgxlConn.connectToTgxl(ip);
            }
        } else {
            m_tgxlConn.disconnect();
        }
    });
    // Wire TgxlConnection to TunerModel
    m_radioModel.tunerModel().setDirectConnection(&m_tgxlConn);
    // Also attempt connection when TGXL IP arrives (may come after presence)
    connect(&m_radioModel.tunerModel(), &TunerModel::stateChanged, this, [this]() {
        auto* tuner = &m_radioModel.tunerModel();
        if (tuner->isPresent() && !tuner->tgxlIp().isEmpty() && !m_tgxlConn.isConnected()) {
            m_tgxlConn.connectToTgxl(tuner->tgxlIp());
        }
    });

    // Auto-connect to PGXL when detected
    connect(&m_radioModel, &RadioModel::amplifierChanged, this, [this](bool present) {
        if (present && !m_radioModel.ampIp().isEmpty() && !m_pgxlConn.isConnected()) {
            m_pgxlConn.connectToPgxl(m_radioModel.ampIp());
        } else if (!present) {
            m_pgxlConn.disconnect();
        }
    });
    // PGXL status → AmpApplet (direct telemetry: vac, id, temp, state, etc.)
    connect(&m_pgxlConn, &PgxlConnection::statusUpdated, this, [this](const QMap<QString, QString>& kvs) {
        qCDebug(lcTuner) << "PGXL status:" << kvs;
        auto* amp = m_appletPanel->ampApplet();
        if (kvs.contains("temp"))
            amp->setTemp(kvs["temp"].toFloat());
        if (kvs.contains("id"))
            amp->setDrainCurrent(kvs["id"].toFloat());
        if (kvs.contains("vac"))
            amp->setMainsVoltage(kvs["vac"].toInt());
        if (kvs.contains("state"))
            amp->setState(kvs["state"]);
        if (kvs.contains("meffa"))
            amp->setMeff(kvs["meffa"]);
        // Convert PGXL dBm to watts and feed S-Meter alongside radio meters.
        // Use peakfwd (actual peak power) not fwd (floor/minimum).
        // Skip when amp is STANDBY — peakfwd reads ~0 dBm in standby and would
        // stomp on the exciter feed that should drive the barefoot scale.
        if (kvs.contains("peakfwd") && m_radioModel.hasAmplifier()
                && m_radioModel.ampOperate()) {
            float dbm = kvs["peakfwd"].toFloat();
            float watts = std::pow(10.0f, (dbm - 30.0f) / 10.0f);
            qCDebug(lcTuner) << "PGXL→SMeter: peakfwd=" << dbm << "dBm =" << watts << "W";
            float swr = 1.0f;
            if (kvs.contains("swr")) {
                float rl = std::abs(kvs["swr"].toFloat());
                float rho = std::pow(10.0f, -rl / 20.0f);
                swr = (rho < 0.999f) ? (1.0f + rho) / (1.0f - rho) : 99.0f;
            }
            // Ensure S-Meter is in TX mode when PGXL reports transmitting
            if (kvs.value("state").startsWith("TRANSMIT"))
                m_appletPanel->sMeterWidget()->setTransmitting(true);
            else if (kvs.contains("state") && !kvs.value("state").startsWith("TRANSMIT"))
                m_appletPanel->sMeterWidget()->setTransmitting(false);
            m_appletPanel->sMeterWidget()->setTxMeters(watts, swr);
        }
    });
    connect(&m_pgxlConn, &PgxlConnection::connected, this, [this]() {
        qDebug() << "PGXL direct connection established, version:" << m_pgxlConn.version();
    });
    // OPERATE button → PGXL standby/operate command via radio amplifier API
    connect(m_appletPanel->ampApplet(), &AmpApplet::operateToggled, this, [this](bool on) {
        if (!m_radioModel.ampHandle().isEmpty())
            m_radioModel.sendCommand(
                QString("amplifier set %1 operate=%2").arg(m_radioModel.ampHandle()).arg(on ? 1 : 0));
    });

    // Switch Fwd Power gauge scale based on radio max power and amplifier presence.
    // All three power gauges (TxApplet, TunerApplet, SMeterWidget) update together.
    // When the PGXL is in STANDBY we fall back to the barefoot scale — only the
    // radio's forward power is reaching the meter, so the 2kW arc would make
    // every reading look tiny and useless.
    auto updatePowerScale = [this]() {
        int maxW = m_radioModel.transmitModel().maxPowerLevel();
        // Aurora (AU-) radios have an integrated 600W PA (Overlord) but
        // max_power_level only reports the exciter limit (100W). Use model
        // name to detect the true PA capability. (#484)
        const QString& model = m_radioModel.model();
        if (model.startsWith("AU-") && maxW <= 100) {
            maxW = 500;
        }
        const bool ampActive = m_radioModel.hasAmplifier()
                            && m_radioModel.ampOperate();
        m_appletPanel->txApplet()->setPowerScale(maxW, ampActive);
        m_appletPanel->tunerApplet()->setPowerScale(maxW, ampActive);
        m_appletPanel->sMeterWidget()->setPowerScale(maxW, ampActive);
    };
    connect(&m_radioModel, &RadioModel::amplifierChanged, this, updatePowerScale);
    connect(&m_radioModel, &RadioModel::ampStateChanged, this, updatePowerScale);

    // TGXL indicator: two-line rich text — label on top, state smaller below.
    // Green = OPERATE, amber = BYPASS, grey = STANDBY (matches SmartSDR)
    auto setIndicatorHtml = [](QLabel* lbl, const QString& name,
                               const QString& state, const QString& color) {
        lbl->setText(QString("<span style='color:%1; font-size:18px; font-weight:bold;'>%2</span><br>"
                             "<span style='color:%1; font-size:11px;'>%3</span>")
                     .arg(color, name, state));
    };

    auto updateTgxlStyle = [this, setIndicatorHtml]() {
        auto& t = m_radioModel.tunerModel();
        if (t.isOperate() && !t.isBypass())
            setIndicatorHtml(m_tgxlIndicator, "TUN", "OPERATE", "#00e060");
        else if (t.isOperate() && t.isBypass())
            setIndicatorHtml(m_tgxlIndicator, "TUN", "BYPASS", "#e0a000");
        else
            setIndicatorHtml(m_tgxlIndicator, "TUN", "STANDBY", "#404858");
    };
    connect(&m_radioModel.tunerModel(), &TunerModel::stateChanged, this, updateTgxlStyle);

    // PGXL indicator: OPERATE (green) or STANDBY (grey) — no bypass for PGXL
    auto updatePgxlStyle = [this, setIndicatorHtml]() {
        if (m_radioModel.ampOperate())
            setIndicatorHtml(m_pgxlIndicator, "AMP", "OPERATE", "#00e060");
        else
            setIndicatorHtml(m_pgxlIndicator, "AMP", "STANDBY", "#404858");
    };
    connect(&m_radioModel, &RadioModel::ampStateChanged, this, updatePgxlStyle);

    connect(&m_radioModel, &RadioModel::amplifierChanged, this, [this, updatePgxlStyle](bool present) {
        m_pgxlIndicator->setVisible(present);
        m_appletPanel->setAmpVisible(present);
        if (present) updatePgxlStyle();
    });
    connect(&m_radioModel.meterModel(), &MeterModel::ampMetersChanged,
            this, [this](float fwdPwr, float swr, float temp) {
        m_appletPanel->ampApplet()->setFwdPower(fwdPwr);
        m_appletPanel->ampApplet()->setSwr(swr);
        m_appletPanel->ampApplet()->setTemp(temp);
        // S-Meter TX power follows the scale: amp output when PGXL is OPERATE,
        // exciter output when it's STANDBY (txMetersChanged already handles that
        // path, so we just stop overriding it here).
        if (m_radioModel.hasAmplifier() && m_radioModel.ampOperate()) {
            m_appletPanel->sMeterWidget()->setTxMeters(fwdPwr, swr);
            static int ampDbg = 0;
            if (++ampDbg % 50 == 1)
                qCDebug(lcTuner) << "AMP→SMeter: fwd=" << fwdPwr << "W swr=" << swr;
        }
    });
    connect(&m_radioModel.transmitModel(), &TransmitModel::maxPowerLevelChanged,
            this, updatePowerScale);

    // ── Meter applet: all meters consolidated ──────────────────────────────
    m_appletPanel->meterApplet()->setMeterModel(&m_radioModel.meterModel());

    // ── TX applet: meters + model ───────────────────────────────────────────
    connect(&m_radioModel.meterModel(), &MeterModel::txMetersChanged,
            m_appletPanel->txApplet(), &TxApplet::updateMeters);
    m_appletPanel->txApplet()->setTransmitModel(&m_radioModel.transmitModel());
    m_appletPanel->txApplet()->setTunerModel(&m_radioModel.tunerModel());
    m_appletPanel->rxApplet()->setTransmitModel(&m_radioModel.transmitModel());

    // Hide APD row on radios that don't support it
    connect(&m_radioModel.transmitModel(), &TransmitModel::apdStateChanged, this, [this]() {
        m_appletPanel->txApplet()->setApdVisible(
            m_radioModel.transmitModel().apdConfigurable());
    });

    // ── External controllers run on a dedicated worker thread (#502) ────
    // FlexControl, SerialPort, and MIDI controllers are created on the
    // worker thread so their I/O (serial port, RtMidi callbacks, poll timers)
    // never competes with paintEvent. Signals auto-queue to main thread.
    m_extCtrlThread = new QThread(this);
    m_extCtrlThread->setObjectName("ExtControllers");

#ifdef HAVE_SERIALPORT
    m_serialPort = new SerialPortController;  // no parent — moved to thread
    m_serialPort->moveToThread(m_extCtrlThread);
    m_flexControl = new FlexControlManager;
    m_flexControl->moveToThread(m_extCtrlThread);

    // Serial port signals (auto-queued from worker → main)
    connect(m_serialPort, &SerialPortController::externalPttChanged,
            this, [this](bool active) {
        m_radioModel.setTransmit(active);
    });
    connect(m_serialPort, &SerialPortController::cwKeyChanged,
            this, [this](bool down) {
        m_radioModel.sendCwKey(down);
    });
    connect(m_serialPort, &SerialPortController::cwPaddleChanged,
            this, [this](bool dit, bool dah) {
        m_radioModel.sendCwPaddle(dit, dah);
    });

    // FlexControl coalesce timer stays on main thread (accesses activeSlice)
    m_flexCoalesceTimer.setSingleShot(true);
    m_flexCoalesceTimer.setInterval(20);
    connect(&m_flexCoalesceTimer, &QTimer::timeout, this, [this]() {
        if (m_flexTargetMhz < 0.0) return;
        auto* s = activeSlice();
        if (!s || s->isLocked()) { m_flexTargetMhz = -1.0; return; }
        double target = m_flexTargetMhz;
        applyTuneRequest(s, target, TuneIntent::IncrementalTune, "flexcontrol");
    });
    // FlexControl signals (auto-queued from worker → main)
    connect(m_flexControl, &FlexControlManager::tuneSteps,
            this, [this](int steps) {
        switch (m_flexWheelMode) {
        case FlexWheelMode::Volume: {
            auto* s = activeSlice();
            if (!s) return;
            float gain = s->audioGain() + steps * 2.0f;
            s->setAudioGain(std::clamp(gain, 0.0f, 100.0f));
            return;
        }
        case FlexWheelMode::Power: {
            auto& tx = m_radioModel.transmitModel();
            int power = tx.rfPower() + steps;
            tx.setRfPower(std::clamp(power, 0, 100));
            return;
        }
        case FlexWheelMode::Frequency:
        default:
            break;
        }
        // Frequency mode (default)
        auto* s = activeSlice();
        if (!s || s->isLocked()) return;
        int stepHz = spectrum() ? spectrum()->stepSize() : 100;
        // Initialize target from slice on first step or after external QSY (#1098)
        if (m_flexTargetMhz < 0.0 ||
            (!m_flexCoalesceTimer.isActive() &&
             std::abs(m_flexTargetMhz - s->frequency()) > 0.001))
            m_flexTargetMhz = s->frequency();
        m_flexTargetMhz += steps * stepHz / 1e6;
        // Optimistic VFO display update — immediate visual feedback
        if (spectrum()) spectrum()->setVfoFrequency(m_flexTargetMhz);
        if (!m_flexCoalesceTimer.isActive())
            m_flexCoalesceTimer.start();
    });

    connect(m_flexControl, &FlexControlManager::buttonPressed,
            this, [this](int button, int action) {
        // Knob press while wheel function is active → return to frequency mode (#1354)
        if (button == 4 && action == 0 && m_flexWheelMode != FlexWheelMode::Frequency) {
            m_flexWheelMode = FlexWheelMode::Frequency;
            return;
        }
        QString key = QString("FlexControlBtn%1Action%2").arg(button).arg(action);
        auto& settings = AppSettings::instance();
        static const char* defaults[4][3] = {
            {"StepUp",     "StepDown",     "None"},
            {"ToggleMox",  "ToggleTune",   "None"},
            {"ToggleMute", "ToggleLock",   "None"},
            {"StepUp",     "StepDown",     "None"},  // Knob button
        };
        const char* def = (button >= 1 && button <= 4 && action >= 0 && action <= 2)
                          ? defaults[button-1][action] : "None";
        QString actionName = settings.value(key, def).toString();

        if (actionName == "StepUp") {
            if (auto* rx = m_appletPanel->rxApplet()) rx->cycleStepUp();
        } else if (actionName == "StepDown") {
            if (auto* rx = m_appletPanel->rxApplet()) rx->cycleStepDown();
        } else if (actionName == "ToggleMox") {
            m_radioModel.setTransmit(!m_radioModel.transmitModel().isTransmitting());
        } else if (actionName == "ToggleTune") {
            if (m_radioModel.transmitModel().isTuning())
                m_radioModel.transmitModel().stopTune();
            else
                m_radioModel.transmitModel().startTune();
        } else if (actionName == "ToggleMute") {
            m_audio->setMuted(!m_audio->isMuted());
        } else if (actionName == "ToggleLock") {
            if (auto* s = activeSlice()) s->setLocked(!s->isLocked());
        } else if (actionName == "NextSlice") {
            const auto& slices = m_radioModel.slices();
            if (slices.size() > 1) {
                int idx = 0;
                for (int i = 0; i < slices.size(); ++i) {
                    if (slices[i]->sliceId() == m_activeSliceId) { idx = i; break; }
                }
                setActiveSlice(slices[(idx + 1) % slices.size()]->sliceId());
            }
        } else if (actionName == "PrevSlice") {
            const auto& slices = m_radioModel.slices();
            if (slices.size() > 1) {
                int idx = 0;
                for (int i = 0; i < slices.size(); ++i) {
                    if (slices[i]->sliceId() == m_activeSliceId) { idx = i; break; }
                }
                setActiveSlice(slices[(idx - 1 + slices.size()) % slices.size()]->sliceId());
            }
        } else if (actionName == "ToggleAgc") {
            if (auto* s = activeSlice()) {
                static const char* modes[] = {"off", "slow", "med", "fast"};
                QString cur = s->agcMode().toLower();
                int idx = 0;
                for (int i = 0; i < 4; ++i) {
                    if (cur == modes[i]) { idx = i; break; }
                }
                s->setAgcMode(modes[(idx + 1) % 4]);
            }
        } else if (actionName == "VolumeUp") {
            if (auto* s = activeSlice()) {
                s->setAudioGain(std::min(100.0f, s->audioGain() + 5.0f));
            }
        } else if (actionName == "VolumeDown") {
            if (auto* s = activeSlice()) {
                s->setAudioGain(std::max(0.0f, s->audioGain() - 5.0f));
            }
        } else if (actionName == "WheelFrequency") {
            m_flexWheelMode = FlexWheelMode::Frequency;
        } else if (actionName == "WheelVolume") {
            m_flexWheelMode = FlexWheelMode::Volume;
        } else if (actionName == "WheelPower") {
            m_flexWheelMode = FlexWheelMode::Power;
        }
    });
#endif

#ifdef HAVE_MIDI
    m_midiControl = new MidiControlManager;
    m_midiControl->moveToThread(m_extCtrlThread);

    // Register MIDI params — setters/getters stored on MainWindow for
    // main-thread dispatch. Param metadata still registered on the manager.
    registerMidiParams();

    // MIDI paramAction signal: dispatches setter on main thread (#502)
    connect(m_midiControl, &MidiControlManager::paramAction,
            this, [this](const QString& paramId, float scaledValue) {
        auto it = m_midiSetters.find(paramId);
        if (it == m_midiSetters.end()) return;
        if (scaledValue == -1.0f) {
            // Toggle sentinel: read getter, flip, call setter
            auto git = m_midiGetters.find(paramId);
            float cur = (git != m_midiGetters.end() && *git) ? (*git)() : 0.0f;
            it.value()(cur > 0.5f ? 0.0f : 1.0f);
        } else {
            it.value()(scaledValue);
        }
    });

    // MIDI relativeAction signal: coalesced step-based tuning with acceleration
    connect(m_midiControl, &MidiControlManager::relativeAction,
            this, [this](const QString& paramId, int steps) {
        if (paramId == "rx.tuneKnob") {
            auto* s = activeSlice();
            if (!s || s->isLocked()) return;
            int stepHz = s->stepHz();
            if (stepHz <= 0) return;  // radio hasn't sent step yet
            // Snap to step grid: round current freq to nearest step, then offset
            long long curHz = static_cast<long long>(std::round(s->frequency() * 1e6));
            long long snapped = ((curHz + stepHz / 2) / stepHz) * stepHz;
            double newMhz = (snapped + steps * stepHz) / 1e6;
            applyTuneRequest(s, newMhz, TuneIntent::IncrementalTune, "midi-relative");
        }
    });

    MidiSettings::instance().load();
    auto savedBindings = MidiSettings::instance().loadBindings();
    for (const auto& b : savedBindings)
        m_midiControl->addBinding(b);
#endif

#ifdef HAVE_HIDAPI
    m_hidEncoder = new HidEncoderManager;
    m_hidEncoder->moveToThread(m_extCtrlThread);

    // HID encoder coalesce timer — same 20ms pattern as FlexControl
    m_hidCoalesceTimer.setSingleShot(true);
    m_hidCoalesceTimer.setInterval(20);
    connect(&m_hidCoalesceTimer, &QTimer::timeout, this, [this]() {
        if (m_hidPendingSteps == 0) return;
        auto* s = activeSlice();
        if (!s || s->isLocked()) { m_hidPendingSteps = 0; return; }
        int stepHz = spectrum() ? spectrum()->stepSize() : 100;
        double newMhz = s->frequency() + m_hidPendingSteps * stepHz / 1e6;
        m_hidPendingSteps = 0;
        applyTuneRequest(s, newMhz, TuneIntent::IncrementalTune, "hid-encoder");
    });

    connect(m_hidEncoder, &HidEncoderManager::tuneSteps,
            this, [this](int steps) {
        m_hidPendingSteps += steps;
        if (!m_hidCoalesceTimer.isActive())
            m_hidCoalesceTimer.start();
    });

    connect(m_hidEncoder, &HidEncoderManager::buttonPressed,
            this, [this](int button, int action) {
        // Reuse same action dispatch as FlexControl
        QString key = QString("HidEncoderBtn%1Action%2").arg(button).arg(action);
        QString actionName = AppSettings::instance().value(key, "None").toString();
        if (actionName == "ToggleMox")
            m_radioModel.setTransmit(!m_radioModel.transmitModel().isTransmitting());
        else if (actionName == "ToggleTune") {
            if (m_radioModel.transmitModel().isTuning())
                m_radioModel.transmitModel().stopTune();
            else
                m_radioModel.transmitModel().startTune();
        } else if (actionName == "ToggleMute")
            m_audio->setMuted(!m_audio->isMuted());
        else if (actionName == "ToggleLock") {
            if (auto* s = activeSlice()) s->setLocked(!s->isLocked());
        }
    });

    connect(m_hidEncoder, &HidEncoderManager::connectionChanged,
            this, [](bool connected, const QString& name) {
        qDebug() << "HID encoder:" << (connected ? "connected" : "disconnected") << name;
    });

    // StreamDeck native integration removed — use TCI StreamController plugin instead.
#endif

    // Start the external controller thread — objects are already moved
    m_extCtrlThread->start();

    // Init that must happen on the worker thread (serial port open, etc.)
#ifdef HAVE_SERIALPORT
    QMetaObject::invokeMethod(m_serialPort, [this] {
        m_serialPort->loadSettings();
    });
    m_flexControl->setInvertDirection(
        AppSettings::instance().value("FlexControlInvertDir", "False").toString() == "True");
    if (AppSettings::instance().value("FlexControlAutoDetect", "True").toString() == "True") {
        QString fcPort = FlexControlManager::detectPort();
        if (!fcPort.isEmpty()) {
            QMetaObject::invokeMethod(m_flexControl, [this, fcPort] {
                m_flexControl->open(fcPort);
            });
        }
    }
#endif
#ifdef HAVE_MIDI
    if (MidiSettings::instance().autoConnect()) {
        QString dev = MidiSettings::instance().lastDevice();
        if (!dev.isEmpty()) {
            QMetaObject::invokeMethod(m_midiControl, [this, dev] {
                m_midiControl->openPortByName(dev);
            });
        }
    }
#endif

#ifdef HAVE_HIDAPI
    QMetaObject::invokeMethod(m_hidEncoder, [this] {
        m_hidEncoder->loadSettings();
    });
#endif

    // ── P/CW applet: mic meters + ALC meter + model ────────────────────────
    // Suppress radio CODEC meters when mic_selection=PC (they just show noise).
    // Client-side metering handles PC mic display below.
    // Compression gauge: full 20fps meter rate, gated on speech_processor_enable.
    {
        connect(&m_radioModel.meterModel(), &MeterModel::micMetersChanged,
                this, [this](float micLevel, float compLevel, float micPeak, float compPeak) {
            // Mic level: hardware mic uses radio meters, PC uses client-side
            if (m_radioModel.transmitModel().micSelection() != "PC")
                m_appletPanel->phoneCwApplet()->updateMeters(micLevel, compLevel, micPeak, 0.0f);

            // Compression gauge: full rate (20fps from radio), gated on PROC
            {
                float comp = m_radioModel.transmitModel().speechProcessorEnable() ? compPeak : 0.0f;
                m_appletPanel->phoneCwApplet()->updateCompression(comp);
            }
        });
    }
    connect(&m_radioModel.meterModel(), &MeterModel::alcChanged,
            m_appletPanel->phoneCwApplet(), &PhoneCwApplet::updateAlc);
    // Client-side PC mic metering — radio CODEC meters only see hardware mics.
    // Apply VU-style ballistics: fast attack, slow decay (~20 dB/sec).
    {
        auto* heldLevel = new float(-150.0f);  // persists across calls
        auto* heldPeak  = new float(-150.0f);
        connect(m_audio, &AudioEngine::pcMicLevelChanged,
                this, [this, heldLevel, heldPeak](float peakDb, float avgDb) {
            if (m_radioModel.transmitModel().micSelection() != "PC") return;
            constexpr float kDecayPerUpdate = 1.0f;  // ~20 dB/sec at 20 updates/sec
            // Level: fast attack, slow decay
            if (avgDb > *heldLevel)
                *heldLevel = avgDb;
            else
                *heldLevel = qMax(avgDb, *heldLevel - kDecayPerUpdate);
            // Peak: fast attack, slower decay
            if (peakDb > *heldPeak)
                *heldPeak = peakDb;
            else
                *heldPeak = qMax(*heldLevel, *heldPeak - kDecayPerUpdate * 0.5f);
            m_appletPanel->phoneCwApplet()->updateMeters(*heldLevel, 0.0f, *heldPeak, 0.0f);
        });
    }
    m_appletPanel->phoneCwApplet()->setTransmitModel(&m_radioModel.transmitModel());



    // ── PHNE applet: VOX + CW controls ──────────────────────────────────────
    m_appletPanel->phoneApplet()->setTransmitModel(&m_radioModel.transmitModel());

    // ── EQ applet: graphic equalizer ─────────────────────────────────────────
    m_appletPanel->eqApplet()->setEqualizerModel(&m_radioModel.equalizerModel());

    // ── Client EQ applets: dedicated TX and RX tiles (Phase 7.1) ───────────
    m_appletPanel->clientEqTxApplet()->setAudioEngine(m_audio);
    m_appletPanel->clientEqRxApplet()->setAudioEngine(m_audio);

    auto wireEqEditOpen = [this](ClientEqApplet* applet) {
        connect(applet, &ClientEqApplet::editRequested, this,
                [this](ClientEqApplet::Path path) {
            ensureClientEqEditor()->showForPath(path);
        });
    };
    wireEqEditOpen(m_appletPanel->clientEqTxApplet());
    wireEqEditOpen(m_appletPanel->clientEqRxApplet());

    // ── Client Compressor applets: TX (#1661) + RX (Phase 7.3) ─────────────
    m_appletPanel->clientCompTxApplet()->setAudioEngine(m_audio);
    m_appletPanel->clientCompRxApplet()->setAudioEngine(m_audio);
    connect(m_appletPanel->clientCompTxApplet(), &ClientCompApplet::editRequested,
            this, [this]() { ensureClientCompEditor()->showForTx(); });
    connect(m_appletPanel->clientCompRxApplet(), &ClientCompApplet::editRequested,
            this, [this]() { ensureClientCompEditor()->showForRx(); });

    // ── Client Gate applets: TX (#1661 Phase 2) + RX (Phase 7.2) ───────────
    m_appletPanel->clientGateTxApplet()->setAudioEngine(m_audio);
    m_appletPanel->clientGateRxApplet()->setAudioEngine(m_audio);
    connect(m_appletPanel->clientGateTxApplet(), &ClientGateApplet::editRequested,
            this, [this]() { ensureClientGateEditor()->showForTx(); });
    connect(m_appletPanel->clientGateRxApplet(), &ClientGateApplet::editRequested,
            this, [this]() { ensureClientGateEditor()->showForRx(); });

    // ── Client De-esser applet: TX sidechain-filtered dynamics (#1661 Phase 3) ─
    m_appletPanel->clientDeEssApplet()->setAudioEngine(m_audio);
    connect(m_appletPanel->clientDeEssApplet(), &ClientDeEssApplet::editRequested,
            this, [this]() {
        if (!m_clientDeEssEditor) {
            m_clientDeEssEditor = new ClientDeEssEditor(m_audio, this);
            connect(m_clientDeEssEditor, &ClientDeEssEditor::bypassToggled,
                    this, [this](bool bypassed) {
                if (m_appletPanel && m_appletPanel->clientDeEssApplet())
                    m_appletPanel->clientDeEssApplet()->refreshEnableFromEngine();
                if (m_appletPanel && m_appletPanel->clientChainApplet())
                    m_appletPanel->clientChainApplet()->refreshFromEngine();
                if (m_appletPanel)
                    m_appletPanel->setAppletVisible("DESS", !bypassed);
            });
        }
        m_clientDeEssEditor->showForTx();
    });

    // ── Client Tube applets: TX (#1661) + RX (Phase 7.4) ───────────────────
    m_appletPanel->clientTubeTxApplet()->setAudioEngine(m_audio);
    m_appletPanel->clientTubeRxApplet()->setAudioEngine(m_audio);
    connect(m_appletPanel->clientTubeTxApplet(), &ClientTubeApplet::editRequested,
            this, [this]() { ensureClientTubeEditor()->showForTx(); });
    connect(m_appletPanel->clientTubeRxApplet(), &ClientTubeApplet::editRequested,
            this, [this]() { ensureClientTubeEditor()->showForRx(); });

    // ── Client Reverb applet: TX reverb (Freeverb) ─
    m_appletPanel->clientReverbApplet()->setAudioEngine(m_audio);

    // ── Client PUDU applets: TX (#1661 Phase 5) + RX (Phase 7.5) ───────────
    m_appletPanel->clientPuduTxApplet()->setAudioEngine(m_audio);
    m_appletPanel->clientPuduRxApplet()->setAudioEngine(m_audio);
    connect(m_appletPanel->clientPuduTxApplet(), &ClientPuduApplet::editRequested,
            this, [this]() { ensureClientPuduEditor()->showForTx(); });
    connect(m_appletPanel->clientPuduRxApplet(), &ClientPuduApplet::editRequested,
            this, [this]() { ensureClientPuduEditor()->showForRx(); });

    // ── TX signal-chain applet (#1661) ──────────────────────────────────────
    // Visual strip showing MIC → stages → TX with per-stage bypass +
    // drag-drop reorder.  Clicking a stage opens its floating editor
    // (only EQ and COMP have editors today; Gate / DeEss / Tube / Enh
    // are placeholders awaiting their DSP).
    //
    // CEQ and CMP tile visibility is driven by DSP bypass state — the
    // chain widget right-click menu, and the Bypass buttons inside
    // each floating editor, all push the new state through
    // stageEnabledChanged → setAppletVisible here.  Initial state is
    // seeded from the engine below so settings persist across launches.
    m_appletPanel->clientChainApplet()->setAudioEngine(m_audio);
    // PooDoo TX/RX tab → AppletPanel side filter.  Hides the inactive
    // side's per-stage applet tiles whenever the user flips the chain
    // tab.  Seed the initial side from the saved tab so the first
    // paint is correct.
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::chainModeChanged,
            this, [this](ClientChainApplet::ChainMode mode) {
        if (!m_appletPanel) return;
        m_appletPanel->setPooDooActiveSide(
            mode == ClientChainApplet::ChainMode::Tx
                ? AppletPanel::PooDooSide::Tx
                : AppletPanel::PooDooSide::Rx);
    });
    // Side-filter seed is moved further down — must run AFTER
    // setTxDspChainOrder, otherwise that helper's insertChildWidget
    // calls re-show every TX container we just hid.

    // Seed the PooDoo MIC-ready + TX-pulse indicators from current
    // state — subsequent changes flow through the TransmitModel
    // signal connections above (micStateChanged + moxChanged).
    {
        const auto& tx = m_radioModel.transmitModel();
        const bool ready = (tx.micSelection() == "PC") && !tx.daxOn();
        m_appletPanel->clientChainApplet()->setMicInputReady(ready);
        m_appletPanel->clientChainApplet()->setTxActive(
            ready && tx.isTransmitting());
    }
    // Pulse the TX endpoint red when we're transmitting AND PooDoo
    // is actually in the signal path (MIC=PC and DAX off).  Otherwise
    // the pulse would lie about what's being processed.
    connect(&m_radioModel.transmitModel(), &TransmitModel::moxChanged,
            this, [this](bool txActive) {
        if (!m_appletPanel || !m_appletPanel->clientChainApplet()) return;
        const auto& tx = m_radioModel.transmitModel();
        const bool ready = (tx.micSelection() == "PC") && !tx.daxOn();
        m_appletPanel->clientChainApplet()->setTxActive(ready && txActive);
    });

    // ── PUDU monitor wiring ─────────────────────────────────────
    auto* chainApplet = m_appletPanel->clientChainApplet();
    chainApplet->setMonitorHasRecording(m_puduMonitor->hasRecording());

    // User-click → start/stop based on current monitor state.  The
    // monitor's own signals drive the button visuals back.
    connect(chainApplet, &ClientChainApplet::monitorRecordClicked,
            this, [this]() {
        if (m_puduMonitor->isRecording()) {
            m_puduMonitor->stopRecording();
        } else {
            // Don't record while playing — button shouldn't be
            // enabled in that state, but guard anyway.
            if (m_puduMonitor->isPlaying()) m_puduMonitor->stopPlayback();
            m_puduMonitor->startRecording();
        }
    });
    connect(chainApplet, &ClientChainApplet::monitorPlayClicked,
            this, [this]() {
        if (m_puduMonitor->isPlaying()) {
            m_puduMonitor->stopPlayback();
        } else {
            m_puduMonitor->startPlayback();
        }
    });

    // Monitor state → UI updates.  RX audio gating is handled
    // separately via the muteRxRequested wiring above.
    connect(m_puduMonitor, &ClientPuduMonitor::recordingStarted,
            this, [this]() {
        if (m_appletPanel && m_appletPanel->clientChainApplet())
            m_appletPanel->clientChainApplet()->setMonitorRecording(true);
    });
    connect(m_puduMonitor, &ClientPuduMonitor::recordingStopped,
            this, [this](int /*durationMs*/) {
        if (m_appletPanel && m_appletPanel->clientChainApplet()) {
            auto* a = m_appletPanel->clientChainApplet();
            a->setMonitorRecording(false);
            a->setMonitorHasRecording(true);
        }
        // Auto-start playback — the mute stays installed across the
        // transition because the monitor only emits muteRxRequested
        // (false) at stopPlayback().
        m_puduMonitor->startPlayback();
    });
    connect(m_puduMonitor, &ClientPuduMonitor::playbackStarted,
            this, [this]() {
        if (m_appletPanel && m_appletPanel->clientChainApplet())
            m_appletPanel->clientChainApplet()->setMonitorPlaying(true);
    });
    connect(m_puduMonitor, &ClientPuduMonitor::playbackStopped,
            this, [this]() {
        if (m_appletPanel && m_appletPanel->clientChainApplet())
            m_appletPanel->clientChainApplet()->setMonitorPlaying(false);
    });
    if (m_audio && m_audio->clientEqTx()) {
        m_appletPanel->setAppletVisible(
            "CEQ", m_audio->clientEqTx()->isEnabled());
    }
    if (m_audio && m_audio->clientCompTx()) {
        m_appletPanel->setAppletVisible(
            "CMP", m_audio->clientCompTx()->isEnabled());
    }
    if (m_audio && m_audio->clientGateTx()) {
        m_appletPanel->setAppletVisible(
            "GATE", m_audio->clientGateTx()->isEnabled());
    }
    if (m_audio && m_audio->clientDeEssTx()) {
        m_appletPanel->setAppletVisible(
            "DESS", m_audio->clientDeEssTx()->isEnabled());
    }
    if (m_audio && m_audio->clientTubeTx()) {
        m_appletPanel->setAppletVisible(
            "TUBE", m_audio->clientTubeTx()->isEnabled());
    }
    if (m_audio && m_audio->clientPuduTx()) {
        m_appletPanel->setAppletVisible(
            "PUDU", m_audio->clientPuduTx()->isEnabled());
    }
    if (m_audio && m_audio->clientReverbTx()) {
        m_appletPanel->setAppletVisible(
            "REVERB", m_audio->clientReverbTx()->isEnabled());
    }

    // Initial applet-stack order mirrors the persisted chain order
    // for both sides, and stays in sync on every subsequent drag-
    // reorder.
    if (m_audio) {
        m_appletPanel->setTxDspChainOrder(m_audio->txChainStages());
        m_appletPanel->setRxDspChainOrder(m_audio->rxChainStages());
    }
    auto reapplyPooDooSide = [this]() {
        if (!m_appletPanel) return;
        const QString savedTab = AppSettings::instance()
            .value("PooDooAudioActiveTab", "TX").toString();
        m_appletPanel->setPooDooActiveSide(
            savedTab == "RX" ? AppletPanel::PooDooSide::Rx
                             : AppletPanel::PooDooSide::Tx);
    };
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::chainReordered,
            this, [this, reapplyPooDooSide]() {
        if (!m_appletPanel || !m_audio) return;
        m_appletPanel->setTxDspChainOrder(m_audio->txChainStages());
        // setTxDspChainOrder re-shows every reinserted child, so re-
        // apply the side filter to keep the inactive side hidden.
        reapplyPooDooSide();
    });
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::rxChainReordered,
            this, [this, reapplyPooDooSide]() {
        if (!m_appletPanel || !m_audio) return;
        m_appletPanel->setRxDspChainOrder(m_audio->rxChainStages());
        reapplyPooDooSide();
    });

    // PooDoo TX/RX side-filter seed — runs AFTER setTxDspChainOrder
    // because that helper's insertChildWidget unconditionally shows
    // each child it reinserts, undoing any earlier hide.  Putting the
    // seed here ensures the inactive side stays hidden on first paint.
    {
        const QString savedTab = AppSettings::instance()
            .value("PooDooAudioActiveTab", "TX").toString();
        m_appletPanel->setPooDooActiveSide(
            savedTab == "RX" ? AppletPanel::PooDooSide::Rx
                             : AppletPanel::PooDooSide::Tx);
    }

    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::stageEnabledChanged,
            this, [this](AudioEngine::TxChainStage stage, bool enabled) {
        if (stage == AudioEngine::TxChainStage::Eq) {
            m_appletPanel->setAppletVisible("CEQ", enabled);
            if (m_appletPanel->clientEqApplet())
                m_appletPanel->clientEqApplet()->refreshEnableFromEngine();
        } else if (stage == AudioEngine::TxChainStage::Comp) {
            m_appletPanel->setAppletVisible("CMP", enabled);
            if (m_appletPanel->clientCompApplet())
                m_appletPanel->clientCompApplet()->refreshEnableFromEngine();
        } else if (stage == AudioEngine::TxChainStage::Gate) {
            m_appletPanel->setAppletVisible("GATE", enabled);
            if (m_appletPanel->clientGateApplet())
                m_appletPanel->clientGateApplet()->refreshEnableFromEngine();
        } else if (stage == AudioEngine::TxChainStage::DeEss) {
            m_appletPanel->setAppletVisible("DESS", enabled);
            if (m_appletPanel->clientDeEssApplet())
                m_appletPanel->clientDeEssApplet()->refreshEnableFromEngine();
        } else if (stage == AudioEngine::TxChainStage::Tube) {
            m_appletPanel->setAppletVisible("TUBE", enabled);
            if (m_appletPanel->clientTubeApplet())
                m_appletPanel->clientTubeApplet()->refreshEnableFromEngine();
        } else if (stage == AudioEngine::TxChainStage::Enh) {
            // Enh slot hosts the PUDU exciter.
            m_appletPanel->setAppletVisible("PUDU", enabled);
            if (m_appletPanel->clientPuduApplet())
                m_appletPanel->clientPuduApplet()->refreshEnableFromEngine();
        } else if (stage == AudioEngine::TxChainStage::Reverb) {
            m_appletPanel->setAppletVisible("REVERB", enabled);
            if (m_appletPanel->clientReverbApplet())
                m_appletPanel->clientReverbApplet()->refreshEnableFromEngine();
        }
    });
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::editRequested,
            this, [this](AudioEngine::TxChainStage stage) {
        switch (stage) {
            case AudioEngine::TxChainStage::Eq:
                ensureClientEqEditor()->showForPath(ClientEqApplet::Path::Tx);
                break;
            case AudioEngine::TxChainStage::Comp:
                ensureClientCompEditor()->showForTx();
                break;
            case AudioEngine::TxChainStage::Gate:
                ensureClientGateEditor()->showForTx();
                break;
            case AudioEngine::TxChainStage::DeEss:
                if (!m_clientDeEssEditor) {
                    m_clientDeEssEditor = new ClientDeEssEditor(m_audio, this);
                    connect(m_clientDeEssEditor, &ClientDeEssEditor::bypassToggled,
                            this, [this](bool bypassed) {
                        if (m_appletPanel && m_appletPanel->clientDeEssApplet())
                            m_appletPanel->clientDeEssApplet()->refreshEnableFromEngine();
                        if (m_appletPanel && m_appletPanel->clientChainApplet())
                            m_appletPanel->clientChainApplet()->refreshFromEngine();
                        if (m_appletPanel)
                            m_appletPanel->setAppletVisible("DESS", !bypassed);
                    });
                }
                m_clientDeEssEditor->showForTx();
                break;
            case AudioEngine::TxChainStage::Tube:
                ensureClientTubeEditor()->showForTx();
                break;
            case AudioEngine::TxChainStage::Enh:
                // Enh slot hosts PUDU.
                ensureClientPuduEditor()->showForTx();
                break;
            case AudioEngine::TxChainStage::Reverb:
                if (!m_clientReverbEditor) {
                    m_clientReverbEditor = new ClientReverbEditor(m_audio, this);
                    connect(m_clientReverbEditor, &ClientReverbEditor::bypassToggled,
                            this, [this](bool bypassed) {
                        if (m_appletPanel && m_appletPanel->clientReverbApplet())
                            m_appletPanel->clientReverbApplet()->refreshEnableFromEngine();
                        if (m_appletPanel && m_appletPanel->clientChainApplet())
                            m_appletPanel->clientChainApplet()->refreshFromEngine();
                        if (m_appletPanel)
                            m_appletPanel->setAppletVisible("REVERB", !bypassed);
                    });
                }
                m_clientReverbEditor->showForTx();
                break;
            default:
                break;
        }
    });

    // ── RX chain edit + bypass ──────────────────────────────────────────────
    // Phase 1 routes RX EQ double-clicks to the existing ClientEqEditor in
    // RX path mode.  Click-bypass lands on the engine via the chain widget
    // itself; we just refresh the CEQ applet's Enable toggle here so it
    // stays in sync.
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::rxEditRequested,
            this, [this](AudioEngine::RxChainStage stage) {
        switch (stage) {
            case AudioEngine::RxChainStage::Eq:
                ensureClientEqEditor()->showForPath(ClientEqApplet::Path::Rx);
                break;
            case AudioEngine::RxChainStage::Gate:
                ensureClientGateEditor()->showForRx();
                break;
            case AudioEngine::RxChainStage::Comp:
                ensureClientCompEditor()->showForRx();
                break;
            case AudioEngine::RxChainStage::Tube:
                ensureClientTubeEditor()->showForRx();
                break;
            case AudioEngine::RxChainStage::Pudu:
                ensureClientPuduEditor()->showForRx();
                break;
            default:
                break;
        }
    });
    connect(m_appletPanel->clientChainApplet(),
            &ClientChainApplet::rxStageEnabledChanged,
            this, [this](AudioEngine::RxChainStage stage, bool /*enabled*/) {
        // Keep the shared per-stage applet's Enable toggle in lock-
        // step with the click-bypass that just fired on the chain
        // widget.  Each stage routes to its own applet refresh.
        if (!m_appletPanel) return;
        switch (stage) {
            case AudioEngine::RxChainStage::Eq:
                if (m_appletPanel->clientEqRxApplet())
                    m_appletPanel->clientEqRxApplet()->refreshEnableFromEngine();
                break;
            case AudioEngine::RxChainStage::Gate:
                if (m_appletPanel->clientGateRxApplet())
                    m_appletPanel->clientGateRxApplet()->refreshEnableFromEngine();
                break;
            case AudioEngine::RxChainStage::Comp:
                if (m_appletPanel->clientCompRxApplet())
                    m_appletPanel->clientCompRxApplet()->refreshEnableFromEngine();
                break;
            case AudioEngine::RxChainStage::Tube:
                if (m_appletPanel->clientTubeRxApplet())
                    m_appletPanel->clientTubeRxApplet()->refreshEnableFromEngine();
                break;
            case AudioEngine::RxChainStage::Pudu:
                if (m_appletPanel->clientPuduRxApplet())
                    m_appletPanel->clientPuduRxApplet()->refreshEnableFromEngine();
                break;
            default:
                break;
        }
    });

    // ── Antenna Genius applet: external 4O3A antenna switch ──────────────────
    m_appletPanel->agApplet()->setModel(&m_antennaGenius);
    connect(&m_antennaGenius, &AntennaGeniusModel::presenceChanged,
            m_appletPanel, &AppletPanel::setAgVisible);

    // ── 8-channel CAT: rigctld + PTY (A-H, each bound to a slice) ────────────
    {
        static const char kLetters[] = "ABCDEFGH";
        for (int i = 0; i < kCatChannels; ++i) {
            m_rigctlServers[i] = new RigctlServer(&m_radioModel, this);
            m_rigctlServers[i]->setSliceIndex(i);
            m_rigctlPtys[i] = new RigctlPty(&m_radioModel, this);
            m_rigctlPtys[i]->setSliceIndex(i);
            m_rigctlPtys[i]->setSymlinkPath(
                QString("/tmp/AetherSDR-CAT-%1").arg(kLetters[i]));
        }
    }
    m_appletPanel->catControlApplet()->setRadioModel(&m_radioModel);
    m_appletPanel->catControlApplet()->setRigctlServers(m_rigctlServers, kCatChannels);
    m_appletPanel->catControlApplet()->setRigctlPtys(m_rigctlPtys, kCatChannels);
    m_appletPanel->daxApplet()->setRadioModel(&m_radioModel);
    m_appletPanel->daxIqApplet()->setRadioModel(&m_radioModel);
#ifdef HAVE_WEBSOCKETS
    m_tciServer = new TciServer(&m_radioModel, this);
    m_tciServer->setAudioEngine(m_audio);
    m_appletPanel->tciApplet()->setRadioModel(&m_radioModel);
    m_appletPanel->tciApplet()->setTciServer(m_tciServer);

    // TCI applet sliders → TciServer gain setters
    connect(m_appletPanel->tciApplet(), &TciApplet::tciRxGainChanged,
            m_tciServer, &TciServer::setRxChannelGain);
    connect(m_appletPanel->tciApplet(), &TciApplet::tciTxGainChanged,
            m_tciServer, &TciServer::setTxGain);

    // TciServer level signals → TCI applet meters
    connect(m_tciServer, &TciServer::rxLevel,
            m_appletPanel->tciApplet(), &TciApplet::setTciRxLevel);
    connect(m_tciServer, &TciServer::txLevel,
            m_appletPanel->tciApplet(), &TciApplet::setTciTxLevel);

    // Wire slice state changes → TCI broadcasts
    connect(&m_radioModel, &RadioModel::sliceAdded, this, [this](SliceModel* s) {
        if (m_tciServer)
            m_tciServer->wireSlice(s->sliceId(), s);
    });
    // Wire existing slices (radio may already be connected with slices)
    for (auto* s : m_radioModel.slices())
        m_tciServer->wireSlice(s->sliceId(), s);
    m_tciServer->wireSpotModel();

    // Wire RX audio from PanadapterStream → TCI server for audio streaming.
    // TCI audio feeds exclusively from DAX (not audioDataReady) so that
    // audio_mute doesn't kill TCI audio (#1331).
    if (m_radioModel.panStream()) {
        connect(m_radioModel.panStream(), &PanadapterStream::daxAudioReady,
                m_tciServer, &TciServer::onDaxAudioReady);
        connect(m_radioModel.panStream(), &PanadapterStream::iqDataReady,
                m_tciServer, &TciServer::onIqDataReady);
    }

    // TCI client count changes no longer auto-create/remove the audio stream.
    // Control-only TCI clients (StreamDeck) don't need audio, and auto-creating
    // the stream overrode the user's explicit PC Audio toggle. Users who need
    // TCI audio (WSJT-X) should enable PC Audio manually. (#1071)
#endif

#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
    // DAX enable button in DaxApplet → start/stop DAX bridge
    connect(m_appletPanel->daxApplet(), &DaxApplet::daxToggled,
            this, [this](bool on) {
        if (on) {
            if (!startDax() && m_appletPanel && m_appletPanel->daxApplet())
                m_appletPanel->daxApplet()->setDaxEnabled(false);
        } else {
            stopDax();
        }
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
        Q_UNUSED(pingMs);
        m_networkLabel->setToolTip(buildNetworkTooltip(m_radioModel));
    });

    connect(&m_radioModel.meterModel(), &MeterModel::hwTelemetryChanged,
            this, [this](float paTemp, float supplyVolts) {
        m_lastPaTempC = paTemp;
        m_hasPaTempTelemetry = true;
        updatePaTempLabel();
        m_supplyVoltLabel->setText(QString("%1 V").arg(supplyVolts, 0, 'f', 2));

        // Update station label (nickname arrives via status after connect)
        const QString nick = m_radioModel.nickname();
        if (!nick.isEmpty())
            m_stationLabel->setText(nick);
    });

    // Frequency reference label from oscillator status (#478)
    // Show what the radio is actually locked to, not GPS satellite state.
    connect(&m_radioModel, &RadioModel::infoChanged, this, [this]() {
        const QString& state = m_radioModel.oscState();
        const bool locked = m_radioModel.oscLocked();
        if (state == "gpsdo" && locked) {
            // Don't override — GPS status handler shows satellite count
        } else if (m_radioModel.extPresent() && (state == "ext" || state == "external")) {
            m_gpsLabel->setText("Ref: Ext 10M");
            m_gpsStatusLabel->setText(QString("[%1]").arg(locked ? "Locked" : "Unlocked"));
        } else {
            m_gpsLabel->setText("Ref: TCXO");
            m_gpsStatusLabel->setText("[Locked]");
        }
    });

    connect(&m_radioModel, &RadioModel::gpsStatusChanged,
            this, [this](const QString& status, int tracked, int visible,
                         const QString& grid, const QString& /*alt*/,
                         const QString& /*lat*/, const QString& /*lon*/,
                         const QString& utcTime) {
        // Only show satellite count + lock status when GPSDO is active
        if (m_radioModel.oscState() == "gpsdo" && m_radioModel.oscLocked()) {
            m_gpsLabel->setText(QString("GPS: %1/%2").arg(tracked).arg(visible));
            m_gpsStatusLabel->setText(QString("[%1]").arg(status));
        }

        if (!grid.isEmpty())
            m_gridLabel->setText(grid);

        // Use GPS UTC time only when GPSDO is installed and locked.
        // GPS with no antenna/lock sends stale "00:00:00Z" — fall back to system clock.
        if (!utcTime.isEmpty()
            && m_radioModel.oscState() == "gpsdo"
            && m_radioModel.oscLocked()) {
            m_gpsTimeLabel->setText(utcTime);
            m_useSystemClock = false;
        } else {
            m_useSystemClock = true;
        }
    });

    // System clock fallback when no GPS is installed
    auto* clockTimer = new QTimer(this);
    connect(clockTimer, &QTimer::timeout, this, [this] {
        auto utc = QDateTime::currentDateTimeUtc();
        m_gpsDateLabel->setText(utc.toString("yyyy-MM-dd"));
        if (m_useSystemClock)
            m_gpsTimeLabel->setText(utc.toString("HH:mm:ssZ"));
    });
    clockTimer->start(1000);

    // Start discovery — show amber indicator while waiting for connection
    if (m_titleBar) m_titleBar->setDiscovering(true);
    m_discovery.startListening();

    const QString startupLastSerial =
        AppSettings::instance().value("LastConnectedRadioSerial").toString();
    if (!startupLastSerial.isEmpty()) {
        m_connPanel->setStatusText("Looking for your radio…");
        setPanadapterConnectionAnimation(true, "Looking for your radio…");
    }

    // Auto-connect to routed radios (probed, not broadcast-discovered)
    connect(m_connPanel, &ConnectionPanel::routedRadioFound,
            this, [this](const RadioInfo& info) {
        if (m_userDisconnected || m_radioModel.isConnected()) return;
        const QString lastSerial = AppSettings::instance()
            .value("LastConnectedRadioSerial").toString();
        if (!lastSerial.isEmpty() && info.serial == lastSerial) {
            QList<quint32> disconnectHandles;
            if (!confirmClientSlotAvailability(info, &disconnectHandles)) {
                m_userDisconnected = true;
                m_connPanel->setStatusText("Connection canceled");
                setPanadapterConnectionAnimation(false);
                return;
            }
            m_radioModel.setPendingClientDisconnects(disconnectHandles);
            qDebug() << "Auto-connecting to routed radio" << info.address.toString();
            m_connPanel->setStatusText("Auto-connecting…");
            setPanadapterConnectionAnimation(true, "Connecting to radio…");
            m_radioModel.connectToRadio(info);
        }
    });

    // Probe saved routed radio on startup
    {
        auto& s = AppSettings::instance();
        const QString routedIp = s.value("LastRoutedRadioIp").toString();
        if (!routedIp.isEmpty() && !m_userDisconnected) {
            m_connPanel->setStatusText("Looking for your radio…");
            setPanadapterConnectionAnimation(true, "Looking for your radio…");
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
    // Clear stale splitter state — layout has changed across versions.
    s.remove("SplitterState");
    // Force 4-pane sizing: CWX=0, DVK=0 (hidden), center=stretch, applet=260px
    QTimer::singleShot(0, this, [this]() {
        m_splitter->setSizes({0, 0, width() - 260, 260});
    });

    // Auto-popup connection dialog if no saved radio
    QString lastSerial = s.value("LastConnectedRadioSerial", "").toString();
    if (lastSerial.isEmpty()) {
        QTimer::singleShot(500, this, [this]() { toggleConnectionDialog(); });
    }

    // Restore the Memory dialog if it was open when the app last exited.
    QTimer::singleShot(0, this, [this]() {
        if (AppSettings::instance().value("MemoryDialogOpen", "False").toString() == "True")
            showMemoryDialog();
    });

    // Track last-seen version (used by Help → What's New)
    {
        auto& settings = AppSettings::instance();
        QString current = QCoreApplication::applicationVersion();
        if (settings.value("LastSeenVersion").toString() != current) {
            settings.setValue("LastSeenVersion", current);
            settings.save();
        }
    }
}

MainWindow::~MainWindow()
{
#ifdef HAVE_RADE
    if (m_radeSliceId >= 0)
        deactivateRADE();
#endif
    // Stop audio processing on the worker thread before destruction (#502).
    // Use BlockingQueuedConnection to ensure completion before we proceed.
    if (m_audio && m_audioThread->isRunning()) {
        QMetaObject::invokeMethod(m_audio, [this]() {
            m_audio->setNr2Enabled(false);
            m_audio->setRn2Enabled(false);
            m_audio->setBnrEnabled(false);
            m_audio->stopRxStream();
            m_audio->stopTxStream();
        }, Qt::BlockingQueuedConnection);
    }
    m_audioThread->quit();
    m_audioThread->wait(3000);
    delete m_audio;

    // Stop external controller thread (#502)
    if (m_extCtrlThread && m_extCtrlThread->isRunning()) {
        m_extCtrlThread->quit();
        m_extCtrlThread->wait(3000);
    }
#ifdef HAVE_SERIALPORT
    delete m_serialPort;
    delete m_flexControl;
#endif
#ifdef HAVE_MIDI
    delete m_midiControl;
#endif
#ifdef HAVE_HIDAPI
    delete m_hidEncoder;
#endif
}

ClientEqEditor* MainWindow::ensureClientEqEditor()
{
    if (!m_clientEqEditor) {
        m_clientEqEditor = new ClientEqEditor(m_audio, this);
        connect(m_clientEqEditor, &ClientEqEditor::bypassToggled,
                this, [this](ClientEqApplet::Path path, bool bypassed) {
            if (!m_appletPanel) return;
            if (path == ClientEqApplet::Path::Tx) {
                if (m_appletPanel->clientEqTxApplet())
                    m_appletPanel->clientEqTxApplet()->refreshEnableFromEngine();
                m_appletPanel->setAppletVisible("CEQ", !bypassed);
            } else {
                if (m_appletPanel->clientEqRxApplet())
                    m_appletPanel->clientEqRxApplet()->refreshEnableFromEngine();
                m_appletPanel->setAppletVisible("CEQ-RX", !bypassed);
            }
            if (m_appletPanel->clientChainApplet())
                m_appletPanel->clientChainApplet()->refreshFromEngine();
        });
    }
    return m_clientEqEditor;
}

ClientGateEditor* MainWindow::ensureClientGateEditor()
{
    if (!m_clientGateEditor) {
        m_clientGateEditor = new ClientGateEditor(m_audio, this);
        connect(m_clientGateEditor, &ClientGateEditor::bypassToggled,
                this, [this](ClientGateEditor::Side side, bool bypassed) {
            if (!m_appletPanel) return;
            if (side == ClientGateEditor::Side::Tx) {
                if (m_appletPanel->clientGateTxApplet())
                    m_appletPanel->clientGateTxApplet()->refreshEnableFromEngine();
                m_appletPanel->setAppletVisible("GATE", !bypassed);
            } else {
                if (m_appletPanel->clientGateRxApplet())
                    m_appletPanel->clientGateRxApplet()->refreshEnableFromEngine();
                m_appletPanel->setAppletVisible("GATE-RX", !bypassed);
            }
            if (m_appletPanel->clientChainApplet())
                m_appletPanel->clientChainApplet()->refreshFromEngine();
        });
    }
    return m_clientGateEditor;
}

ClientCompEditor* MainWindow::ensureClientCompEditor()
{
    if (!m_clientCompEditor) {
        m_clientCompEditor = new ClientCompEditor(m_audio, this);
        connect(m_clientCompEditor, &ClientCompEditor::bypassToggled,
                this, [this](ClientCompEditor::Side side, bool bypassed) {
            if (!m_appletPanel) return;
            if (side == ClientCompEditor::Side::Tx) {
                if (m_appletPanel->clientCompTxApplet())
                    m_appletPanel->clientCompTxApplet()->refreshEnableFromEngine();
                m_appletPanel->setAppletVisible("CMP", !bypassed);
            } else {
                if (m_appletPanel->clientCompRxApplet())
                    m_appletPanel->clientCompRxApplet()->refreshEnableFromEngine();
                m_appletPanel->setAppletVisible("CMP-RX", !bypassed);
            }
            if (m_appletPanel->clientChainApplet())
                m_appletPanel->clientChainApplet()->refreshFromEngine();
        });
    }
    return m_clientCompEditor;
}

ClientTubeEditor* MainWindow::ensureClientTubeEditor()
{
    if (!m_clientTubeEditor) {
        m_clientTubeEditor = new ClientTubeEditor(m_audio, this);
        connect(m_clientTubeEditor, &ClientTubeEditor::bypassToggled,
                this, [this](ClientTubeEditor::Side side, bool bypassed) {
            if (!m_appletPanel) return;
            if (side == ClientTubeEditor::Side::Tx) {
                if (m_appletPanel->clientTubeTxApplet())
                    m_appletPanel->clientTubeTxApplet()->refreshEnableFromEngine();
                m_appletPanel->setAppletVisible("TUBE", !bypassed);
            } else {
                if (m_appletPanel->clientTubeRxApplet())
                    m_appletPanel->clientTubeRxApplet()->refreshEnableFromEngine();
                m_appletPanel->setAppletVisible("TUBE-RX", !bypassed);
            }
            if (m_appletPanel->clientChainApplet())
                m_appletPanel->clientChainApplet()->refreshFromEngine();
        });
    }
    return m_clientTubeEditor;
}

ClientPuduEditor* MainWindow::ensureClientPuduEditor()
{
    if (!m_clientPuduEditor) {
        m_clientPuduEditor = new ClientPuduEditor(m_audio, this);
        connect(m_clientPuduEditor, &ClientPuduEditor::bypassToggled,
                this, [this](ClientPuduEditor::Side side, bool bypassed) {
            if (!m_appletPanel) return;
            if (side == ClientPuduEditor::Side::Tx) {
                if (m_appletPanel->clientPuduTxApplet())
                    m_appletPanel->clientPuduTxApplet()->refreshEnableFromEngine();
                m_appletPanel->setAppletVisible("PUDU", !bypassed);
            } else {
                if (m_appletPanel->clientPuduRxApplet())
                    m_appletPanel->clientPuduRxApplet()->refreshEnableFromEngine();
                m_appletPanel->setAppletVisible("PUDU-RX", !bypassed);
            }
            if (m_appletPanel->clientChainApplet())
                m_appletPanel->clientChainApplet()->refreshFromEngine();
        });
    }
    return m_clientPuduEditor;
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    // Anchor the frameless-mode size grip to the bottom-right corner,
    // overlaying the status bar.  Direct child of MainWindow so the
    // grip's native dotted-texture paint isn't suppressed by the
    // status-bar stylesheet.
    if (m_sizeGrip) {
        const int s = m_sizeGrip->width();
        m_sizeGrip->move(width() - s - 1, height() - s - 1);
        m_sizeGrip->raise();
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_shuttingDown = true;
    m_panStack->prepareShutdown();
    if (m_appletPanel && m_appletPanel->containerManager()) {
        m_appletPanel->containerManager()->prepareShutdown();
    }
    auto& s = AppSettings::instance();
    s.setValue("MainWindowGeometry", saveGeometry().toBase64());
    s.setValue("MainWindowState",   saveState().toBase64());

    // Close the applet-panel pop-out window if it's floating.  We
    // must do this explicitly because the window has parent=nullptr
    // (Qt::Window, top-level) so Qt won't auto-close it when the
    // main window exits.  m_shuttingDown gates the eventFilter so
    // AppletPanelFloating stays True for restart persistence.
    if (m_appletPanelFloatWindow) {
        m_appletPanelFloatWindow->close();
    }
    // SplitterState no longer saved (2-pane layout uses stretch factors)
    // ConnPanelCollapsed removed — panel is now a popup dialog

    s.setValue("MemoryDialogOpen",
        (m_memoryDialog && m_memoryDialog->isVisible()) ? "True" : "False");

    // Save active slice frequency/mode so the next empty-radio reconnect can
    // recreate a default slice at a sensible place (see RadioModel slice-list
    // handling). NOT used for panadapter centering — the radio is
    // authoritative for that (#1493).
    auto* sl = activeSlice();
    if (sl) {
        s.setValue("LastFrequency", QString::number(sl->frequency(), 'f', 6));
        s.setValue("LastMode", sl->mode());
    }

    // Save per-slice DAX channel assignments for restore on next launch.
    // Keyed by slice index (A=0, B=1, ...) since radio-assigned IDs change.
    {
        const QList<SliceModel*> slices = m_radioModel.slices();
        for (int i = 0; i < slices.size(); ++i) {
            const QString key = QString("DaxChannel_Slice%1").arg(QChar('A' + i));
            if (slices[i]->daxChannel() > 0) {
                s.setValue(key, QString::number(slices[i]->daxChannel()));
            } else {
                s.remove(key);
            }
        }
    }

    // DAX IQ channel is radio-authoritative — no client-side persistence needed.
    // The radio echoes daxiq_channel in pan status on reconnect.

    // Save client-side DSP state before destructor disables them
    s.setValue("ClientNr2Enabled", m_audio->nr2Enabled() ? "True" : "False");
    s.setValue("ClientRn2Enabled", m_audio->rn2Enabled() ? "True" : "False");
    s.setValue("ClientNr4Enabled", m_audio->nr4Enabled() ? "True" : "False");
    s.setValue("ClientDfnrEnabled", m_audio->dfnrEnabled() ? "True" : "False");
    s.setValue("ClientMnrEnabled", m_audio->mnrEnabled() ? "True" : "False");
    // BNR not persisted — requires manual enable each session
    // DEXP saved on-change in PhoneApplet — do NOT overwrite here, because
    // the radio may have reset DEXP to off (model reflects radio state, not
    // the user's preference).

    s.save();

    // Suppress reconnect dialog during shutdown (#527)
    m_userDisconnected = true;
    if (m_reconnectDlg) {
        m_reconnectDlg->close();
        delete m_reconnectDlg;
        m_reconnectDlg = nullptr;
    }

    m_discovery.stopListening();
    m_radioModel.disconnectFromRadio();
    audioStopRx();

    // Stop spot client worker thread
    if (m_spotThread) {
        m_spotThread->quit();
        m_spotThread->wait(3000);
        delete m_dxCluster;  m_dxCluster = nullptr;
        delete m_rbnClient;  m_rbnClient = nullptr;
        delete m_wsjtxClient; m_wsjtxClient = nullptr;
        delete m_spotCollectorClient; m_spotCollectorClient = nullptr;
        delete m_potaClient;  m_potaClient = nullptr;
#ifdef HAVE_WEBSOCKETS
        delete m_freedvClient; m_freedvClient = nullptr;
#endif
    }

    QMainWindow::closeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    QMainWindow::keyPressEvent(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::showNetworkDiagnosticsDialog()
{
    if (!m_networkDiagnosticsDialog) {
        auto* dlg = new NetworkDiagnosticsDialog(&m_radioModel, m_audio, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setModal(false);
        dlg->setWindowModality(Qt::NonModal);
        m_networkDiagnosticsDialog = dlg;
    }

    m_networkDiagnosticsDialog->show();
    m_networkDiagnosticsDialog->raise();
    m_networkDiagnosticsDialog->activateWindow();
}

QJsonObject MainWindow::buildControlDevicesSnapshot() const
{
    auto stringArray = [](const QStringList& values) {
        QJsonArray array;
        for (const QString& value : values)
            array.append(value);
        return array;
    };

    auto buttonBindings = [](const QString& prefix, int buttonCount) {
        static const char* kActionNames[] = {"tap", "double_tap", "hold"};
        QJsonArray bindings;
        auto& settings = AppSettings::instance();
        for (int button = 1; button <= buttonCount; ++button) {
            for (int action = 0; action < 3; ++action) {
                const QString key = QString("%1Btn%2Action%3").arg(prefix).arg(button).arg(action);
                const QString mappedAction = settings.value(key, "None").toString();
                if (mappedAction == QStringLiteral("None"))
                    continue;
                QJsonObject obj;
                obj["button"] = button;
                obj["gesture"] = kActionNames[action];
                obj["action"] = mappedAction;
                bindings.append(obj);
            }
        }
        return bindings;
    };

    auto addTarget = [this](QJsonObject* obj) {
        if (m_activeSliceId >= 0)
            (*obj)["target_slice_id"] = m_activeSliceId;
        else
            (*obj)["target_slice_id"] = QJsonValue();
        (*obj)["target_scope"] = "active_slice";
    };

    auto flexWheelModeName = [](FlexWheelMode mode) {
        switch (mode) {
        case FlexWheelMode::Frequency: return QStringLiteral("Frequency");
        case FlexWheelMode::Volume:    return QStringLiteral("Volume");
        case FlexWheelMode::Power:     return QStringLiteral("Power");
        }
        return QStringLiteral("Unknown");
    };

    const bool activeSliceAvailable = activeSlice() != nullptr;
    QJsonArray devices;
    int activeDeviceCount = 0;

    auto appendDevice = [&devices, &activeDeviceCount](QJsonObject device) {
        if (device["active"].toBool())
            ++activeDeviceCount;
        devices.append(device);
    };

#ifdef HAVE_SERIALPORT
    {
        const bool active = m_flexControl && m_flexControl->isOpen();
        QJsonObject flex;
        flex["type"] = "FlexControl";
        flex["available"] = true;
        flex["active"] = active;
        flex["active_for_current_slice"] = active && activeSliceAvailable;
        flex["bus_type"] = "USB";
        flex["transport"] = "USB serial";
        flex["wheel_mode"] = flexWheelModeName(m_flexWheelMode);
        flex["port_name"] = (m_flexControl && active)
            ? m_flexControl->portName()
            : AppSettings::instance().value("FlexControlPort").toString();
        flex["auto_detect"] = AppSettings::instance().value("FlexControlAutoDetect", "True").toString() == "True";
        flex["invert_direction"] = AppSettings::instance().value("FlexControlInvertDir", "False").toString() == "True";
        flex["button_bindings"] = buttonBindings(QStringLiteral("FlexControl"), 4);
        addTarget(&flex);
        flex["detail"] = active
            ? QString("Flex wheel controls %1 on the active slice").arg(flex["wheel_mode"].toString())
            : QStringLiteral("FlexControl is not connected");
        appendDevice(flex);
    }
#else
    appendDevice(QJsonObject{
        {"type", "FlexControl"},
        {"available", false},
        {"active", false},
        {"active_for_current_slice", false},
        {"bus_type", "USB"},
        {"detail", "Qt SerialPort support is not compiled in"}
    });
#endif

#ifdef HAVE_HIDAPI
    {
        const bool active = m_hidEncoder && m_hidEncoder->isOpen();
        QJsonObject hid;
        hid["type"] = "USB HID Wheel";
        hid["available"] = true;
        hid["active"] = active;
        hid["active_for_current_slice"] = active && activeSliceAvailable;
        hid["bus_type"] = "USB";
        hid["transport"] = "hidapi";
        hid["device_name"] = m_hidEncoder ? m_hidEncoder->deviceName() : QString();
        hid["vendor_id"] = (m_hidEncoder && m_hidEncoder->vendorId() != 0)
            ? QString("0x%1").arg(m_hidEncoder->vendorId(), 4, 16, QChar('0'))
            : QString();
        hid["product_id"] = (m_hidEncoder && m_hidEncoder->productId() != 0)
            ? QString("0x%1").arg(m_hidEncoder->productId(), 4, 16, QChar('0'))
            : QString();
        hid["auto_detect"] = AppSettings::instance().value("HidEncoderAutoDetect", "True").toString() == "True";
        hid["invert_direction"] = AppSettings::instance().value("HidEncoderInvertDir", "False").toString() == "True";
        hid["button_bindings"] = buttonBindings(QStringLiteral("HidEncoder"), 16);
        addTarget(&hid);
        hid["detail"] = active
            ? QString("HID wheel `%1` tunes the active slice").arg(hid["device_name"].toString())
            : QStringLiteral("No supported HID wheel is connected");
        appendDevice(hid);
    }
#else
    appendDevice(QJsonObject{
        {"type", "USB HID Wheel"},
        {"available", false},
        {"active", false},
        {"active_for_current_slice", false},
        {"bus_type", "USB"},
        {"detail", "hidapi support is not compiled in"}
    });
#endif

#ifdef HAVE_MIDI
    {
        auto messageTypeName = [](MidiBinding::MsgType type) {
            switch (type) {
            case MidiBinding::CC:        return QStringLiteral("CC");
            case MidiBinding::NoteOn:    return QStringLiteral("NoteOn");
            case MidiBinding::NoteOff:   return QStringLiteral("NoteOff");
            case MidiBinding::PitchBend: return QStringLiteral("PitchBend");
            }
            return QStringLiteral("Unknown");
        };

        auto bindingScope = [](const QString& paramId, const QString& category) {
            if (paramId.startsWith(QStringLiteral("rx."))
                || paramId == QStringLiteral("global.nextSlice")
                || paramId == QStringLiteral("global.prevSlice")) {
                return QStringLiteral("active_slice");
            }
            if (category == QStringLiteral("Global"))
                return QStringLiteral("global");
            if (category == QStringLiteral("TX") || category == QStringLiteral("Phone/CW"))
                return QStringLiteral("transmit");
            return category.toLower();
        };

        const bool active = m_midiControl && m_midiControl->isOpen();
        QJsonArray bindings;
        int sliceBindingCount = 0;
        if (m_midiControl) {
            for (const MidiBinding& binding : m_midiControl->bindings()) {
                const MidiParam* param = m_midiControl->findParam(binding.paramId);
                const QString category = param ? param->category : QString();
                const QString scope = bindingScope(binding.paramId, category);
                if (scope == QStringLiteral("active_slice"))
                    ++sliceBindingCount;

                QJsonObject obj;
                obj["source"] = binding.sourceDisplayName();
                obj["channel"] = binding.channel < 0
                    ? QStringLiteral("any")
                    : QString::number(binding.channel + 1);
                obj["message_type"] = messageTypeName(binding.msgType);
                obj["number"] = binding.number;
                obj["param_id"] = binding.paramId;
                obj["param_name"] = param ? param->displayName : binding.paramId;
                obj["category"] = category;
                obj["relative"] = binding.relative;
                obj["inverted"] = binding.inverted;
                obj["scope"] = scope;
                bindings.append(obj);
            }
        }

        QJsonObject midi;
        midi["type"] = "MIDI Controller";
        midi["available"] = true;
        midi["active"] = active;
        midi["active_for_current_slice"] = active && activeSliceAvailable && sliceBindingCount > 0;
        midi["bus_type"] = "Unknown";
        midi["transport"] = "RtMidi";
        midi["port_name"] = m_midiControl ? m_midiControl->currentPortName() : QString();
        midi["available_ports"] = m_midiControl ? stringArray(m_midiControl->availablePorts()) : QJsonArray{};
        midi["binding_count"] = bindings.size();
        midi["slice_binding_count"] = sliceBindingCount;
        midi["bindings"] = bindings;
        addTarget(&midi);
        midi["detail"] = active
            ? QString("MIDI port `%1` has %2 active-slice binding(s)")
                  .arg(midi["port_name"].toString())
                  .arg(sliceBindingCount)
            : QStringLiteral("MIDI controller is not connected");
        appendDevice(midi);
    }
#else
    appendDevice(QJsonObject{
        {"type", "MIDI Controller"},
        {"available", false},
        {"active", false},
        {"active_for_current_slice", false},
        {"bus_type", "Unknown"},
        {"detail", "MIDI support is not compiled in"}
    });
#endif

    QJsonObject snapshot;
    snapshot["available"] = true;
    snapshot["target_scope"] = "active_slice";
    snapshot["active_slice_available"] = activeSliceAvailable;
    if (m_activeSliceId >= 0)
        snapshot["target_slice_id"] = m_activeSliceId;
    else
        snapshot["target_slice_id"] = QJsonValue();
    snapshot["active_device_count"] = activeDeviceCount;
    snapshot["devices"] = devices;
    snapshot["note"] = "External wheel controls operate on the current active slice unless a binding changes slices.";
    return snapshot;
}

void MainWindow::showPropDashboard()
{
    if (!m_propDashboardDialog) {
        auto* dlg = new PropDashboardDialog(m_propForecast, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setModal(false);
        dlg->setWindowModality(Qt::NonModal);
        m_propDashboardDialog = dlg;
    }

    m_propDashboardDialog->show();
    m_propDashboardDialog->raise();
    m_propDashboardDialog->activateWindow();
}

void MainWindow::beginSliderShortcutLease(QAbstractSlider* slider)
{
    if (!slider) return;

    m_sliderShortcutLease = slider;
    s_sliderShortcutLeaseActive = true;
    m_shortcutManager.setShortcutsEnabled(false);
    renewSliderShortcutLease();
}

void MainWindow::renewSliderShortcutLease()
{
    if (!m_sliderShortcutLease) {
        releaseSliderShortcutLease(false);
        return;
    }

    s_sliderShortcutLeaseActive = true;
    m_shortcutManager.setShortcutsEnabled(false);

    if (m_sliderShortcutLease->isSliderDown()) {
        m_sliderShortcutLeaseTimer.stop();
        return;
    }

    m_sliderShortcutLeaseTimer.start(kSliderShortcutLeaseMs);
}

void MainWindow::releaseSliderShortcutLease(bool clearFocus)
{
    auto* slider = m_sliderShortcutLease.data();

    if (clearFocus && slider && slider->isSliderDown()) {
        renewSliderShortcutLease();
        return;
    }

    m_sliderShortcutLeaseTimer.stop();
    m_sliderShortcutLease.clear();
    s_sliderShortcutLeaseActive = false;
    m_shortcutManager.setShortcutsEnabled(true);

    if (clearFocus && slider && QApplication::focusWidget() == slider)
        slider->clearFocus();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (auto* slider = qobject_cast<QAbstractSlider*>(obj)) {
        if (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonDblClick) {
            beginSliderShortcutLease(slider);
        } else if (event->type() == QEvent::MouseButtonRelease
                   && m_sliderShortcutLease.data() == slider) {
            renewSliderShortcutLease();
        }
    }

    // Applet-panel floating window — save geometry on move/resize, and
    // dock back on close so the menu action stays in sync.
    if (obj == m_appletPanelFloatWindow) {
        if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
            AppSettings::instance().setValue(
                "AppletPanelFloatGeometry",
                m_appletPanelFloatWindow->saveGeometry().toBase64());
        } else if (event->type() == QEvent::Close) {
            // Distinguish user-initiated close from app shutdown.
            // During shutdown, leave AppletPanelFloating=True so the
            // next launch re-opens the panel floating — the user
            // didn't "dock it back", the whole app is exiting.
            if (m_shuttingDown) {
                AppSettings::instance().setValue(
                    "AppletPanelFloatGeometry",
                    m_appletPanelFloatWindow->saveGeometry().toBase64());
                // Fall through — let Qt close the window normally.
            } else {
                // User clicked the X on the floating window.  Flip
                // the menu action; its toggled handler docks the
                // panel back and persists AppletPanelFloating=False.
                QTimer::singleShot(0, this, [this]() {
                    if (m_popOutSidebarAction)
                        m_popOutSidebarAction->setChecked(false);
                });
            }
        }
    }

    // Space PTT: intercept at application level so it works regardless of
    // which widget has focus (buttons, combos, etc. won't steal Space).
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Space && !ke->isAutoRepeat()
            && !shortcutInputCaptured()
            && m_radioModel.isConnected()) {
            if (m_keyboardShortcutsEnabled) {
                if (event->type() == QEvent::KeyPress && !m_spacePttActive) {
                    m_spacePttActive = true;
                    m_radioModel.setTransmit(true);
                } else if (event->type() == QEvent::KeyRelease && m_spacePttActive) {
                    m_spacePttActive = false;
                    m_radioModel.setTransmit(false);
                }
            }
            return true;  // always consume Space to prevent button activation
        }

        // A clicked slider gets a short keyboard lease so arrow nudges adjust
        // the slider, then global operating shortcuts automatically resume.
        if (event->type() == QEvent::KeyPress) {
            auto* slider = qobject_cast<QAbstractSlider*>(QApplication::focusWidget());
            if (slider && m_sliderShortcutLease.data() == slider && s_sliderShortcutLeaseActive) {
                int k = ke->key();
                if (k == Qt::Key_Left || k == Qt::Key_Right
                    || k == Qt::Key_Up || k == Qt::Key_Down) {
                    bool increase = (k == Qt::Key_Right || k == Qt::Key_Up);
                    int step = (ke->modifiers() & Qt::ControlModifier)
                                   ? slider->pageStep() : slider->singleStep();
                    slider->setValue(slider->value() + (increase ? step : -step));
                    renewSliderShortcutLease();
                    return true;
                }
                if (k == Qt::Key_PageUp || k == Qt::Key_PageDown) {
                    const int step = slider->pageStep();
                    slider->setValue(slider->value()
                                     + (k == Qt::Key_PageUp ? step : -step));
                    renewSliderShortcutLease();
                    return true;
                }
                if (k == Qt::Key_Home || k == Qt::Key_End) {
                    slider->setValue(k == Qt::Key_Home
                                         ? slider->minimum()
                                         : slider->maximum());
                    renewSliderShortcutLease();
                    return true;
                }
            }
        }
    }

    if (obj == m_paTempLabel && event->type() == QEvent::MouseButtonPress) {
        setPaTempDisplayUnit(!m_paTempUseFahrenheit);
        return true;
    }
    if (obj == m_networkLabel && event->type() == QEvent::MouseButtonDblClick) {
        showNetworkDiagnosticsDialog();
        return true;
    }
    if (obj == m_stationNickLabel && event->type() == QEvent::MouseButtonDblClick) {
        toggleConnectionDialog();
        return true;
    }
    if (obj == m_cwxIndicator && event->type() == QEvent::MouseButtonPress) {
        if (!m_cwxIndicator->isEnabled()) return true;
        bool show = !m_cwxPanel->isVisible();
        // Close DVK (mutual exclusion)
        if (show && m_dvkPanel->isVisible()) {
            m_dvkPanel->hide();
            auto* sl = activeSlice();
            updateKeyerAvailability(sl ? sl->mode() : QString());
        }
        m_cwxPanel->setVisible(show);
        m_cwxIndicator->setStyleSheet(show
            ? "QLabel { color: #00b4d8; font-weight: bold; font-size: 24px; }"
            : "QLabel { color: rgba(255,255,255,40); font-weight: bold; font-size: 24px; }");
        if (show) {
            auto sizes = m_splitter->sizes();
            if (sizes.size() >= 4) {
                int cwxW = 250;
                int total = sizes[0] + sizes[1] + sizes[2];
                sizes[0] = cwxW;
                sizes[1] = 0;
                sizes[2] = total - cwxW;
                m_splitter->setSizes(sizes);
            }
        }
        return true;
    }
    if (obj == m_dvkIndicator && event->type() == QEvent::MouseButtonPress) {
        if (!m_dvkIndicator->isEnabled()) return true;
        bool show = !m_dvkPanel->isVisible();
        // Close CWX (mutual exclusion)
        if (show && m_cwxPanel->isVisible()) {
            m_cwxPanel->hide();
            auto* sl = activeSlice();
            updateKeyerAvailability(sl ? sl->mode() : QString());
        }
        m_dvkPanel->setVisible(show);
        m_dvkIndicator->setStyleSheet(show
            ? "QLabel { color: #00b4d8; font-weight: bold; font-size: 24px; }"
            : "QLabel { color: rgba(255,255,255,40); font-weight: bold; font-size: 24px; }");
        if (show) {
            auto sizes = m_splitter->sizes();
            if (sizes.size() >= 4) {
                int dvkW = 250;
                int total = sizes[0] + sizes[1] + sizes[2];
                sizes[0] = 0;
                sizes[1] = dvkW;
                sizes[2] = total - dvkW;
                m_splitter->setSizes(sizes);
            }
        }
        return true;
    }
    if (obj == m_tnfIndicator && event->type() == QEvent::MouseButtonPress) {
        m_radioModel.tnfModel().requestGlobalTnfEnabled(!m_radioModel.tnfModel().globalEnabled());
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
    if (obj == m_bandStackIndicator && event->type() == QEvent::MouseButtonPress) {
        bool show = !m_panStack->bandStackPanel()->isVisible();
        m_panStack->setBandStackVisible(show);
        updateBandStackIndicator();
        return true;
    }
    if (obj == m_tgxlIndicator && event->type() == QEvent::MouseButtonPress) {
        auto& t = m_radioModel.tunerModel();
        // Cycle: OPERATE → BYPASS → STANDBY → OPERATE
        if (t.isOperate() && !t.isBypass())
            t.setBypass(true);
        else if (t.isOperate() && t.isBypass())
            t.setOperate(false);
        else {
            t.setBypass(false);
            t.setOperate(true);
        }
        return true;
    }
    if (obj == m_pgxlIndicator && event->type() == QEvent::MouseButtonPress) {
        // Simple toggle: OPERATE ↔ STANDBY (PGXL has no BYPASS)
        m_radioModel.setAmpOperate(!m_radioModel.ampOperate());
        return true;
    }
    if (obj == m_panelToggle && event->type() == QEvent::MouseButtonPress) {
        bool visible = m_appletPanel->isVisible();
        m_appletPanel->setVisible(!visible);
        m_panelToggle->setStyleSheet(!visible
            ? "QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 22px; }"
            : "QLabel { color: #404858; font-weight: bold; font-size: 22px; }");
        AppSettings::instance().setValue("AppletPanelVisible", !visible ? "True" : "False");
        AppSettings::instance().save();
        if (m_panelVisAction) {
            QSignalBlocker sb(m_panelVisAction);
            m_panelVisAction->setChecked(!visible);
        }
        return true;
    }
    if (obj == m_addPanLabel && event->type() == QEvent::MouseButtonPress) {
        if (!m_radioModel.isConnected()) return true;
        int maxPans = m_radioModel.maxPanadapters();
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

    // Position above the status bar, centered on the station label, while staying on-screen.
    QPoint statusBarTop = statusBar()->mapToGlobal(QPoint(0, 0));
    QPoint labelCenter = m_stationNickLabel->mapToGlobal(
        QPoint(m_stationNickLabel->width() / 2, 0));
    QScreen* screen = QApplication::screenAt(labelCenter);
    if (!screen && windowHandle())
        screen = windowHandle()->screen();
    if (!screen)
        screen = QApplication::primaryScreen();

    const QSize dlgSize = m_connPanel->size();
    QPoint pos(labelCenter.x() - dlgSize.width() / 2,
               statusBarTop.y() - dlgSize.height() - 8);

    if (screen) {
        const QRect available = screen->availableGeometry();
        const int maxX = available.left() + available.width() - dlgSize.width();
        const int maxY = available.top() + available.height() - dlgSize.height();
        pos.setX(qMax(available.left(), qMin(pos.x(), maxX)));
        pos.setY(qMax(available.top(), qMin(pos.y(), maxY)));
    }

    m_connPanel->move(pos);
    m_connPanel->show();
    m_connPanel->raise();
    m_connPanel->activateWindow();
}

void MainWindow::showMemoryDialog()
{
    if (m_memoryDialog) {
        m_memoryDialog->show();
        m_memoryDialog->raise();
        m_memoryDialog->activateWindow();
        return;
    }

    auto* dlg = new MemoryDialog(&m_radioModel, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &MemoryDialog::memoryActivated,
            this, [this](int memoryIndex) { activateMemorySpot(memoryIndex); });
    connect(dlg, &QObject::destroyed, this, [this] {
        if (m_shuttingDown)
            return;
        auto& s = AppSettings::instance();
        s.setValue("MemoryDialogOpen", "False");
        s.save();
    });
    m_memoryDialog = dlg;
    dlg->show();
}

SliceModel* MainWindow::preferredMemorySlice(const QString& preferredPanId) const
{
    if (preferredPanId.isEmpty())
        return activeSlice();

    if (auto* slice = activeSlice(); slice && slice->panId() == preferredPanId)
        return slice;

    for (auto* slice : m_radioModel.slices()) {
        if (slice && slice->panId() == preferredPanId)
            return slice;
    }

    return nullptr;
}

void MainWindow::showQuickAddMemoryDialog(const QString& preferredPanId)
{
    auto* slice = preferredMemorySlice(preferredPanId);
    if (!slice) {
        statusBar()->showMessage(
            preferredPanId.isEmpty()
                ? "Open a slice before saving a memory."
                : "Open a slice on this pan before saving a memory.",
            3000);
        return;
    }

    const int sliceId = slice->sliceId();
    const QString frequencyText = QString::number(slice->frequency(), 'f', 6);
    const QString summaryText = QString("%1  |  Filter %2 to %3 Hz")
        .arg(slice->mode())
        .arg(slice->filterLow())
        .arg(slice->filterHigh());

    QDialog dialog(this);
    dialog.setWindowTitle("Save Memory");
    dialog.setModal(true);
    dialog.setMinimumWidth(360);

    auto* root = new QVBoxLayout(&dialog);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* nameLabel = new QLabel("Name:", &dialog);
    root->addWidget(nameLabel);

    auto* nameEdit = new QLineEdit(&dialog);
    nameEdit->setPlaceholderText("Enter a memory name");
    root->addWidget(nameEdit);

    auto* freqLabel = new QLabel(QString("Current Frequency: %1 MHz").arg(frequencyText), &dialog);
    freqLabel->setStyleSheet("QLabel { color: #c8d8e8; font-size: 12px; }");
    root->addWidget(freqLabel);

    auto* summaryLabel = new QLabel(summaryText, &dialog);
    summaryLabel->setStyleSheet("QLabel { color: #70879b; font-size: 11px; }");
    root->addWidget(summaryLabel);

    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch(1);

    auto* cancelButton = new QPushButton("Cancel", &dialog);
    cancelButton->setAutoDefault(false);
    buttonRow->addWidget(cancelButton);

    auto* saveButton = new QPushButton("Save", &dialog);
    saveButton->setDefault(true);
    saveButton->setEnabled(false);
    buttonRow->addWidget(saveButton);
    root->addLayout(buttonRow);

    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(nameEdit, &QLineEdit::textChanged, &dialog, [saveButton](const QString& text) {
        saveButton->setEnabled(!text.trimmed().isEmpty());
    });

    const QPointer<QDialog> dialogGuard(&dialog);
    connect(saveButton, &QPushButton::clicked, &dialog, [this,
                                                         sliceId,
                                                         dialogGuard,
                                                         nameEdit,
                                                         saveButton,
                                                         cancelButton]() {
        auto* currentSlice = m_radioModel.slice(sliceId);
        if (!currentSlice) {
            QMessageBox::warning(this, "Save Memory",
                                 "That slice is no longer available.");
            return;
        }

        saveButton->setEnabled(false);
        cancelButton->setEnabled(false);
        nameEdit->setEnabled(false);

        const QString name = nameEdit->text().trimmed();
        createMemoryFromSlice(&m_radioModel, currentSlice, name, dialogGuard.data(),
            [this, dialogGuard, nameEdit, saveButton, cancelButton, name](int code, const QString& body, int) {
            if (!dialogGuard)
                return;

            if (code == 0) {
                statusBar()->showMessage(
                    QString("Saved \"%1\" to memories.").arg(name),
                    3000);
                dialogGuard->accept();
                return;
            }

            QMessageBox::warning(dialogGuard, "Save Memory",
                                 body.isEmpty()
                                     ? "Couldn't save the current slice to memory."
                                     : body);
            nameEdit->setEnabled(true);
            saveButton->setEnabled(!nameEdit->text().trimmed().isEmpty());
            cancelButton->setEnabled(true);
            nameEdit->setFocus(Qt::OtherFocusReason);
            nameEdit->selectAll();
        });
    });

    nameEdit->setFocus(Qt::OtherFocusReason);
    dialog.exec();
}

void MainWindow::updatePaTempLabel()
{
    const QString unit = m_paTempUseFahrenheit ? "F" : "C";
    if (!m_hasPaTempTelemetry) {
        m_paTempLabel->setText(QString("PA --\u00B0%1").arg(unit));
    } else if (m_paTempUseFahrenheit) {
        const float paTempF = (m_lastPaTempC * 9.0f / 5.0f) + 32.0f;
        m_paTempLabel->setText(QString("PA %1\u00B0F").arg(paTempF, 0, 'f', 1));
    } else {
        m_paTempLabel->setText(QString("PA %1\u00B0C").arg(m_lastPaTempC, 0, 'f', 1));
    }

    m_paTempLabel->setToolTip(
        QString("PA temperature\nClick to switch to %1")
            .arg(m_paTempUseFahrenheit ? "Celsius (\u00B0C)" : "Fahrenheit (\u00B0F)"));
}

void MainWindow::setPaTempDisplayUnit(bool useFahrenheit)
{
    if (m_paTempUseFahrenheit == useFahrenheit)
        return;

    m_paTempUseFahrenheit = useFahrenheit;
    auto& settings = AppSettings::instance();
    settings.setValue(kPaTempUnitSettingKey, useFahrenheit ? "Fahrenheit" : "Celsius");
    settings.save();
    updatePaTempLabel();
}

// ─── Audio thread helpers (#502) ─────────────────────────────────────────────
// These invoke AudioEngine methods on the audio worker thread.

void MainWindow::audioStartRx()
{
    QMetaObject::invokeMethod(m_audio, &AudioEngine::startRxStream);
}

void MainWindow::audioStopRx()
{
    QMetaObject::invokeMethod(m_audio, &AudioEngine::stopRxStream);
}

void MainWindow::audioStartTx(const QHostAddress& addr, quint16 port)
{
    QMetaObject::invokeMethod(m_audio, [this, addr, port]() {
        m_audio->startTxStream(addr, port);
    });
}

void MainWindow::audioStopTx()
{
    QMetaObject::invokeMethod(m_audio, &AudioEngine::stopTxStream);
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
    radioSetup->setMenuRole(QAction::PreferencesRole);  // macOS: appears in app menu as Preferences (#883, #1013)
    connect(radioSetup, &QAction::triggered, this, [this] {
        if (m_radioSetupDialog) {
            m_radioSetupDialog->raise();
            m_radioSetupDialog->activateWindow();
            return;
        }
        // Snapshot compression setting before dialog opens
        QString prevComp = m_radioModel.audioCompressionParam();

        auto* dlg = new RadioSetupDialog(&m_radioModel, m_audio, &m_tgxlConn, &m_pgxlConn, &m_antennaGenius, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        m_radioSetupDialog = dlg;
        connect(dlg, &RadioSetupDialog::txBandSettingsRequested,
                m_txBandAction, &QAction::trigger);
        connect(dlg, &QDialog::finished, this, [this, prevComp]() {
#ifdef HAVE_SERIALPORT
            // Re-load serial port settings if changed (on worker thread)
            QMetaObject::invokeMethod(m_serialPort, [this] { m_serialPort->loadSettings(); });
            // Re-check FlexControl open/close state
            auto& fcs = AppSettings::instance();
            bool fcOpen = fcs.value("FlexControlOpen", "False").toString() == "True";
            QString fcPort = fcs.value("FlexControlPort").toString();
            bool fcInvert = fcs.value("FlexControlInvertDir", "False").toString() == "True";
            QMetaObject::invokeMethod(m_flexControl, [this, fcOpen, fcPort, fcInvert] {
                if (fcOpen) {
                    if (!m_flexControl->isOpen() && !fcPort.isEmpty())
                        m_flexControl->open(fcPort);
                } else {
                    if (m_flexControl->isOpen()) m_flexControl->close();
                }
                m_flexControl->setInvertDirection(fcInvert);
            });
#endif
            // Re-evaluate CW decode overlay visibility
            bool decodeOn = AppSettings::instance().value("CwDecodeOverlay", "True").toString() == "True";
            auto* s = activeSlice();
            if (s) {
                bool isCw = (s->mode() == "CW" || s->mode() == "CWL");
                if (m_cwDecoderApplet) m_cwDecoderApplet->setCwPanelVisible(isCw && decodeOn);
            }

            // If audio compression changed, recreate the RX audio stream
            QString newComp = m_radioModel.audioCompressionParam();
            if (newComp != prevComp && m_radioModel.isConnected()) {
                qDebug() << "MainWindow: audio compression changed from" << prevComp
                         << "to" << newComp << "— recreating audio stream";
                m_radioModel.removeRxAudioStream();
                QTimer::singleShot(500, this, [this]() {
                    m_radioModel.createRxAudioStream();
                });
                updateNr2Availability();  // Disable NR2 if switching to Opus (#1597)
            }
        });
        dlg->show();
    });

    auto* chooseRadio = settingsMenu->addAction("Connect to Radio...");
    chooseRadio->setMenuRole(QAction::NoRole);      // prevent macOS auto-reparenting (#883)
    connect(chooseRadio, &QAction::triggered, this, [this] {
        toggleConnectionDialog();
    });

#ifdef HAVE_SERIALPORT
    auto* flexControlAction = settingsMenu->addAction("FlexControl...");
    connect(flexControlAction, &QAction::triggered, this, [this] {
        auto* dlg = new RadioSetupDialog(&m_radioModel, m_audio, &m_tgxlConn, &m_pgxlConn, &m_antennaGenius, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &RadioSetupDialog::txBandSettingsRequested,
                m_txBandAction, &QAction::trigger);
        if (auto* tabs = dlg->findChild<QTabWidget*>()) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i) == "Serial") {
                    tabs->setCurrentIndex(i);
                    break;
                }
            }
        }
        dlg->show();
    });
#endif
    auto* networkAction = settingsMenu->addAction("Network...");
    connect(networkAction, &QAction::triggered, this, [this] {
        showNetworkDiagnosticsDialog();
    });
    auto* memoryAction = settingsMenu->addAction("Memory...");
    connect(memoryAction, &QAction::triggered, this, [this] {
        showMemoryDialog();
    });
    auto* usbCablesAction = settingsMenu->addAction("USB Cables...");
    connect(usbCablesAction, &QAction::triggered, this, [this] {
        auto* dlg = new RadioSetupDialog(&m_radioModel, m_audio, &m_tgxlConn, &m_pgxlConn, &m_antennaGenius, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &RadioSetupDialog::txBandSettingsRequested,
                m_txBandAction, &QAction::trigger);
        // Switch to the USB Cables tab
        if (auto* tabs = dlg->findChild<QTabWidget*>()) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i) == "USB Cables") {
                    tabs->setCurrentIndex(i);
                    break;
                }
            }
        }
        dlg->show();
    });
#ifdef HAVE_MIDI
    auto* midiAction = settingsMenu->addAction("MIDI Mapping...");
    connect(midiAction, &QAction::triggered, this, [this] {
        if (m_midiDialog) {
            m_midiDialog->raise();
            m_midiDialog->activateWindow();
            return;
        }
        auto* dlg = new MidiMappingDialog(m_midiControl, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        m_midiDialog = dlg;
        dlg->show();
    });
#endif
#ifdef HAVE_HIDAPI
#endif
    auto* spotsAction = settingsMenu->addAction("SpotHub...");
    connect(spotsAction, &QAction::triggered, this, [this] {
        // Raise existing dialog if already open
        if (m_spotHubDialog) {
            m_spotHubDialog->raise();
            m_spotHubDialog->activateWindow();
            return;
        }
        auto* dlg = new DxClusterDialog(m_dxCluster, m_rbnClient, m_wsjtxClient,
                            m_spotCollectorClient, m_potaClient,
#ifdef HAVE_WEBSOCKETS
                            m_freedvClient,
#endif
                            &m_radioModel, &m_dxccProvider, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        m_spotHubDialog = dlg;
        dlg->setTotalSpots(m_radioModel.spotModel().spots().size());
        // Live preview: refresh spots on every display settings change
        auto refreshSpots = [this]() {
            auto& s = AppSettings::instance();
            bool on       = s.value("IsSpotsEnabled", "True").toString() == "True";
            int fontSize  = s.value("SpotFontSize", "16").toInt();
            int levels    = s.value("SpotsMaxLevel", "3").toInt();
            int position  = s.value("SpotsStartingHeightPercentage", "50").toInt();
            bool override = s.value("IsSpotsOverrideColorsEnabled", "False").toString() == "True";
            QColor spotColor(s.value("SpotsOverrideColor", "#FFFF00").toString());
            QColor bgColor(s.value("SpotsOverrideBgColor", "#000000").toString());
            int bgOpacity = s.value("SpotsBackgroundOpacity", 48).toInt();
            for (auto* a : m_panStack->allApplets()) {
                auto* sw = a->spectrumWidget();
                sw->setShowSpots(on);
                sw->setSpotFontSize(fontSize);
                sw->setSpotMaxLevels(levels);
                sw->setSpotStartPct(position);
                sw->setSpotOverrideColors(override);
                sw->setSpotOverrideBg(s.value("IsSpotsOverrideBackgroundColorsEnabled", "True").toString() == "True");
                sw->setSpotColor(spotColor);
                sw->setSpotBgColor(bgColor);
                sw->setSpotBgOpacity(bgOpacity);
            }
            // Rebuild markers so source-level visibility changes, such as the
            // Memories feed toggle, apply immediately without mutating the cache.
            m_radioModel.spotModel().refresh();
        };
        connect(dlg, &DxClusterDialog::settingsChanged, this, refreshSpots);
        connect(dlg, &DxClusterDialog::connectRequested,
                this, [this](const QString& host, quint16 port, const QString& call) {
            QMetaObject::invokeMethod(m_dxCluster, [=] { m_dxCluster->connectToCluster(host, port, call); });
        });
        connect(dlg, &DxClusterDialog::disconnectRequested,
                this, [this] { QMetaObject::invokeMethod(m_dxCluster, [=] { m_dxCluster->disconnect(); }); });
        connect(dlg, &DxClusterDialog::rbnConnectRequested,
                this, [this](const QString& host, quint16 port, const QString& call) {
            QMetaObject::invokeMethod(m_rbnClient, [=] { m_rbnClient->connectToCluster(host, port, call); });
        });
        connect(dlg, &DxClusterDialog::rbnDisconnectRequested,
                this, [this] { QMetaObject::invokeMethod(m_rbnClient, [=] { m_rbnClient->disconnect(); }); });
        connect(dlg, &DxClusterDialog::wsjtxStartRequested,
                this, [this](const QString& addr, quint16 port) {
            QMetaObject::invokeMethod(m_wsjtxClient, [=] { m_wsjtxClient->startListening(addr, port); });
        });
        connect(dlg, &DxClusterDialog::wsjtxStopRequested,
                this, [this] { QMetaObject::invokeMethod(m_wsjtxClient, [=] { m_wsjtxClient->stopListening(); }); });
        connect(dlg, &DxClusterDialog::spotCollectorStartRequested,
                this, [this](quint16 port) {
            QMetaObject::invokeMethod(m_spotCollectorClient, [=] { m_spotCollectorClient->startListening(port); });
        });
        connect(dlg, &DxClusterDialog::spotCollectorStopRequested,
                this, [this] { QMetaObject::invokeMethod(m_spotCollectorClient, [=] { m_spotCollectorClient->stopListening(); }); });
        connect(dlg, &DxClusterDialog::potaStartRequested,
                this, [this](int interval) {
            QMetaObject::invokeMethod(m_potaClient, [=] { m_potaClient->startPolling(interval); });
        });
        connect(dlg, &DxClusterDialog::potaStopRequested,
                this, [this] { QMetaObject::invokeMethod(m_potaClient, [=] { m_potaClient->stopPolling(); }); });
#ifdef HAVE_WEBSOCKETS
        connect(dlg, &DxClusterDialog::freedvStartRequested,
                this, [this] { QMetaObject::invokeMethod(m_freedvClient, [this] { m_freedvClient->startConnection(); }); });
        connect(dlg, &DxClusterDialog::freedvStopRequested,
                this, [this] { QMetaObject::invokeMethod(m_freedvClient, [this] { m_freedvClient->stopConnection(); }); });
#endif
        connect(dlg, &DxClusterDialog::spotsClearedAll,
                this, [this] {
            m_spotDedup.clear();
            m_radioModel.spotModel().clear();
        });
        connect(dlg, &DxClusterDialog::tuneRequested,
                this, [this](double freqMhz) {
            if (auto* sl = activeSlice())
                applyTuneRequest(sl, freqMhz, TuneIntent::AbsoluteJump, "dx-cluster");
        });
        connect(dlg, &QDialog::finished, this, refreshSpots);  // refresh on close
        dlg->show();
    });
    auto* multiFlexAction = settingsMenu->addAction("multiFLEX...");
    auto openMultiFlex = [this] {
        MultiFlexDialog dlg(&m_radioModel, this);
        dlg.exec();
    };
    connect(multiFlexAction, &QAction::triggered, this, openMultiFlex);
    // m_titleBar connect deferred — see after TitleBar creation (~line 2530)
    m_txBandAction = settingsMenu->addAction("TX Band Settings...");
    m_txBandAction->setMenuRole(QAction::NoRole);   // prevent macOS auto-reparenting (#883)
    auto* txBandAct = m_txBandAction;
    connect(txBandAct, &QAction::triggered, this, [this] {
        if (!m_radioModel.isConnected()) {
            statusBar()->showMessage("Not connected to radio", 3000);
            return;
        }
        auto* dlg = new QDialog(this);
        dlg->setWindowTitle(QString("TX Band Settings (Current TX Profile: %1)")
            .arg(m_radioModel.transmitModel().activeProfile()));
        dlg->setMinimumSize(700, 450);
        dlg->setStyleSheet("QDialog { background: #0f0f1a; }");
        dlg->setAttribute(Qt::WA_DeleteOnClose);

        auto* vb = new QVBoxLayout(dlg);
        auto* gridContainer = new QWidget;
        gridContainer->setStyleSheet("background: #506070;");
        auto* headerGrid = new QGridLayout(gridContainer);
        headerGrid->setContentsMargins(1, 1, 1, 1);
        headerGrid->setSpacing(1);
        const QStringList headers = {"Band", "RF PWR(%)", "Tune PWR(%)", "PTT Inhibit",
                                      "ACC TX", "RCA TX Req", "ACC TX Req",
                                      "RCA TX1", "RCA TX2", "RCA TX3", "HWALC"};
        for (int c = 0; c < headers.size(); ++c) {
            auto* lbl = new QLabel(headers[c]);
            lbl->setStyleSheet("QLabel { color: #8aa8c0; font-size: 10px; "
                                "font-weight: bold; background: #1a2a3a; "
                                "padding: 2px 4px; }");
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
                w->setStyleSheet("background: #0f0f1a;");
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

        vb->addWidget(gridContainer);
        vb->addStretch();
        dlg->show();
    });

    // Inhibit during TUNE submenu — user selects which TX outputs to suppress.
    // Uses QWidgetAction with QCheckBox so the menu stays open for multi-select.
    auto* tuneInhibitMenu = settingsMenu->addMenu("Inhibit during TUNE");

    auto& settings = AppSettings::instance();
    struct InhibitDef { const char* label; const char* key; };
    static const InhibitDef inhibitDefs[] = {
        {"None",   "TuneInhibitNone"},
        {"ACC TX", "TuneInhibitAccTx"},
        {"TX1",    "TuneInhibitTx1"},
        {"TX2",    "TuneInhibitTx2"},
        {"TX3",    "TuneInhibitTx3"},
    };

    QCheckBox* noneCb = nullptr;
    QVector<QCheckBox*> outputCbs;

    for (const auto& def : inhibitDefs) {
        auto* cb = new QCheckBox(def.label);
        cb->setStyleSheet(
            "QCheckBox { color: #c8d8e8; padding: 4px 12px; }"
            "QCheckBox::indicator { width: 14px; height: 14px; }"
            "QCheckBox::indicator:unchecked { border: 1px solid #506070; background: #1a2a3a; border-radius: 2px; }"
            "QCheckBox::indicator:checked { border: 1px solid #00b4d8; background: #00b4d8; border-radius: 2px; }");
        bool on = settings.value(def.key, "False").toString() == "True";
        cb->setChecked(on);

        auto* wa = new QWidgetAction(tuneInhibitMenu);
        wa->setDefaultWidget(cb);
        tuneInhibitMenu->addAction(wa);

        if (QString(def.label) == "None")
            noneCb = cb;
        else
            outputCbs.append(cb);
    }

    // Migrate old TuneInhibitAmp → TuneInhibitAccTx
    if (settings.value("TuneInhibitAmp", "").toString() == "True"
        && settings.value("TuneInhibitAccTx", "").toString().isEmpty()) {
        settings.setValue("TuneInhibitAccTx", "True");
        settings.setValue("TuneInhibitNone", "False");
        outputCbs[0]->setChecked(true);  // ACC TX
        if (noneCb) noneCb->setChecked(false);
        settings.save();
    }

    // If no outputs selected, check None
    bool anyOutput = false;
    for (auto* cb : outputCbs) anyOutput |= cb->isChecked();
    if (noneCb && !anyOutput) noneCb->setChecked(true);

    auto syncNone = [noneCb, outputCbs]() {
        bool anyOn = false;
        for (auto* cb : outputCbs) anyOn |= cb->isChecked();
        if (noneCb) {
            QSignalBlocker b(noneCb);
            noneCb->setChecked(!anyOn);
        }
    };

    // "None" unchecks all outputs
    connect(noneCb, &QCheckBox::toggled, this, [noneCb, outputCbs, &settings](bool on) {
        if (on) {
            for (auto* cb : outputCbs) {
                QSignalBlocker b(cb);
                cb->setChecked(false);
            }
            settings.setValue("TuneInhibitAccTx", "False");
            settings.setValue("TuneInhibitTx1", "False");
            settings.setValue("TuneInhibitTx2", "False");
            settings.setValue("TuneInhibitTx3", "False");
            settings.setValue("TuneInhibitNone", "True");
            settings.save();
        } else {
            QSignalBlocker b(noneCb);
            bool anyOn = false;
            for (auto* cb : outputCbs) anyOn |= cb->isChecked();
            if (!anyOn) noneCb->setChecked(true);
        }
    });

    // Each output toggle saves and syncs None
    for (int i = 0; i < outputCbs.size(); ++i) {
        connect(outputCbs[i], &QCheckBox::toggled, this,
                [i, syncNone, &settings](bool on) {
            static const char* keys[] = {"TuneInhibitAccTx", "TuneInhibitTx1",
                                         "TuneInhibitTx2", "TuneInhibitTx3"};
            settings.setValue(keys[i], on ? "True" : "False");
            if (on)
                settings.setValue("TuneInhibitNone", "False");
            syncNone();
            settings.save();
        });
    }

    auto* dspAction = settingsMenu->addAction("AetherDSP Settings...");
    dspAction->setMenuRole(QAction::NoRole);        // prevent macOS auto-reparenting (#883)
    connect(dspAction, &QAction::triggered, this, [this] {
        if (m_dspDialog) {
            m_dspDialog->raise();
            m_dspDialog->activateWindow();
            return;
        }
        auto* dlg = new AetherDspDialog(m_audio, this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        // Wire NR2 parameter signals to AudioEngine (audio thread safe)
        connect(dlg, &AetherDspDialog::nr2GainMaxChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2GainMax(v); });
        });
        connect(dlg, &AetherDspDialog::nr2GainSmoothChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2GainSmooth(v); });
        });
        connect(dlg, &AetherDspDialog::nr2QsppChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2Qspp(v); });
        });
        connect(dlg, &AetherDspDialog::nr2GainMethodChanged, this, [this](int m) {
            QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr2GainMethod(m); });
        });
        connect(dlg, &AetherDspDialog::nr2NpeMethodChanged, this, [this](int m) {
            QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr2NpeMethod(m); });
        });
        connect(dlg, &AetherDspDialog::nr2AeFilterChanged, this, [this](bool on) {
            QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr2AeFilter(on); });
        });
        // Wire NR4 parameter signals to AudioEngine
        connect(dlg, &AetherDspDialog::nr4ReductionChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4ReductionAmount(v); });
        });
        connect(dlg, &AetherDspDialog::nr4SmoothingChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4SmoothingFactor(v); });
        });
        connect(dlg, &AetherDspDialog::nr4WhiteningChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4WhiteningFactor(v); });
        });
        connect(dlg, &AetherDspDialog::nr4AdaptiveNoiseChanged, this, [this](bool on) {
            QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr4AdaptiveNoise(on); });
        });
        connect(dlg, &AetherDspDialog::nr4NoiseMethodChanged, this, [this](int m) {
            QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr4NoiseEstimationMethod(m); });
        });
        connect(dlg, &AetherDspDialog::nr4MaskingDepthChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4MaskingDepth(v); });
        });
        connect(dlg, &AetherDspDialog::nr4SuppressionChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4SuppressionStrength(v); });
        });
        // Wire DFNR parameter signals
        connect(dlg, &AetherDspDialog::dfnrAttenLimitChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrAttenLimit(v); });
        });
        connect(dlg, &AetherDspDialog::dfnrPostFilterBetaChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrPostFilterBeta(v); });
        });
        // Wire MNR enable / strength signals
        connect(dlg, &AetherDspDialog::mnrEnabledChanged, this, [this](bool on) {
            QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setMnrEnabled(on); });
        });
        connect(dlg, &AetherDspDialog::mnrStrengthChanged, this, [this](float v) {
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setMnrStrength(v); });
        });
        m_dspDialog = dlg;
        dlg->show();
    });

    settingsMenu->addSeparator();

    auto* autoRigctlAction = settingsMenu->addAction("Autostart rigctld with AetherSDR");
    autoRigctlAction->setCheckable(true);
    autoRigctlAction->setChecked(
        AppSettings::instance().value("AutoStartRigctld", "False").toString() == "True");
    connect(autoRigctlAction, &QAction::toggled, this, [this](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("AutoStartRigctld", on ? "True" : "False");
        s.save();
        if (m_radioModel.isConnected()) {
            const int basePort = s.value("CatTcpPort", "4532").toInt();
            for (int i = 0; i < kCatChannels; ++i) {
                if (on && m_rigctlServers[i] && !m_rigctlServers[i]->isRunning())
                    m_rigctlServers[i]->start(static_cast<quint16>(basePort + i));
                else if (!on && m_rigctlServers[i] && m_rigctlServers[i]->isRunning())
                    m_rigctlServers[i]->stop();
            }
            if (m_appletPanel && m_appletPanel->catControlApplet())
                m_appletPanel->catControlApplet()->setTcpEnabled(on);
        }
    });

#ifdef _WIN32
    // CAT virtual serial ports require macOS or Linux. Force the setting
    // off so a saved True (e.g. from a Linux settings import) can't
    // activate anything, and omit the menu entry entirely (#1556).
    {
        auto& s = AppSettings::instance();
        if (s.value("AutoStartCAT", "False").toString() != "False") {
            s.setValue("AutoStartCAT", "False");
            s.save();
        }
    }
#else
    auto* autoCatAction = settingsMenu->addAction("Autostart CAT with AetherSDR");
    autoCatAction->setCheckable(true);
    autoCatAction->setChecked(
        AppSettings::instance().value("AutoStartCAT", "False").toString() == "True");
    connect(autoCatAction, &QAction::toggled, this, [this](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("AutoStartCAT", on ? "True" : "False");
        s.save();
        if (m_radioModel.isConnected()) {
            for (int i = 0; i < kCatChannels; ++i) {
                if (on && m_rigctlPtys[i] && !m_rigctlPtys[i]->isRunning())
                    m_rigctlPtys[i]->start();
                else if (!on && m_rigctlPtys[i] && m_rigctlPtys[i]->isRunning())
                    m_rigctlPtys[i]->stop();
            }
            if (m_appletPanel && m_appletPanel->catControlApplet())
                m_appletPanel->catControlApplet()->setPtyEnabled(on);
        }
    });
#endif

    auto* autoTciAction = settingsMenu->addAction("Autostart TCI with AetherSDR");
    autoTciAction->setCheckable(true);
    autoTciAction->setChecked(
        AppSettings::instance().value("AutoStartTCI", "False").toString() == "True");
    connect(autoTciAction, &QAction::toggled, this, [this](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("AutoStartTCI", on ? "True" : "False");
        s.save();
#ifdef HAVE_WEBSOCKETS
        if (m_tciServer) {
            if (on && !m_tciServer->isRunning()) {
                int port = s.value("TciPort", "50001").toInt();
                m_tciServer->start(static_cast<quint16>(port));
            } else if (!on && m_tciServer->isRunning()) {
                m_tciServer->stop();
            }
            if (m_appletPanel && m_appletPanel->tciApplet())
                m_appletPanel->tciApplet()->setTciEnabled(on);
        }
#endif
    });

#if !defined(Q_OS_MAC) && !defined(HAVE_PIPEWIRE)
    // DAX audio bridge requires macOS CoreAudio or Linux with PipeWire.
    // Force off and omit the menu entry on platforms without a bridge (#1556).
    {
        auto& s = AppSettings::instance();
        if (s.value("AutoStartDAX", "False").toString() != "False") {
            s.setValue("AutoStartDAX", "False");
            s.save();
        }
    }
#else
    auto* autoDaxAction = settingsMenu->addAction("Autostart DAX with AetherSDR");
    autoDaxAction->setCheckable(true);
    autoDaxAction->setChecked(
        AppSettings::instance().value("AutoStartDAX", "False").toString() == "True");
    connect(autoDaxAction, &QAction::toggled, this, [this](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("AutoStartDAX", on ? "True" : "False");
        s.save();
        if (m_radioModel.isConnected()) {
            if (on) {
                if (startDax() && m_appletPanel && m_appletPanel->daxApplet())
                    m_appletPanel->daxApplet()->setDaxEnabled(true);
            } else {
                stopDax();
                if (m_appletPanel && m_appletPanel->daxApplet())
                    m_appletPanel->daxApplet()->setDaxEnabled(false);
            }
        }
    });
#endif

    // "Low-Latency DAX (FreeDV)" menu retired in v0.8.19 — the toggle
    // it used to drive is now applied automatically by RADE mode, since
    // RADE was the only consumer that ever actually wanted that route.
    // See AudioEngine::setRadeMode().

    // Connect placeholder items to show "not implemented" message
    for (auto* action : settingsMenu->actions()) {
        if (!action->isSeparator() && action != radioSetup && action != chooseRadio
            && action != networkAction && action != memoryAction && action != spotsAction
            && action != usbCablesAction
#ifdef HAVE_SERIALPORT
            && action != flexControlAction
#endif
#ifdef HAVE_MIDI
            && action != midiAction
#endif
            && action != multiFlexAction
            && action != autoRigctlAction
#ifndef _WIN32
            && action != autoCatAction
#endif
            && action != autoTciAction
#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
            && action != autoDaxAction
#endif
            ) {
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

    m_panelVisAction = viewMenu->addAction("Applet Panel");
    m_panelVisAction->setCheckable(true);
    m_panelVisAction->setChecked(
        AppSettings::instance().value("AppletPanelVisible", "True").toString() == "True");
    connect(m_panelVisAction, &QAction::toggled, this, [this](bool on) {
        m_appletPanel->setVisible(on);
        m_panelToggle->setStyleSheet(on
            ? "QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 22px; }"
            : "QLabel { color: #404858; font-weight: bold; font-size: 22px; }");
        AppSettings::instance().setValue("AppletPanelVisible", on ? "True" : "False");
        AppSettings::instance().save();
    });

    // Pop out the whole applet panel into its own window (#1713 Phase 6).
    // Toggles between panel-docked (inside the QSplitter) and floating
    // (separate Qt::Window).  Ctrl+Shift+S is the keyboard shortcut.
    m_popOutSidebarAction = viewMenu->addAction("Pop Out Applet Panel");
    m_popOutSidebarAction->setCheckable(true);
    m_popOutSidebarAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
    m_popOutSidebarAction->setChecked(
        AppSettings::instance().value(
            "AppletPanelFloating", "False").toString() == "True");
    connect(m_popOutSidebarAction, &QAction::toggled, this, [this](bool floating) {
        if (floating) floatAppletPanel();
        else          dockAppletPanel();
        AppSettings::instance().setValue(
            "AppletPanelFloating", floating ? "True" : "False");
        AppSettings::instance().save();
    });
    if (m_popOutSidebarAction->isChecked()) {
        QTimer::singleShot(0, this, [this]() { floatAppletPanel(); });
    }

    viewMenu->addSeparator();

    // Band Plan submenu — Off / Small / Medium / Large / Huge
    auto* bandPlanMenu = viewMenu->addMenu("Band Plan");
    int savedBpSize = AppSettings::instance().value("BandPlanFontSize", "").toInt();
    if (savedBpSize == 0 && AppSettings::instance().value("ShowBandPlan", "True").toString() == "True")
        savedBpSize = 6;  // migrate old boolean setting
    auto* bpGroup = new QActionGroup(bandPlanMenu);
    struct BpOption { const char* label; int pt; };
    for (auto [label, pt] : {BpOption{"Off", 0}, {"Small", 6}, {"Medium", 10}, {"Large", 12}, {"Huge", 16}}) {
        auto* act = bandPlanMenu->addAction(label);
        act->setCheckable(true);
        act->setChecked(pt == savedBpSize);
        bpGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, pt] {
            for (auto* a : m_panStack->allApplets())
                a->spectrumWidget()->setBandPlanFontSize(pt);
            AppSettings::instance().setValue("BandPlanFontSize", QString::number(pt));
            AppSettings::instance().save();
        });
    }

    // Band plan region selector (#425)
    bandPlanMenu->addSeparator();
    auto* planGroup = new QActionGroup(bandPlanMenu);
    const QString activePlan = m_bandPlanMgr->activePlanName();
    for (const auto& name : m_bandPlanMgr->availablePlans()) {
        auto* act = bandPlanMenu->addAction(name);
        act->setCheckable(true);
        act->setChecked(name == activePlan);
        planGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, name] {
            m_bandPlanMgr->setActivePlan(name);
        });
    }

    auto* singleClickTuneAct = viewMenu->addAction("Single-Click to Tune");
    singleClickTuneAct->setCheckable(true);
    singleClickTuneAct->setChecked(
        AppSettings::instance().value("SingleClickTune", "False").toString() == "True");
    connect(singleClickTuneAct, &QAction::toggled, this, [this](bool on) {
        for (auto* a : m_panStack->allApplets())
            a->spectrumWidget()->setSingleClickTune(on);
        AppSettings::instance().setValue("SingleClickTune", on ? "True" : "False");
        AppSettings::instance().save();
    });

    auto* panFollowVfoAct = viewMenu->addAction("Pan Follows VFO");
    panFollowVfoAct->setCheckable(true);
    panFollowVfoAct->setChecked(
        AppSettings::instance().value("PanFollowVfo", "True").toString() == "True");
    connect(panFollowVfoAct, &QAction::toggled, this, [](bool on) {
        AppSettings::instance().setValue("PanFollowVfo", on ? "True" : "False");
        AppSettings::instance().save();
    });

    // UI Scale submenu — sets QT_SCALE_FACTOR, applies on restart
    auto* scaleMenu = viewMenu->addMenu("UI Scale");
    int savedScale = AppSettings::instance().value("UiScalePercent", "100").toInt();
    auto* scaleGroup = new QActionGroup(scaleMenu);
    for (int pct : {75, 85, 100, 110, 125, 150, 175, 200}) {
        auto* act = scaleMenu->addAction(QString("%1%").arg(pct));
        act->setCheckable(true);
        act->setChecked(pct == savedScale);
        scaleGroup->addAction(act);
        connect(act, &QAction::triggered, this, [this, pct] {
            applyUiScale(pct);
        });
    }
    scaleMenu->addSeparator();
    auto* zoomInAct = scaleMenu->addAction("Zoom In");
    zoomInAct->setShortcut(QKeySequence("Ctrl+="));
    connect(zoomInAct, &QAction::triggered, this, [this] { stepUiScale(+1); });
    auto* zoomOutAct = scaleMenu->addAction("Zoom Out");
    zoomOutAct->setShortcut(QKeySequence("Ctrl+-"));
    connect(zoomOutAct, &QAction::triggered, this, [this] { stepUiScale(-1); });
    auto* zoomResetAct = scaleMenu->addAction("Reset (100%)");
    zoomResetAct->setShortcut(QKeySequence("Ctrl+0"));
    connect(zoomResetAct, &QAction::triggered, this, [this] { applyUiScale(100); });

    auto* resetOrderAct = viewMenu->addAction("Reset Applet Order");
    connect(resetOrderAct, &QAction::triggered, this, [this] {
        m_appletPanel->resetOrder();
    });

    viewMenu->addSeparator();
    m_minimalModeAction = viewMenu->addAction("Minimal Mode");
    m_minimalModeAction->setCheckable(true);
    m_minimalModeAction->setShortcut(QKeySequence("Ctrl+M"));
    m_minimalModeAction->setChecked(
        AppSettings::instance().value("MinimalModeEnabled", "False").toString() == "True");
    connect(m_minimalModeAction, &QAction::toggled, this, [this](bool on) {
        toggleMinimalMode(on);
    });

    auto* framelessAct = viewMenu->addAction("Frameless Window");
    framelessAct->setCheckable(true);
    framelessAct->setShortcut(QKeySequence("Ctrl+Shift+F"));
    framelessAct->setToolTip(
        "Hide the OS title bar.  Drag the AetherSDR title bar to move,\n"
        "double-click to maximize, or use the min/max/close buttons on\n"
        "the right.  Toggle off if your compositor mishandles it.");
    framelessAct->setChecked(
        AppSettings::instance().value("FramelessWindow", "True").toString() == "True");
    connect(framelessAct, &QAction::toggled, this, [this](bool on) {
        setFramelessWindow(on);
    });

    auto* propForecastAct = viewMenu->addAction("Propagation Conditions");
    propForecastAct->setCheckable(true);
    propForecastAct->setChecked(
        AppSettings::instance().value("PropForecastEnabled", "False").toString() == "True");
    connect(propForecastAct, &QAction::toggled, this, [this](bool on) {
        AppSettings::instance().setValue("PropForecastEnabled", on ? "True" : "False");
        AppSettings::instance().save();
        // Enable/disable the client (timer only runs when on)
        m_propForecast->setEnabled(on);
        // Show/hide the overlay on all panadapters immediately
        for (PanadapterApplet* applet : m_panStack->allApplets()) {
            applet->spectrumWidget()->setPropForecastVisible(on);
        }
        // If turning off, clear the stale values so they don't reappear
        if (!on) {
            for (PanadapterApplet* applet : m_panStack->allApplets()) {
                applet->spectrumWidget()->setPropForecast(-1, -1, -1);
            }
        }
    });

    m_keyboardShortcutsEnabled = AppSettings::instance()
        .value("KeyboardShortcutsEnabled", "False").toString() == "True";
    auto* kbAct = viewMenu->addAction("Keyboard Shortcuts");
    kbAct->setCheckable(true);
    kbAct->setChecked(m_keyboardShortcutsEnabled);
    connect(kbAct, &QAction::toggled, this, [this](bool on) {
        m_keyboardShortcutsEnabled = on;
        s_keyboardShortcutsEnabled = on;
        AppSettings::instance().setValue("KeyboardShortcutsEnabled", on ? "True" : "False");
        AppSettings::instance().save();
    });
    auto* configShortcutsAct = viewMenu->addAction("Configure Shortcuts...");
    configShortcutsAct->setMenuRole(QAction::NoRole); // prevent macOS auto-reparenting (#883)
    connect(configShortcutsAct, &QAction::triggered, this, [this] {
        ShortcutDialog dlg(&m_shortcutManager, this);
        dlg.exec();
        // Rebuild shortcuts in case bindings changed
        m_shortcutManager.rebuildShortcuts(this, shortcutGuard);
    });

    viewMenu->addSeparator();
    auto* heartbeatBlinkAct = viewMenu->addAction("Blink Status Indicator");
    heartbeatBlinkAct->setCheckable(true);
    heartbeatBlinkAct->setChecked(
        AppSettings::instance().value("HeartbeatBlinkEnabled", "True").toString() == "True");
    connect(heartbeatBlinkAct, &QAction::toggled, this, [this](bool on) {
        if (m_titleBar) m_titleBar->setBlinkEnabled(on);
    });
    // Keep the menu item in sync when the right-click on the indicator changes the setting
    if (m_titleBar) {
        connect(m_titleBar, &TitleBar::blinkEnabledChanged,
                heartbeatBlinkAct, &QAction::setChecked);
    }

    auto* helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("Getting Started...", this, [this]() {
        auto* dlg = new HelpDialog("Getting Started", ":/help/getting-started.md", this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setModal(false);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });
    helpMenu->addAction("AetherSDR Help...", this, [this]() {
        auto* dlg = new HelpDialog("AetherSDR Help", ":/help/aethersdr-help.md", this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setModal(false);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });
    helpMenu->addAction("Understanding Noise Cancellation...", this, [this]() {
        auto* dlg = new HelpDialog("Understanding Noise Cancellation", ":/help/understanding-noise-cancellation.md", this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setModal(false);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });
    auto* controlsHelpAction = helpMenu->addAction("Configuring AetherSDR Controls...", this, [this]() {
        auto* dlg = new HelpDialog("Configuring AetherSDR Controls", ":/help/configuring-aethersdr-controls.md", this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setModal(false);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });
    controlsHelpAction->setMenuRole(QAction::NoRole); // prevent macOS auto-reparenting (#883)
    auto* dataModesAction = helpMenu->addAction("Configuring Data Modes...", this, [this]() {
        auto* dlg = new HelpDialog("Configuring Data Modes", ":/help/understanding-data-modes.md", this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setModal(false);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });
    dataModesAction->setMenuRole(QAction::NoRole); // prevent macOS auto-reparenting (#883)
    helpMenu->addAction("Contributing to AetherSDR...", this, [this]() {
        auto* dlg = new HelpDialog("Contributing to AetherSDR", ":/help/contributing-to-aethersdr.md", this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setModal(false);
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
    });
    helpMenu->addSeparator();
    helpMenu->addAction(QString::fromUtf8("Submit your idea... \xF0\x9F\x92\xA1"),
                        this, [this]() {
        if (m_titleBar) m_titleBar->showFeatureRequestDialog();
    });
    helpMenu->addAction("Support...", this, [this]() {
        auto* dlg = new SupportDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setRadioModel(&m_radioModel);
        dlg->show();
        dlg->raise();
    });
    helpMenu->addAction("Slice Troubleshooting...", this, [this]() {
        SliceTroubleshootingDialog dlg(&m_radioModel, m_audio, this,
                                       [this]() { return buildControlDevicesSnapshot(); });
        dlg.exec();
    });
    helpMenu->addAction("What's New...", this, [this]() {
        if (m_whatsNewDialog) {
            m_whatsNewDialog->raise();
            m_whatsNewDialog->activateWindow();
            return;
        }
        m_whatsNewDialog = WhatsNewDialog::showAll(this);
    });
    helpMenu->addSeparator();
    helpMenu->addAction("About AetherSDR", this, [this]{
        auto* dlg = new QDialog(this);
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->setWindowTitle("About AetherSDR");
        dlg->setFixedWidth(380);
        dlg->setStyleSheet("QDialog { background: #0f0f1a; }");

        auto* vbox = new QVBoxLayout(dlg);
        vbox->setSpacing(8);
        vbox->setContentsMargins(16, 16, 16, 16);

        // Icon
        auto* iconLbl = new QLabel;
        iconLbl->setPixmap(QPixmap(":/icon.png").scaled(96, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        iconLbl->setAlignment(Qt::AlignCenter);
        vbox->addWidget(iconLbl);

        // Header
        auto* header = new QLabel(QString(
            "<div style='text-align:center;'>"
            "<h2 style='margin-bottom:2px; color:#c8d8e8;'>AetherSDR</h2>"
            "<p style='margin-top:0; color:#8aa8c0;'>v%1</p>"
            "<p style='color:#c8d8e8;'>Linux-native SmartSDR-compatible client<br>"
            "for FlexRadio transceivers.</p>"
            "<p style='font-size:11px; color:#6a8090;'>"
            "Built with Qt %2 &middot; C++20<br>"
            "Compiled: %3</p>"
            "</div>")
            .arg(QCoreApplication::applicationVersion(), qVersion(),
                 QStringLiteral(__DATE__)));
        header->setAlignment(Qt::AlignCenter);
        header->setWordWrap(true);
        vbox->addWidget(header);

        // Separator
        auto* sep1 = new QFrame;
        sep1->setFrameShape(QFrame::HLine);
        sep1->setStyleSheet("color: #304050;");
        vbox->addWidget(sep1);

        // Contributors label
        auto* contribTitle = new QLabel("<b style='color:#c8d8e8;'>Contributors</b>");
        contribTitle->setAlignment(Qt::AlignCenter);
        vbox->addWidget(contribTitle);

        // Scrollable contributors list
        auto* contribLabel = new QLabel("Jeremy (KK7GWY)<br>Claude &middot; Anthropic<br>rfoust<br>Ian (M7HNF)<br>VE3NEM<br>jensenpat<br>chibondking<br>Dependabot");
        contribLabel->setAlignment(Qt::AlignCenter);
        contribLabel->setStyleSheet("QLabel { color: #c8d8e8; font-size: 11px; }");
        contribLabel->setWordWrap(true);

        auto* scroll = new QScrollArea;
        scroll->setWidget(contribLabel);
        scroll->setWidgetResizable(true);
        scroll->setFixedHeight(80);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet(
            "QScrollArea { background: #0a0a14; border: 1px solid #203040; border-radius: 4px; }"
            "QScrollBar:vertical { background: #0a0a14; width: 6px; }"
            "QScrollBar::handle:vertical { background: #304050; border-radius: 3px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
        vbox->addWidget(scroll);

        // Separator
        auto* sep2 = new QFrame;
        sep2->setFrameShape(QFrame::HLine);
        sep2->setStyleSheet("color: #304050;");
        vbox->addWidget(sep2);

        // Footer
        auto* footer = new QLabel(
            "<div style='text-align:center;'>"
            "<p style='font-size:11px; color:#8aa8c0;'>"
            "&copy; 2026 AetherSDR Contributors<br>"
            "Licensed under "
            "<a href='https://www.gnu.org/licenses/gpl-3.0.html' style='color:#00b4d8;'>GPLv3</a></p>"
            "<p style='font-size:11px;'>"
            "<a href='https://github.com/ten9876/AetherSDR' style='color:#00b4d8;'>"
            "github.com/ten9876/AetherSDR</a></p>"
            "<p style='font-size:10px; color:#6a8090;'>"
            "SmartSDR protocol &copy; FlexRadio Systems</p>"
            "<p style='font-size:10px; color:#6a8090;'>"
            "HF propagation forecasts provided by "
            "<a href='https://www.hamqsl.com/' style='color:#8aa8c0;'>hamqsl.com</a></p>"
            "</div>");
        footer->setAlignment(Qt::AlignCenter);
        footer->setOpenExternalLinks(true);
        footer->setWordWrap(true);
        vbox->addWidget(footer);

        // OK button
        auto* okBtn = new QPushButton("OK");
        okBtn->setStyleSheet(
            "QPushButton { background: #00b4d8; color: #0f0f1a; font-weight: bold; "
            "border-radius: 4px; padding: 6px 24px; }"
            "QPushButton:hover { background: #00c8f0; }");
        connect(okBtn, &QPushButton::clicked, dlg, &QDialog::close);
        vbox->addWidget(okBtn, 0, Qt::AlignCenter);

        dlg->show();

        // Fetch live contributor list from GitHub API
        auto* nam = new QNetworkAccessManager(dlg);
        auto* reply = nam->get(QNetworkRequest(
            QUrl("https://api.github.com/repos/ten9876/AetherSDR/contributors")));
        connect(reply, &QNetworkReply::finished, dlg, [contribLabel, reply] {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) return;
            auto doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isArray()) return;
            QStringList names;
            names << "Jeremy (KK7GWY)" << "Claude &middot; Anthropic";
            for (const auto& val : doc.array()) {
                auto obj = val.toObject();
                QString login = obj.value("login").toString();
                if (login.isEmpty() || login == "ten9876") continue;
                if (login.contains("[bot]"))
                    login = login.replace("[bot]", "");
                if (!names.contains(login))
                    names << login;
            }
            contribLabel->setText(names.join("<br>"));
        });
    });
}

void MainWindow::buildUI()
{
    // ── Title bar + central splitter ─────────────────────────────────────────
    m_titleBar = new TitleBar(this);
    // Embed the menu bar into the title bar (left side)
    m_titleBar->setMenuBar(menuBar());
    connect(m_titleBar, &TitleBar::multiFlexClicked, this, [this] {
        MultiFlexDialog dlg(&m_radioModel, this);
        dlg.exec();
    });
    connect(m_titleBar, &TitleBar::minimalModeRequested, this, [this]() {
        toggleMinimalMode(!m_minimalMode);
        if (m_minimalModeAction) {
            QSignalBlocker b(m_minimalModeAction);
            m_minimalModeAction->setChecked(m_minimalMode);
        }
    });

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

    // Connection panel — modeless dialog with standard decorations (#560, #574)
    m_connPanel = new ConnectionPanel(this);
    m_connPanel->setWindowFlags(Qt::Dialog);
    m_connPanel->setWindowTitle("Connect to Radio");
    m_connPanel->setMinimumSize(640, 580);
    m_connPanel->resize(760, 660);
    m_connPanel->hide();

    // CWX panel — left of spectrum, hidden by default
    m_cwxPanel = new CwxPanel(&m_radioModel.cwxModel(), splitter);
    // Provide state probes so CWX can guard its F1-F12 / ESC app-wide
    // shortcuts on mode + transmit state (#1552).
    m_cwxPanel->setActiveModeProvider([this]() {
        auto* s = activeSlice();
        return s ? s->mode() : QString();
    });
    m_cwxPanel->setTransmittingProvider([this]() {
        return m_radioModel.transmitModel().isTransmitting();
    });
    splitter->addWidget(m_cwxPanel);
    m_cwxPanel->hide();

    // DVK panel — left of spectrum, hidden by default (mutually exclusive with CWX)
    m_dvkPanel = new DvkPanel(&m_radioModel.dvkModel(), splitter);
    auto* dvkTransfer = new DvkWavTransfer(&m_radioModel, this);
    m_dvkPanel->setWavTransfer(dvkTransfer);
    splitter->addWidget(m_dvkPanel);
    m_dvkPanel->hide();

    // Centre — panadapter stack (one or more FFT + waterfall panes)
    m_panStack = new PanadapterStack(splitter);
    m_panApplet = nullptr;  // ensure setActivePanApplet sees a change
    setActivePanApplet(m_panStack->addPanadapter("default"));
    splitter->addWidget(m_panStack);

    // Band stack panel signal wiring
    auto* bsPanel = m_panStack->bandStackPanel();

    connect(bsPanel, &BandStackPanel::addRequested, this, [this]() {
        auto* slice = activeSlice();
        if (!slice) return;
        BandStackEntry entry;
        entry.frequencyMhz = slice->frequency();
        entry.mode = slice->mode();
        entry.filterLow = slice->filterLow();
        entry.filterHigh = slice->filterHigh();
        entry.rxAntenna = slice->rxAntenna();
        entry.txAntenna = slice->txAntenna();
        entry.agcMode = slice->agcMode();
        entry.agcThreshold = slice->agcThreshold();
        entry.audioGain = static_cast<int>(slice->audioGain());
        entry.nbOn = slice->nbOn();
        entry.nbLevel = slice->nbLevel();
        entry.nrOn = slice->nrOn();
        entry.nrLevel = slice->nrLevel();
        entry.createdAtMs = QDateTime::currentMSecsSinceEpoch();
        if (auto* pan = m_radioModel.activePanadapter()) {
            entry.wnbOn = pan->wnbActive();
            entry.wnbLevel = pan->wnbLevel();
        }

        BandStackSettings::instance().addEntry(m_radioModel.serial(), entry);
        BandStackSettings::instance().save();
        m_panStack->bandStackPanel()->loadBookmarks(
            m_radioModel.serial(), m_bandPlanMgr);
    });
    connect(bsPanel, &BandStackPanel::recallRequested, this,
            [this](const BandStackEntry& e) {
        auto* slice = activeSlice();
        if (!slice) return;
        int id = slice->sliceId();

        // Mode first (affects filter ranges)
        if (slice->mode() != e.mode) {
            slice->setMode(e.mode);
        }
        applyTuneRequest(slice, e.frequencyMhz,
                         TuneIntent::AbsoluteJump, "bandstack-recall");
        // Filter
        if (e.filterLow != 0 || e.filterHigh != 0) {
            slice->setFilterWidth(e.filterLow, e.filterHigh);
        }
        // Antennas
        if (!e.rxAntenna.isEmpty() && e.rxAntenna != slice->rxAntenna()) {
            m_radioModel.sendCommand(QString("slice set %1 rxant=%2").arg(id).arg(e.rxAntenna));
        }
        if (!e.txAntenna.isEmpty() && e.txAntenna != slice->txAntenna()) {
            m_radioModel.sendCommand(QString("slice set %1 txant=%2").arg(id).arg(e.txAntenna));
        }
        // AGC
        if (!e.agcMode.isEmpty() && e.agcMode != slice->agcMode()) {
            m_radioModel.sendCommand(QString("slice set %1 agc_mode=%2").arg(id).arg(e.agcMode));
        }
        if (e.agcThreshold != slice->agcThreshold()) {
            m_radioModel.sendCommand(QString("slice set %1 agc_threshold=%2").arg(id).arg(e.agcThreshold));
        }
        // Volume
        if (static_cast<int>(slice->audioGain()) != e.audioGain) {
            slice->setAudioGain(static_cast<float>(e.audioGain));
        }
        // NB
        if (e.nbOn != slice->nbOn()) {
            m_radioModel.sendCommand(QString("slice set %1 nb=%2").arg(id).arg(e.nbOn ? 1 : 0));
        }
        if (e.nbLevel != slice->nbLevel()) {
            m_radioModel.sendCommand(QString("slice set %1 nb_level=%2").arg(id).arg(e.nbLevel));
        }
        // NR
        if (e.nrOn != slice->nrOn()) {
            m_radioModel.sendCommand(QString("slice set %1 nr=%2").arg(id).arg(e.nrOn ? 1 : 0));
        }
        if (e.nrLevel != slice->nrLevel()) {
            m_radioModel.sendCommand(QString("slice set %1 nr_level=%2").arg(id).arg(e.nrLevel));
        }
        // WNB (panadapter-level, not slice)
        if (auto* pan = m_radioModel.activePanadapter()) {
            if (e.wnbOn != pan->wnbActive()) {
                m_radioModel.sendCommand(
                    QString("display pan set %1 wnb=%2").arg(pan->panId()).arg(e.wnbOn ? 1 : 0));
            }
            if (e.wnbLevel != pan->wnbLevel()) {
                m_radioModel.sendCommand(
                    QString("display pan set %1 wnb_level=%2").arg(pan->panId()).arg(e.wnbLevel));
            }
        }
    });
    connect(bsPanel, &BandStackPanel::removeRequested, this,
            [this](int index) {
        BandStackSettings::instance().removeEntry(m_radioModel.serial(), index);
        BandStackSettings::instance().save();
        m_panStack->bandStackPanel()->loadBookmarks(
            m_radioModel.serial(), m_bandPlanMgr);
    });

    // Clear All — with confirmation to avoid accidental loss during contests
    connect(bsPanel, &BandStackPanel::clearAllRequested, this, [this]() {
        if (BandStackSettings::instance().entries(m_radioModel.serial()).isEmpty())
            return;
        auto answer = QMessageBox::question(
            this, "Clear All Bookmarks",
            "Remove all band stack bookmarks?",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
        BandStackSettings::instance().clearAllEntries(m_radioModel.serial());
        BandStackSettings::instance().save();
        m_panStack->bandStackPanel()->loadBookmarks(
            m_radioModel.serial(), m_bandPlanMgr);
    });

    // Clear band bookmarks (from grouped header right-click)
    connect(bsPanel, &BandStackPanel::clearBandRequested, this,
            [this](double lowMhz, double highMhz) {
        BandStackSettings::instance().clearBandEntries(
            m_radioModel.serial(), lowMhz, highMhz);
        BandStackSettings::instance().save();
        m_panStack->bandStackPanel()->loadBookmarks(
            m_radioModel.serial(), m_bandPlanMgr);
    });

    // Group by band toggle
    connect(bsPanel, &BandStackPanel::groupByBandChanged, this, [](bool grouped) {
        BandStackSettings::instance().setGroupByBand(grouped);
        BandStackSettings::instance().save();
    });

    // Auto-expiry setting changed
    connect(bsPanel, &BandStackPanel::autoExpiryChanged, this, [](int minutes) {
        BandStackSettings::instance().setAutoExpiryMinutes(minutes);
        BandStackSettings::instance().save();
    });

    // Auto-save dwell setting changed
    connect(bsPanel, &BandStackPanel::autoSaveDwellChanged, this, [this](int seconds) {
        BandStackSettings::instance().setAutoSaveDwellSeconds(seconds);
        BandStackSettings::instance().save();
        if (!m_bsAutoSaveTimer) return;
        if (seconds <= 0) {
            m_bsAutoSaveTimer->stop();
        } else {
            m_bsAutoSaveTimer->setInterval(seconds * 1000);
            m_bsAutoSaveTimer->start();
        }
    });

    // Auto-expiry timer — runs every 30s, prunes stale bookmarks.
    // Started on radio connect and stopped on disconnect so the tick doesn't
    // fire in an idle app with no radio to prune bookmarks for.
    m_bsExpiryTimer = new QTimer(this);
    m_bsExpiryTimer->setInterval(30000);
    connect(m_bsExpiryTimer, &QTimer::timeout, this, [this]() {
        int minutes = BandStackSettings::instance().autoExpiryMinutes();
        if (minutes <= 0) return;
        if (m_radioModel.serial().isEmpty()) return;
        qint64 maxAge = static_cast<qint64>(minutes) * 60 * 1000;
        int removed = BandStackSettings::instance().removeExpiredEntries(
            m_radioModel.serial(), maxAge);
        if (removed > 0) {
            BandStackSettings::instance().save();
            m_panStack->bandStackPanel()->loadBookmarks(
                m_radioModel.serial(), m_bandPlanMgr);
        }
    });

    // Band-stack auto-save: single-shot per dwell window.  Reset on every
    // active-slice frequency change and on active-slice change; fires once
    // when the slice has been parked on the same freq long enough.
    m_bsAutoSaveTimer = new QTimer(this);
    m_bsAutoSaveTimer->setSingleShot(true);
    connect(m_bsAutoSaveTimer, &QTimer::timeout, this, [this]() {
        const int dwellSec = BandStackSettings::instance().autoSaveDwellSeconds();
        if (dwellSec <= 0) return;
        if (m_radioModel.serial().isEmpty()) return;
        if (m_radioModel.transmitModel().isTransmitting()) return;
        if (QDateTime::currentMSecsSinceEpoch() < m_bsConnectGraceUntilMs) return;

        auto* slice = activeSlice();
        if (!slice) return;
        const double freqMhz = slice->frequency();

        // Skip if any existing entry is within ±100 Hz on this radio
        // (avoids re-stacking the exact same station after a brief retune).
        auto existing = BandStackSettings::instance().entries(m_radioModel.serial());
        for (const auto& e : existing) {
            if (std::abs(e.frequencyMhz - freqMhz) < 0.0001) return;
        }

        // Per-band cap on auto-saved entries: keep at most 5 in the same
        // band before overwriting the oldest auto-saved entry there.  Manual
        // entries are never displaced — they hold their slot indefinitely.
        constexpr int kMaxAutoPerBand = 5;
        int bandIdx = -1;
        for (int b = 0; b < kBandCount; ++b) {
            if (freqMhz >= kBands[b].lowMhz && freqMhz <= kBands[b].highMhz) {
                bandIdx = b;
                break;
            }
        }
        if (bandIdx >= 0) {
            const double bandLow = kBands[bandIdx].lowMhz;
            const double bandHigh = kBands[bandIdx].highMhz;
            int autoCount = 0;
            int oldestAutoIdx = -1;
            qint64 oldestAutoMs = std::numeric_limits<qint64>::max();
            for (int i = 0; i < existing.size(); ++i) {
                const auto& e = existing[i];
                if (!e.autoSaved) continue;
                if (e.frequencyMhz < bandLow || e.frequencyMhz > bandHigh) continue;
                ++autoCount;
                if (e.createdAtMs > 0 && e.createdAtMs < oldestAutoMs) {
                    oldestAutoMs = e.createdAtMs;
                    oldestAutoIdx = i;
                }
            }
            if (autoCount >= kMaxAutoPerBand && oldestAutoIdx >= 0) {
                BandStackSettings::instance().removeEntry(
                    m_radioModel.serial(), oldestAutoIdx);
            }
        }

        BandStackEntry entry;
        entry.autoSaved = true;
        entry.frequencyMhz = freqMhz;
        entry.mode = slice->mode();
        entry.filterLow = slice->filterLow();
        entry.filterHigh = slice->filterHigh();
        entry.rxAntenna = slice->rxAntenna();
        entry.txAntenna = slice->txAntenna();
        entry.agcMode = slice->agcMode();
        entry.agcThreshold = slice->agcThreshold();
        entry.audioGain = static_cast<int>(slice->audioGain());
        entry.nbOn = slice->nbOn();
        entry.nbLevel = slice->nbLevel();
        entry.nrOn = slice->nrOn();
        entry.nrLevel = slice->nrLevel();
        entry.createdAtMs = QDateTime::currentMSecsSinceEpoch();
        if (auto* pan = m_radioModel.activePanadapter()) {
            entry.wnbOn = pan->wnbActive();
            entry.wnbLevel = pan->wnbLevel();
        }
        BandStackSettings::instance().addEntry(m_radioModel.serial(), entry);
        BandStackSettings::instance().save();
        m_panStack->bandStackPanel()->loadBookmarks(
            m_radioModel.serial(), m_bandPlanMgr);
    });
    refreshMemoryBrowsePanel();

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

        // Update m_panApplet for the new active pan
        if (auto* applet = m_panStack->panadapter(panId))
            setActivePanApplet(applet);

        // Show/hide CW decode panel based on the new active pan's slice mode
        for (auto* sl : m_radioModel.slices()) {
            if (sl->panId() == panId) {
                bool isCw = (sl->mode() == "CW" || sl->mode() == "CWL");
                bool decodeOn = AppSettings::instance().value("CwDecodeOverlay", "True").toString() == "True";
                if (auto* applet = m_panStack->panadapter(panId))
                    applet->setCwPanelVisible(isCw && decodeOn);
                if (isCw && !m_cwDecoder.isRunning())
                    m_cwDecoder.start();
                else if (!isCw && m_cwDecoder.isRunning())
                    m_cwDecoder.stop();
                break;
            }
        }
    });
    splitter->setStretchFactor(0, 0);  // CWX panel: fixed width
    splitter->setStretchFactor(1, 0);  // DVK panel: fixed width
    splitter->setStretchFactor(2, 1);  // PanStack: stretch
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);

    // Right — applet panel (includes S-Meter)
    m_appletPanel = new AppletPanel(splitter);
    splitter->addWidget(m_appletPanel);
    splitter->setStretchFactor(3, 0);
    splitter->setCollapsible(3, false);

    // Set initial splitter sizes: CWX=0, DVK=0 (both hidden), center=stretch, right=310
    const int centerWidth = qMax(400, width() - 310);
    splitter->setSizes({0, 0, centerWidth, 310});

    // Restore applet panel visibility
    if (AppSettings::instance().value("AppletPanelVisible", "True").toString() != "True")
        m_appletPanel->hide();

    // ── Status bar (SmartSDR-style, double height) ─────────────────────
    statusBar()->setFixedHeight(46);
    statusBar()->setSizeGripEnabled(false);
    // Bottom-right resize grip — direct child of MainWindow (not parented
    // into the status bar, whose stylesheet was suppressing the grip's
    // dotted texture).  Position is maintained in resizeEvent.
    m_sizeGrip = new QSizeGrip(this);
    m_sizeGrip->setFixedSize(16, 16);
    m_sizeGrip->raise();
    m_sizeGrip->setVisible(
        AppSettings::instance().value("FramelessWindow", "True").toString() == "True");
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
    // +PAN icon: mini spectrum with + overlay
    {
        QPixmap pm(36, 28);
        pm.fill(Qt::transparent);
        QPainter pp(&pm);
        pp.setRenderHint(QPainter::Antialiasing);
        // Spectrum line — flat noise floor with two signal peaks
        pp.setPen(QPen(QColor(255, 255, 255, 128), 1.8));
        const QPointF pts[] = {
            {0,22}, {4,21}, {7,20}, {9,14}, {10,7}, {11,14}, {13,20},
            {16,21}, {18,20}, {20,10}, {21,4}, {22,10}, {24,20},
            {27,21}, {30,22}
        };
        pp.drawPolyline(pts, 15);
        // + sign in upper-right
        pp.setPen(QPen(QColor(255, 255, 255, 200), 2.2));
        pp.drawLine(30, 4, 30, 14);   // vertical
        pp.drawLine(25, 9, 35, 9);    // horizontal
        pp.end();
        // Band stack toggle: 3 vertically stacked circles
        {
            m_bandStackIndicator = new QLabel;
            m_bandStackIndicator->setPixmap(buildBandStackIndicatorPixmap(false));
            m_bandStackIndicator->setCursor(Qt::PointingHandCursor);
            m_bandStackIndicator->setToolTip("Open band stack panel");
            m_bandStackIndicator->installEventFilter(this);
            hbox->addWidget(m_bandStackIndicator);
        }

        hbox->addSpacing(8);

        auto* addPanBtn = new QLabel;
        addPanBtn->setPixmap(pm);
        addPanBtn->setCursor(Qt::PointingHandCursor);
        addPanBtn->setToolTip("Add Panadapter");
        addPanBtn->installEventFilter(this);
        hbox->addWidget(addPanBtn);
        m_addPanLabel = addPanBtn;
    }

    hbox->addSpacing(8);

    bool panelVis = AppSettings::instance().value("AppletPanelVisible", "True").toString() == "True";
    m_panelToggle = new QLabel(QString::fromUtf8("\xe2\x98\xb0"));  // ☰ hamburger
    m_panelToggle->setStyleSheet(panelVis
        ? "QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 22px; }"
        : "QLabel { color: #404858; font-weight: bold; font-size: 22px; }");
    m_panelToggle->setAlignment(Qt::AlignBottom);
    m_panelToggle->setCursor(Qt::PointingHandCursor);
    m_panelToggle->setToolTip("Toggle applet panel");
    m_panelToggle->installEventFilter(this);
    hbox->addWidget(m_panelToggle);

    hbox->addSpacing(8);

    m_tnfIndicator = new QLabel("TNF");
    m_tnfIndicator->setStyleSheet("QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 24px; }");
    m_tnfIndicator->setCursor(Qt::PointingHandCursor);
    m_tnfIndicator->installEventFilter(this);
    hbox->addWidget(m_tnfIndicator);

    m_cwxIndicator = new QLabel("CWX");
    m_cwxIndicator->setStyleSheet(greyIndLg);
    m_cwxIndicator->setCursor(Qt::PointingHandCursor);
    m_cwxIndicator->setToolTip("CW Keyer — click to toggle");
    m_cwxIndicator->installEventFilter(this);
    hbox->addWidget(m_cwxIndicator);

    m_dvkIndicator = new QLabel("DVK");
    m_dvkIndicator->setStyleSheet(greyIndLg);
    m_dvkIndicator->setCursor(Qt::PointingHandCursor);
    m_dvkIndicator->setToolTip("Digital Voice Keyer — click to toggle");
    m_dvkIndicator->installEventFilter(this);
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
    m_radioVersionLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_radioVersionLabel->setAlignment(Qt::AlignCenter);
    radioVbox->addWidget(m_radioInfoLabel);
    radioVbox->addWidget(m_radioVersionLabel);
    hbox->addWidget(radioStack);

    // ── Center stretch → STATION → stretch ───────────────────────────────
    hbox->addStretch(1);

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
    // Reserve consistent width for the compact telemetry stacks so updates
    // do not cause the status bar to reshuffle as values change.
    constexpr int kTelemetryStackMinWidth = 84;

    // GPS satellites (top) + lock status (bottom) stacked
    auto* gpsStack = new QWidget;
    gpsStack->setMinimumWidth(kTelemetryStackMinWidth);
    auto* gpsVbox = new QVBoxLayout(gpsStack);
    gpsVbox->setContentsMargins(0, 0, 0, 0);
    gpsVbox->setSpacing(0);
    m_gpsLabel = new QLabel("");
    m_gpsLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_gpsLabel->setAlignment(Qt::AlignCenter);
    m_gpsStatusLabel = new QLabel("");
    m_gpsStatusLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_gpsStatusLabel->setAlignment(Qt::AlignCenter);
    gpsVbox->addWidget(m_gpsLabel);
    gpsVbox->addWidget(m_gpsStatusLabel);
    hbox->addWidget(gpsStack);

    addSep();

    // CPU (top) + Memory (bottom) stacked
    {
        auto* cpuStack = new QWidget;
        cpuStack->setMinimumWidth(kTelemetryStackMinWidth);
        auto* cpuVbox = new QVBoxLayout(cpuStack);
        cpuVbox->setContentsMargins(0, 0, 0, 0);
        cpuVbox->setSpacing(0);
        m_cpuLabel = new QLabel("CPU: \u2014");
        m_cpuLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
        m_cpuLabel->setAlignment(Qt::AlignCenter);
        m_cpuLabel->setToolTip("AetherSDR process CPU usage");
        m_memLabel = new QLabel("Mem: \u2014");
        m_memLabel->setStyleSheet("QLabel { color: #607080; font-size: 12px; }");
        m_memLabel->setAlignment(Qt::AlignCenter);
        m_memLabel->setToolTip("AetherSDR process memory (RSS)");
        cpuVbox->addWidget(m_cpuLabel);
        cpuVbox->addWidget(m_memLabel);
        hbox->addWidget(cpuStack);

        m_cpuTimer = new QTimer(this);
        m_cpuTimer->setInterval(1500);
        connect(m_cpuTimer, &QTimer::timeout, this, [this]() {
            double cpuPct = -1.0;
#ifdef Q_OS_WIN
            static FILETIME prevKernel{}, prevUser{};
            static qint64 prevWall = 0;
            FILETIME creation, exit, kernel, user;
            if (GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
                auto toUs = [](const FILETIME& ft) -> qint64 {
                    return (static_cast<qint64>(ft.dwHighDateTime) << 32 | ft.dwLowDateTime) / 10;
                };
                qint64 now = QDateTime::currentMSecsSinceEpoch() * 1000;
                qint64 cpuUs = toUs(kernel) + toUs(user);
                qint64 prevCpuUs = toUs(prevKernel) + toUs(prevUser);
                if (prevWall > 0) {
                    qint64 wallDelta = now - prevWall;
                    qint64 cpuDelta = cpuUs - prevCpuUs;
                    if (wallDelta > 0)
                        cpuPct = 100.0 * cpuDelta / wallDelta / QThread::idealThreadCount();
                }
                prevKernel = kernel;
                prevUser = user;
                prevWall = now;
            }
#else
            // POSIX (Linux + macOS): getrusage
            static qint64 prevUserUs = 0, prevSysUs = 0, prevWallMs = 0;
            struct rusage ru;
            if (getrusage(RUSAGE_SELF, &ru) == 0) {
                qint64 userUs = ru.ru_utime.tv_sec * 1000000LL + ru.ru_utime.tv_usec;
                qint64 sysUs  = ru.ru_stime.tv_sec * 1000000LL + ru.ru_stime.tv_usec;
                qint64 nowMs  = QDateTime::currentMSecsSinceEpoch();
                if (prevWallMs > 0) {
                    qint64 wallDelta = (nowMs - prevWallMs) * 1000; // to microseconds
                    qint64 cpuDelta  = (userUs - prevUserUs) + (sysUs - prevSysUs);
                    if (wallDelta > 0)
                        cpuPct = 100.0 * cpuDelta / wallDelta / QThread::idealThreadCount();
                }
                prevUserUs = userUs;
                prevSysUs  = sysUs;
                prevWallMs = nowMs;
            }
#endif
            if (cpuPct >= 0.0) {
                QString color = "#8aa8c0";
                if (cpuPct >= 80.0) color = "#e05050";
                else if (cpuPct >= 50.0) color = "#f0c040";
                m_cpuLabel->setText(QString("CPU: %1%").arg(cpuPct, 0, 'f', 1));
                m_cpuLabel->setStyleSheet(QString("QLabel { color: %1; font-size: 12px; }").arg(color));
            }

            // Memory (RSS)
#ifdef Q_OS_WIN
            PROCESS_MEMORY_COUNTERS pmc;
            if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
                double mb = pmc.WorkingSetSize / (1024.0 * 1024.0);
                m_memLabel->setText(QString("Mem: %1 MB").arg(static_cast<int>(mb)));
            }
#else
            // getrusage ru_maxrss is in KB on Linux, bytes on macOS
            struct rusage ruMem;
            if (getrusage(RUSAGE_SELF, &ruMem) == 0) {
#ifdef Q_OS_MAC
                double mb = ruMem.ru_maxrss / (1024.0 * 1024.0);
#else
                double mb = ruMem.ru_maxrss / 1024.0;
#endif
                m_memLabel->setText(QString("Mem: %1 MB").arg(static_cast<int>(mb)));
            }
#endif
        });
        m_cpuTimer->start();
    }

    addSep();

    // PA temp (top) + supply voltage (bottom) stacked
    auto* paStack = new QWidget;
    paStack->setMinimumWidth(kTelemetryStackMinWidth);
    auto* paVbox = new QVBoxLayout(paStack);
    paVbox->setContentsMargins(0, 0, 0, 0);
    paVbox->setSpacing(0);
    m_paTempLabel = new QLabel("");
    m_paTempLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_paTempLabel->setAlignment(Qt::AlignCenter);
    m_paTempLabel->setCursor(Qt::PointingHandCursor);
    m_paTempLabel->installEventFilter(this);
    updatePaTempLabel();
    m_supplyVoltLabel = new QLabel("");
    m_supplyVoltLabel->setStyleSheet("QLabel { color: #607080; font-size: 12px; }");
    m_supplyVoltLabel->setAlignment(Qt::AlignCenter);
    paVbox->addWidget(m_paTempLabel);
    paVbox->addWidget(m_supplyVoltLabel);
    hbox->addWidget(paStack);

    addSep();

    // Network label (top) + quality (bottom) stacked
    auto* netStack = new QWidget;
    netStack->setMinimumWidth(kTelemetryStackMinWidth);
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
    m_networkLabel->setToolTip(buildNetworkTooltip(m_radioModel));
    m_networkLabel->installEventFilter(this);
    netVbox->addWidget(m_networkLabel);
    hbox->addWidget(netStack);

    addSep();

    m_tgxlIndicator = new QLabel;
    m_tgxlIndicator->setTextFormat(Qt::RichText);
    m_tgxlIndicator->setAlignment(Qt::AlignCenter);
    m_tgxlIndicator->setCursor(Qt::PointingHandCursor);
    m_tgxlIndicator->setToolTip("Tuner Genius XL — click to cycle OPERATE/BYPASS/STANDBY");
    m_tgxlIndicator->installEventFilter(this);
    m_tgxlIndicator->setVisible(false);
    hbox->addWidget(m_tgxlIndicator);

    addSep();

    m_pgxlIndicator = new QLabel;
    m_pgxlIndicator->setTextFormat(Qt::RichText);
    m_pgxlIndicator->setAlignment(Qt::AlignCenter);
    m_pgxlIndicator->setCursor(Qt::PointingHandCursor);
    m_pgxlIndicator->setToolTip("Power Genius XL — click to toggle OPERATE/STANDBY");
    m_pgxlIndicator->installEventFilter(this);
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
    timeStack->setMinimumWidth(kTelemetryStackMinWidth);
    auto* timeVbox = new QVBoxLayout(timeStack);
    timeVbox->setContentsMargins(0, 0, 0, 0);
    timeVbox->setSpacing(0);
    m_gridLabel = new QLabel("");
    m_gridLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_gridLabel->setAlignment(Qt::AlignCenter);
    m_gridLabel->setMinimumWidth(kTelemetryStackMinWidth);
    m_gpsDateLabel = new QLabel("");
    m_gpsDateLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_gpsDateLabel->setAlignment(Qt::AlignCenter);
    m_gpsDateLabel->setMinimumWidth(kTelemetryStackMinWidth);
    m_gpsTimeLabel = new QLabel("");
    m_gpsTimeLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 12px; }");
    m_gpsTimeLabel->setAlignment(Qt::AlignCenter);
    m_gpsTimeLabel->setMinimumWidth(kTelemetryStackMinWidth);
    timeVbox->addWidget(m_gridLabel);
    timeVbox->addWidget(m_gpsDateLabel);
    timeVbox->addWidget(m_gpsTimeLabel);
    hbox->addWidget(timeStack);

    statusBar()->addWidget(container, 1);
    updateBandStackIndicator();
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

        // Slice tab toggle is initialized from infoChanged when radio
        // reports its actual slice capacity (#1278).

        // Show DIV button on dual-SCU radios
        {
            const QString& model = m_radioModel.model();
            bool divAllowed = model.contains("6500") || model.contains("6600")
                           || model.contains("6700") || model.contains("8600")
                           || model.contains("AU-520");
            // Set diversity allowed on all existing VFO widgets (including Slice A at startup) (#1503)
            if (m_panStack) {
                for (auto* pan : m_radioModel.panadapters()) {
                    if (auto* sw = m_panStack->spectrum(pan->panId())) {
                        for (auto* slice : m_radioModel.slices()) {
                            if (auto* vfo = sw->vfoWidget(slice->sliceId())) {
                                vfo->setDiversityAllowed(divAllowed);
                            }
                        }
                    }
                }
            }
        }
        // Only start the local RX audio sink if the user wants audio routed
        // to the PC. When PC Audio is off we may still request a
        // remote_audio_rx stream for TCI clients; the sink should stay
        // silent in that case. The PC Audio toggle handler starts/stops
        // the sink when the user flips it.
        // Always sync the button to the setting here so any divergence
        // (e.g. from a profile load before connect) is corrected (#1536).
        {
            const bool pcAudio = AppSettings::instance().value("PcAudioEnabled", "True").toString() == "True";
            m_titleBar->setPcAudioEnabled(pcAudio);
            if (pcAudio)
                audioStartRx();
        }
        updateNr2Availability();  // Disable NR2 if connected via SmartLink/Opus (#1597)
        // TX audio stream will start when the radio assigns a stream ID
        // Auto-hide the connection dialog on successful connect
        m_connPanel->hide();

        // Close reconnect dialog if it was showing
        if (m_reconnectDlg) {
            m_reconnectDlg->close();
            m_reconnectDlg->deleteLater();
            m_reconnectDlg = nullptr;
        }

        // Load band stack bookmarks for this radio
        BandStackSettings::instance().load();
        // Prune expired entries on startup
        int expiryMin = BandStackSettings::instance().autoExpiryMinutes();
        if (expiryMin > 0) {
            qint64 maxAge = static_cast<qint64>(expiryMin) * 60 * 1000;
            if (BandStackSettings::instance().removeExpiredEntries(
                    m_radioModel.serial(), maxAge) > 0) {
                BandStackSettings::instance().save();
            }
        }
        // Apply saved UI preferences to the panel
        m_panStack->bandStackPanel()->setGrouped(
            BandStackSettings::instance().groupByBand());
        m_panStack->bandStackPanel()->setAutoExpiryMinutes(expiryMin);
        m_panStack->bandStackPanel()->setAutoSaveDwellSeconds(
            BandStackSettings::instance().autoSaveDwellSeconds());

        // 5-second grace window after connect — suppresses an auto-save fire
        // while the radio finishes pushing initial slice/pan state.
        m_bsConnectGraceUntilMs = QDateTime::currentMSecsSinceEpoch() + 5000;
        m_panStack->bandStackPanel()->loadBookmarks(
            m_radioModel.serial(), m_bandPlanMgr);
        refreshMemoryBrowsePanel();
        updateBandStackIndicator();

        // Start the auto-expiry timer now that we have a radio to prune for
        if (m_bsExpiryTimer && !m_bsExpiryTimer->isActive())
            m_bsExpiryTimer->start();

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
            if (m_appletPanel && m_appletPanel->catControlApplet())
                m_appletPanel->catControlApplet()->setTcpEnabled(true);
        }
        // Auto-start 8-channel CAT virtual serial ports if enabled
        if (as.value("AutoStartCAT", "False").toString() == "True") {
            for (int i = 0; i < kCatChannels; ++i) {
                if (m_rigctlPtys[i] && !m_rigctlPtys[i]->isRunning()) {
                    m_rigctlPtys[i]->start();
                    qDebug() << "AutoStart: PTY ch" << i
                             << m_rigctlPtys[i]->symlinkPath();
                }
            }
            if (m_appletPanel && m_appletPanel->catControlApplet())
                m_appletPanel->catControlApplet()->setPtyEnabled(true);
        }
#ifdef HAVE_WEBSOCKETS
        // Auto-start TCI WebSocket server if enabled
        if (as.value("AutoStartTCI", "False").toString() == "True") {
            if (m_tciServer && !m_tciServer->isRunning()) {
                int tciPort = as.value("TciPort", "50001").toInt();
                m_tciServer->start(static_cast<quint16>(tciPort));
                qDebug() << "AutoStart: TCI on port" << tciPort
                         << " running=" << m_tciServer->isRunning();
            }
            // Only light up the Enable button if the server actually bound —
            // otherwise the UI shows a green "Enable" while the port is in use.
            if (m_appletPanel && m_appletPanel->tciApplet())
                m_appletPanel->tciApplet()->setTciEnabled(
                    m_tciServer && m_tciServer->isRunning());
        }
#endif
        // Populate XVTR bands after radio status settles, and refresh
        // whenever XVTR config changes (add/remove/rename). (#571)
        auto refreshXvtr = [this]() {
            if (!m_radioModel.isConnected()) return;
            QVector<SpectrumOverlayMenu::XvtrBand> xvtrBands;
            for (const auto& x : m_radioModel.xvtrList()) {
                if (x.isValid)
                    xvtrBands.append({x.name, x.rfFreq});
            }
            for (auto* applet : m_panStack->allApplets())
                applet->spectrumWidget()->overlayMenu()->setXvtrBands(xvtrBands);
        };
        QTimer::singleShot(2000, this, refreshXvtr);
        connect(&m_radioModel, &RadioModel::infoChanged, this, refreshXvtr);

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
                if (startDax() && m_appletPanel && m_appletPanel->daxApplet())
                    m_appletPanel->daxApplet()->setDaxEnabled(true);
            });
        }
#endif
        // Auto-connect DX cluster if enabled
        {
            auto& cs = AppSettings::instance();
            if (cs.value("DxClusterAutoConnect", "False").toString() == "True") {
                QString host = cs.value("DxClusterHost", "dxc.nc7j.com").toString();
                quint16 cPort = static_cast<quint16>(cs.value("DxClusterPort", 7300).toInt());
                QString call = cs.value("DxClusterCallsign").toString();
                if (!call.isEmpty() && !m_dxCluster->isConnected())
                    QMetaObject::invokeMethod(m_dxCluster, [=] { m_dxCluster->connectToCluster(host, cPort, call); });
            }
            // Auto-connect RBN if enabled
            if (cs.value("RbnAutoConnect", "False").toString() == "True") {
                QString host = cs.value("RbnHost", "telnet.reversebeacon.net").toString();
                quint16 rPort = static_cast<quint16>(cs.value("RbnPort", 7000).toInt());
                QString call = cs.value("RbnCallsign").toString();
                if (call.isEmpty())
                    call = cs.value("DxClusterCallsign").toString();
                if (!call.isEmpty() && !m_rbnClient->isConnected())
                    QMetaObject::invokeMethod(m_rbnClient, [=] { m_rbnClient->connectToCluster(host, rPort, call); });
            }
            // Auto-start WSJT-X listener if enabled
            if (cs.value("WsjtxAutoStart", "False").toString() == "True") {
                QString wAddr = cs.value("WsjtxAddress", "224.0.0.1").toString();
                quint16 wPort = static_cast<quint16>(cs.value("WsjtxPort", 2237).toInt());
                if (!m_wsjtxClient->isListening())
                    QMetaObject::invokeMethod(m_wsjtxClient, [=] { m_wsjtxClient->startListening(wAddr, wPort); });
            }
            // Auto-start SpotCollector listener if enabled
            if (cs.value("SpotCollectorAutoStart", "False").toString() == "True") {
                quint16 scPort = static_cast<quint16>(cs.value("SpotCollectorPort", 9999).toInt());
                if (!m_spotCollectorClient->isListening())
                    QMetaObject::invokeMethod(m_spotCollectorClient, [=] { m_spotCollectorClient->startListening(scPort); });
            }
            // Auto-start POTA polling if enabled
            if (cs.value("PotaAutoStart", "False").toString() == "True") {
                int pInterval = cs.value("PotaPollInterval", 30).toInt();
                if (!m_potaClient->isPolling())
                    QMetaObject::invokeMethod(m_potaClient, [=] { m_potaClient->startPolling(pInterval); });
            }
#ifdef HAVE_WEBSOCKETS
            // Auto-start FreeDV Reporter if enabled
            if (cs.value("FreeDvAutoStart", "False").toString() == "True") {
                if (!m_freedvClient->isConnected())
                    QMetaObject::invokeMethod(m_freedvClient, [this] { m_freedvClient->startConnection(); });
            }
#endif
            // Auto-connect peripherals with manual IPs (#914)
            QString tgxlIp = cs.value("TGXL_ManualIp", "").toString();
            if (!tgxlIp.isEmpty() && !m_tgxlConn.isConnected()) {
                quint16 tgxlPort = static_cast<quint16>(cs.value("TGXL_ManualPort", "9010").toInt());
                m_tgxlConn.connectToTgxl(tgxlIp, tgxlPort);
            }
            QString pgxlIp = cs.value("PGXL_ManualIp", "").toString();
            if (!pgxlIp.isEmpty() && !m_pgxlConn.isConnected()) {
                quint16 pgxlPort = static_cast<quint16>(cs.value("PGXL_ManualPort", "9008").toInt());
                m_pgxlConn.connectToPgxl(pgxlIp, pgxlPort);
            }
            QString agIp = cs.value("AG_ManualIp", "").toString();
            if (!agIp.isEmpty() && !m_antennaGenius.isConnected()) {
                quint16 agPort = static_cast<quint16>(cs.value("AG_ManualPort", "9007").toInt());
                m_antennaGenius.connectToAddress(QHostAddress(agIp), agPort);
            }
        }
    } else {
        QMetaObject::invokeMethod(m_dxCluster, [=] { m_dxCluster->disconnect(); });
        QMetaObject::invokeMethod(m_rbnClient, [=] { m_rbnClient->disconnect(); });
        QMetaObject::invokeMethod(m_wsjtxClient, [=] { m_wsjtxClient->stopListening(); });
        QMetaObject::invokeMethod(m_spotCollectorClient, [=] { m_spotCollectorClient->stopListening(); });
        QMetaObject::invokeMethod(m_potaClient, [=] { m_potaClient->stopPolling(); });
#ifdef HAVE_WEBSOCKETS
        QMetaObject::invokeMethod(m_freedvClient, [this] { m_freedvClient->stopConnection(); });
#endif
        m_connStatusLabel->setText("Disconnected");
        m_radioInfoLabel->setText("");
        m_radioVersionLabel->setText("");
        m_stationLabel->setText("N0CALL");
        m_tnfIndicator->setStyleSheet("QLabel { color: #404858; font-weight: bold; font-size: 24px; }");
        m_panStack->bandStackPanel()->clear();
        if (m_bsExpiryTimer && m_bsExpiryTimer->isActive())
            m_bsExpiryTimer->stop();
        if (m_bsAutoSaveTimer && m_bsAutoSaveTimer->isActive())
            m_bsAutoSaveTimer->stop();
        m_panStack->setBandStackVisible(false);
        refreshMemoryBrowsePanel();
        updateBandStackIndicator();
        m_tgxlIndicator->setVisible(false);
        m_tgxlConn.disconnect();
        m_pgxlConn.disconnect();
        m_pgxlIndicator->setVisible(false);
        m_txIndicator->setStyleSheet("QLabel { color: rgba(255,255,255,128); font-weight: bold; font-size: 21px; }");
        m_txIndicator->setText("TX");
        m_connPanel->setStatusText("Not connected");
#if defined(Q_OS_MAC) || defined(HAVE_PIPEWIRE)
        stopDax();
#endif
        audioStopRx();
        audioStopTx();

        // Clear spectrum/waterfall so the display doesn't look frozen
        for (auto* applet : m_panStack->allApplets())
            applet->spectrumWidget()->clearDisplay();

        setPanadapterConnectionAnimation(!m_userDisconnected, "Reconnecting to radio…");

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
                setPanadapterConnectionAnimation(false);
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
    if (!m_reconnectDlg)
        setPanadapterConnectionAnimation(false);
}

void MainWindow::setPanadapterConnectionAnimation(bool visible, const QString& label)
{
    const QString nextLabel = label.trimmed().isEmpty()
        ? QStringLiteral("Connecting to radio…")
        : label.trimmed();
    m_panadapterConnectionAnimationVisible = visible;
    m_waitingForFirstPanadapterFrame = visible;
    m_panadapterConnectionAnimationLabel = visible ? nextLabel : QString();

    if (!m_panStack)
        return;

    for (auto* applet : m_panStack->allApplets()) {
        if (!applet)
            continue;
        if (auto* spectrumWidget = applet->spectrumWidget())
            spectrumWidget->setConnectionAnimationVisible(visible, nextLabel);
    }
}

void MainWindow::finishPanadapterConnectionAnimation()
{
    if (!m_waitingForFirstPanadapterFrame || !m_panadapterConnectionAnimationVisible)
        return;

    setPanadapterConnectionAnimation(false);
}

void MainWindow::syncMemorySpot(int memoryIndex)
{
    auto it = m_radioModel.memories().constFind(memoryIndex);
    if (it == m_radioModel.memories().constEnd()) {
        removeMemorySpot(memoryIndex);
        return;
    }

    const MemoryEntry& memory = it.value();
    if (memory.freq <= 0.0) {
        removeMemorySpot(memoryIndex);
        return;
    }

    QMap<QString, QString> kvs;
    kvs["callsign"] = memorySpotLabel(memory).replace(' ', QChar(0x7f));
    kvs["rx_freq"] = QString::number(memory.freq, 'f', 6);
    kvs["tx_freq"] = QString::number(memory.freq, 'f', 6);
    kvs["source"] = "Memory";
    kvs["mode"] = memory.mode;
    kvs["color"] = "#FFFFC857";
    const QString comment = memorySpotComment(memory);
    if (!comment.isEmpty())
        kvs["comment"] = QString(comment).replace(' ', QChar(0x7f));

    m_radioModel.spotModel().applySpotStatus(memorySpotId(memoryIndex), kvs);
}

void MainWindow::removeMemorySpot(int memoryIndex)
{
    m_radioModel.spotModel().removeSpot(memorySpotId(memoryIndex));
}

void MainWindow::clearMemorySpotFeed()
{
    QVector<int> ids;
    const auto& spots = m_radioModel.spotModel().spots();
    for (auto it = spots.cbegin(); it != spots.cend(); ++it) {
        if (it.value().source == "Memory")
            ids.append(it.key());
    }
    // Block signals during batch removal to avoid N marker rebuilds (#708)
    m_radioModel.spotModel().blockSignals(true);
    for (int id : ids)
        m_radioModel.spotModel().removeSpot(id);
    m_radioModel.spotModel().blockSignals(false);
    if (!ids.isEmpty())
        emit m_radioModel.spotModel().spotsRefreshed();
}

void MainWindow::rebuildMemorySpotFeed()
{
    for (auto it = m_radioModel.memories().cbegin(); it != m_radioModel.memories().cend(); ++it)
        syncMemorySpot(it.key());
}

void MainWindow::refreshMemoryBrowsePanel()
{
    if (!m_panStack)
        return;
    for (auto* applet : m_panStack->allApplets()) {
        if (!applet)
            continue;
        if (auto* menu = applet->spectrumWidget()->overlayMenu()) {
            menu->setMemories(m_radioModel.memories());
            // Update the MEM button badge showing which slice save/recall
            // will target on this pan — makes the fallback slice visible
            // when the active slice is on a different pan (#1781).
            auto* target = preferredMemorySlice(menu->panId());
            menu->setMemoryTargetSliceLetter(
                target ? QChar('A' + (target->sliceId() & 3)) : QChar());
        }
    }
}

void MainWindow::updateBandStackIndicator()
{
    if (!m_bandStackIndicator || !m_panStack)
        return;

    const bool visible = m_panStack->bandStackPanel()->isVisible();
    m_bandStackIndicator->setPixmap(buildBandStackIndicatorPixmap(visible));
    m_bandStackIndicator->setToolTip(visible ? "Close band stack panel"
                                             : "Open band stack panel");
}

bool MainWindow::activateMemorySpot(int memoryIndex, const QString& preferredPanId)
{
    auto* slice = preferredMemorySlice(preferredPanId);
    if (!slice) {
        statusBar()->showMessage(
            preferredPanId.isEmpty()
                ? "Open a slice before recalling a memory."
                : "Open a slice on this pan before recalling a memory.",
            3000);
        return false;
    }
    if (slice->isLocked()) {
        statusBar()->showMessage("Unlock the target slice before recalling a memory.", 3000);
        return false;
    }

    const auto it = m_radioModel.memories().constFind(memoryIndex);
    if (it == m_radioModel.memories().constEnd())
        return false;

    const int sliceId = slice->sliceId();
    if (!activeSlice() || activeSlice()->sliceId() != sliceId)
        setActiveSlice(sliceId);

    slice = m_radioModel.slice(sliceId);
    if (!slice)
        return false;

    m_pendingMemoryRevealSliceId = sliceId;
    m_radioModel.sendCommand(QString("memory apply %1").arg(memoryIndex));
    QTimer::singleShot(750, this, [this, sliceId]() {
        if (m_pendingMemoryRevealSliceId != sliceId)
            return;
        m_pendingMemoryRevealSliceId = -1;
        if (auto* pendingSlice = m_radioModel.slice(sliceId)) {
            const TuneCenteringResult result =
                revealFrequencyIfNeeded(pendingSlice, pendingSlice->frequency(),
                                        TuneIntent::CommandedTargetCenter,
                                        "memory-apply-timeout");
            logTunePolicyDecision("memory-apply-timeout", TuneIntent::CommandedTargetCenter,
                                  pendingSlice->frequency(), pendingSlice->frequency(),
                                  result);
        }
    });

    // The radio should push the rest of the applied memory state, but keep the
    // local tuning step in sync immediately so wheel/click snap follows along.
    if (it->step > 0)
        m_radioModel.sendCommand(QString("slice set %1 step=%2")
            .arg(sliceId).arg(it->step));
    return true;
}

void MainWindow::onSliceAdded(SliceModel* s)
{
    // During layout transition, spectrums are being destroyed/recreated — skip
    if (m_applyingLayout) return;

    qDebug() << "MainWindow: slice added" << s->sliceId();
    const bool firstSlice = (m_activeSliceId < 0);

    // First slice — wire everything up
    if (firstSlice) {
        setActiveSlice(s->sliceId());

        // Detect initial band from radio's frequency
        if (m_bandSettings.currentBand().isEmpty())
            m_bandSettings.setCurrentBand(BandSettings::bandForFrequency(s->frequency()));

        // Re-create audio stream if it was invalidated by a profile load.
        // Only create if PC Audio is enabled — if the user is listening
        // through the radio's hardware outputs, don't switch to PC. (#336)
        if (m_needAudioStream) {
            m_needAudioStream = false;
            if (AppSettings::instance().value("PcAudioEnabled", "True").toString() == "True")
                m_radioModel.createRxAudioStream();
        }

        // Restore client-side DSP (NR2/RN2) from last session.
        // Deferred so the VFO widget exists for button sync.
        QTimer::singleShot(500, this, [this]() {
            auto& settings = AppSettings::instance();
            if (settings.value("ClientNr2Enabled", "False").toString() == "True")
                enableNr2WithWisdom();
            else if (settings.value("ClientRn2Enabled", "False").toString() == "True")
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setRn2Enabled(true); });
            else if (settings.value("ClientNr4Enabled", "False").toString() == "True")
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr4Enabled(true); });
            else if (settings.value("ClientDfnrEnabled", "False").toString() == "True")
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setDfnrEnabled(true); });
            else if (settings.value("ClientMnrEnabled", "False").toString() == "True")
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setMnrEnabled(true); });
            // BNR not auto-restored — requires manual enable each session

            // Restore DEXP (downward expander) — radio does not persist across sessions
            bool dexpSaved = settings.value("DexpEnabled", "False").toString() == "True";
            int dexpLevel = settings.value("DexpLevel", "0").toInt();
            if (dexpSaved) {
                m_radioModel.transmitModel().setDexp(true);
                if (dexpLevel > 0) {
                    m_radioModel.transmitModel().setDexpLevel(dexpLevel);
                }
            }

            // Deferred CW decoder restart after profile load (#305).
            // Mode status arrives asynchronously — by the time setActiveSlice
            // runs, the slice may still have its default mode (not CW).
            // Re-check after status has settled.
            auto* sl = activeSlice();
            if (sl) {
                bool isCw = (sl->mode() == "CW" || sl->mode() == "CWL");
                bool decodeOn = settings.value("CwDecodeOverlay", "True").toString() == "True";
                if (m_cwDecoderApplet) m_cwDecoderApplet->setCwPanelVisible(isCw && decodeOn);
                if (isCw && !m_cwDecoder.isRunning())
                    m_cwDecoder.start();
            }
        });
    }

    // Restore per-slice DAX channel from last session (#1221).
    // Deferred so the radio's initial slice status has arrived first.
    {
        const int sliceIdx = m_radioModel.slices().indexOf(s);
        if (sliceIdx >= 0) {
            const QString key = QString("DaxChannel_Slice%1").arg(QChar('A' + sliceIdx));
            int savedDax = AppSettings::instance().value(key, "0").toInt();
            if (savedDax > 0) {
                QTimer::singleShot(300, this, [s, savedDax]() {
                    if (s) { s->setDaxChannel(savedDax); }
                });
            }
        }
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
        int txSliceId = -1;
        for (auto* sl : m_radioModel.slices()) {
            if (sl->isTxSlice()) {
                txSliceId = sl->sliceId();
                const QString& m = sl->mode();
                isDigital = (m == "DIGU" || m == "DIGL" || m == "RTTY"
                          || m == "DFM"  || m == "NFM");
                break;
            }
        }
        m_audio->setDaxTxMode(isDigital);

        // Auto-toggle radio-side DAX flag on mode change (#534).
        // Digital modes need dax=1 for TX audio routing through DAX.
        m_radioModel.transmitModel().setDax(isDigital);

#ifdef HAVE_RADE
        // RADE mode should only route mic→RADEEngine when the TX slice IS
        // the RADE slice.  Otherwise a RADE slice running on a non-TX slice
        // would hijack voice TX on the actual TX slice.
        if (m_radeSliceId >= 0 && m_radeEngine && m_radeEngine->isActive())
            m_audio->setRadeMode(txSliceId == m_radeSliceId);
#endif
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
    if (s->isTxSlice()) {
        if (auto* sw = spectrumForSlice(s))
            sw->setHasTxSlice(true);
    }

    // Sync show-TX-in-waterfall on first slice
    if (auto* sw = spectrumForSlice(s))
        sw->setShowTxInWaterfall(
            m_radioModel.transmitModel().showTxInWaterfall());

    // Connect slice state changes → spectrum overlay updates
    connect(s, &SliceModel::frequencyChanged, this, [this, s](double mhz) {
        // Don't snap overlay back to stale radio-confirmed freq during active
        // encoder tuning — the optimistic VFO position is already ahead (#1524)
        bool activeTuning = false;
#ifdef HAVE_SERIALPORT
        activeTuning = activeTuning || m_flexCoalesceTimer.isActive();
#endif
#ifdef HAVE_HIDAPI
        activeTuning = activeTuning || m_hidCoalesceTimer.isActive();
#endif
        const bool memoryRevealPending = (m_pendingMemoryRevealSliceId == s->sliceId());
        if (activeTuning && s->sliceId() == m_activeSliceId && !memoryRevealPending)
            return;

        m_updatingFromModel = true;
        if (auto* sw = spectrumForSlice(s))
            sw->setSliceOverlay(s->sliceId(), mhz,
                s->filterLow(), s->filterHigh(), s->isTxSlice(),
                s->sliceId() == m_activeSliceId,
                s->mode(), s->rttyMark(), s->rttyShift(),
                s->ritOn(), s->ritFreq(), s->xitOn(), s->xitFreq());
        m_updatingFromModel = false;

        if (memoryRevealPending) {
            m_pendingMemoryRevealSliceId = -1;
            const TuneCenteringResult result =
                revealFrequencyIfNeeded(s, mhz, TuneIntent::CommandedTargetCenter, "memory-apply");
            logTunePolicyDecision("memory-apply", TuneIntent::CommandedTargetCenter,
                                  mhz, mhz, result);
        }

        // Feed frequency to Antenna Genius for band→antenna recall
        if (s->sliceId() == m_activeSliceId)
            m_antennaGenius.setRadioFrequency(mhz);
    });

    // Feed current frequency immediately (AG may connect later and reprocess).
    if (s->sliceId() == m_activeSliceId && s->frequency() > 0.0)
        m_antennaGenius.setRadioFrequency(s->frequency());

    connect(s, &SliceModel::filterChanged, this, [this, s](int lo, int hi) {
        auto* sw = spectrumForSlice(s);
        if (!sw) return;
        // Skip overlay update while user is dragging a filter edge — the radio's
        // status echo would overwrite the drag position, causing snap-to-zero (#764)
        if (sw->isDraggingFilter()) return;
        sw->setSliceOverlay(s->sliceId(), s->frequency(),
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
        if (auto* sw = spectrumForSlice(s))
            sw->setSliceOverlay(s->sliceId(), s->frequency(),
                s->filterLow(), s->filterHigh(), tx,
                s->sliceId() == m_activeSliceId,
                s->mode(), s->rttyMark(), s->rttyShift(),
                s->ritOn(), s->ritFreq(), s->xitOn(), s->xitFreq());
        updateSplitState();

        // Active follows TX slice (#1351) — switch the displayed/active slice
        // when an external program (e.g. WSJT-X) moves the TX flag
        if (tx && s->sliceId() != m_activeSliceId
            && AppSettings::instance().value("ActiveFollowsTxSlice", "False").toString() == "True") {
            setActiveSlice(s->sliceId());
        }
    });

    // When the radio notifies us that this slice became active, switch to it
    connect(s, &SliceModel::activeChanged, this, [this, s](bool active) {
        if (!active) return;
        // Accept radio's active echo — update client state but use
        // m_updatingFromModel to prevent sending active=1 back (feedback loop).
        m_updatingFromModel = true;
        setActiveSlice(s->sliceId());
        m_updatingFromModel = false;
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
            if (m_cwDecoderApplet) m_cwDecoderApplet->setCwPanelVisible(isCw && decodeOn);
            if (isCw && !m_cwDecoder.isRunning())
                m_cwDecoder.start();
            else if (!isCw && m_cwDecoder.isRunning())
                m_cwDecoder.stop();

            // Update CWX/DVK indicator availability for new mode
            updateKeyerAvailability(mode);

            // Disable client-side DSP in digital and CW modes — NR2/RN2/BNR
            // corrupt digital data (#534) and suppress CW tones (#784)
            bool disableDsp = (mode == "DIGU" || mode == "DIGL" || mode == "RTTY"
                            || mode == "CW"   || mode == "CWL");
            if (disableDsp) {
                if (m_audio->nr2Enabled())
                    QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr2Enabled(false); });
                if (m_audio->rn2Enabled())
                    QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setRn2Enabled(false); });
#ifdef HAVE_BNR
                if (m_audio->bnrEnabled())
                    QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setBnrEnabled(false); });
#endif
                if (m_audio->nr4Enabled())
                    QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr4Enabled(false); });
                if (m_audio->dfnrEnabled())
                    QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setDfnrEnabled(false); });
            }
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
        if (!sw) return;
        auto* vfo = sw->addVfoWidget(s->sliceId());
        wireVfoWidget(vfo, s);
        pushSliceOverlay(s);
    });

    // Create a VfoWidget for this slice on the correct panadapter
    auto* swForVfo = spectrumForSlice(s);
    if (!swForVfo) return;
    auto* vfo = swForVfo->addVfoWidget(s->sliceId());

    // Set SmartSDR+ flag before wireVfoWidget so rebuildFilterButtons
    // sees the correct value when setSlice() triggers the first build (#1356)
    {
        const QString& sub = m_radioModel.licenseSubscription();
        bool hasPlus = sub.contains("SmartSDR+");
        vfo->setSmartSdrPlus(hasPlus);
    }

    wireVfoWidget(vfo, s);

    // NR2/RN2/RADE are now wired permanently in wireVfoWidget — no
    // special handling needed here for active slice timing.

    // Show DIV button on dual-SCU radios
    {
        const QString& model = m_radioModel.model();
        bool divAllowed = model.contains("6500") || model.contains("6600")
                       || model.contains("6700") || model.contains("8600")
                       || model.contains("AU-520");
        vfo->setDiversityAllowed(divAllowed);
    }

    // Feed S-meter per-slice — only this VFO's slice level
    const int sid = s->sliceId();
    connect(&m_radioModel.meterModel(), &MeterModel::sLevelChanged,
            vfo, [vfo, sid](int sliceIndex, float dbm) {
        if (sliceIndex == sid)
            vfo->setSignalLevel(dbm);
    });
    // Feed ESC meter per-slice — signal strength after ESC processing
    connect(&m_radioModel.meterModel(), &MeterModel::escLevelChanged,
            vfo, [vfo, sid](int sliceIndex, float dbm) {
        if (sliceIndex == sid)
            vfo->setEscLevel(dbm);
    });
    connect(&m_radioModel, &RadioModel::antListChanged,
            vfo, &VfoWidget::setAntennaList);

    // Reset band-stack auto-save dwell timer on every active-slice tune
    connect(s, &SliceModel::frequencyChanged, this, [this, s]() {
        if (s->sliceId() != m_activeSliceId) return;
        if (!m_bsAutoSaveTimer) return;
        const int dwellSec = BandStackSettings::instance().autoSaveDwellSeconds();
        if (dwellSec <= 0) return;
        m_bsAutoSaveTimer->start(dwellSec * 1000);
    });

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
        // TX slice frequency is already set by the slice create command
        // (with mode-dependent offset), so do NOT override it here (#789).
        if (auto* sw = spectrum()) sw->setSplitPair(m_splitRxSliceId, m_splitTxSliceId);
        updateSplitState();
        // Auto-focus the TX VFO so the user can immediately tune the TX offset
        setActiveSlice(s->sliceId());
    }

    // Refresh slice tab buttons (#1278)
    m_appletPanel->updateSliceButtons(m_radioModel.slices(), m_activeSliceId);
}

void MainWindow::onSliceRemoved(int id)
{
    if (m_applyingLayout) return;

    qDebug() << "MainWindow: slice removed" << id;

#ifdef HAVE_RADE
    // If the RADE slice was closed, deactivate RADE
    if (id == m_radeSliceId)
        deactivateRADE();
#endif

    // If the split TX slice was closed, disable split
    if (m_splitActive && id == m_splitTxSliceId) {
        m_splitActive = false;
        m_splitRxSliceId = -1;
        m_splitTxSliceId = -1;
        if (auto* sw = spectrum()) sw->setSplitPair(-1, -1);
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

    // Update pan title bars — show the first remaining slice on each pan,
    // or clear the title if the pan has no slices left.
    if (m_panStack) {
        for (auto* applet : m_panStack->allApplets()) {
            bool found = false;
            for (auto* sl : m_radioModel.slices()) {
                if (sl->panId() == applet->panId()) {
                    applet->setSliceId(sl->sliceId());
                    found = true;
                    break;
                }
            }
            if (!found)
                applet->clearSliceTitle();
        }
    }

    // Reset panadapter state so display settings re-sync after profile load
    m_radioModel.resetPanState();
    m_needAudioStream = true;

    // If the removed slice was active, switch to the first remaining slice
    if (id == m_activeSliceId) {
        m_appletPanel->setSlice(nullptr);
        if (auto* sw = spectrum()) sw->overlayMenu()->setSlice(nullptr);

        const auto& slices = m_radioModel.slices();
        if (!slices.isEmpty())
            setActiveSlice(slices.first()->sliceId());
        else
            m_activeSliceId = -1;
    }

    // Refresh slice tab buttons (#1278)
    m_appletPanel->updateSliceButtons(m_radioModel.slices(), m_activeSliceId);
}

SliceModel* MainWindow::activeSlice() const
{
    if (m_activeSliceId < 0) return nullptr;
    return m_radioModel.slice(m_activeSliceId);
}

const char* MainWindow::tuneIntentName(TuneIntent intent)
{
    switch (intent) {
    case TuneIntent::IncrementalTune: return "IncrementalTune";
    case TuneIntent::AbsoluteJump:    return "AbsoluteJump";
    case TuneIntent::CommandedTargetCenter: return "CommandedTargetCenter";
    case TuneIntent::ExplicitPan:     return "ExplicitPan";
    case TuneIntent::RevealOffscreen: return "RevealOffscreen";
    }
    return "Unknown";
}

bool MainWindow::panFollowEnabled() const
{
    return AppSettings::instance().value("PanFollowVfo", "True").toString() == "True";
}

void MainWindow::logTunePolicyDecision(const char* source, TuneIntent intent,
                                       double oldFreqMhz, double newFreqMhz,
                                       const TuneCenteringResult& result) const
{
    qDebug().nospace()
        << "TunePolicy:"
        << " source=" << source
        << " intent=" << tuneIntentName(intent)
        << " oldFreq=" << oldFreqMhz
        << " newFreq=" << newFreqMhz
        << " oldCenter=" << result.oldCenterMhz
        << " newCenter=" << result.newCenterMhz
        << " bandwidth=" << result.bandwidthMhz
        << " followRevealTriggered=" << result.followRevealTriggered
        << " hardCenterUsed=" << result.hardCenterUsed
        << " animationMs=" << result.animationDurationMs;
}

void MainWindow::mirrorDiversityChildFrequency(SliceModel* slice, double mhz)
{
    if (!slice || !slice->isDiversityParent())
        return;

    long long hz = static_cast<long long>(std::round(mhz * 1e6));
    const QString freqStr = QString("%1.%2.%3")
        .arg(static_cast<int>(hz / 1000000))
        .arg(static_cast<int>((hz / 1000) % 1000), 3, 10, QChar('0'))
        .arg(static_cast<int>(hz % 1000), 3, 10, QChar('0'));

    for (auto* other : m_radioModel.slices()) {
        if (!other->isDiversityChild() || other->sliceId() == slice->sliceId())
            continue;
        auto* sw = spectrumForSlice(other);
        if (!sw)
            continue;
        if (auto* vfo = sw->vfoWidget(other->sliceId()))
            vfo->freqLabel()->setText(freqStr);
        sw->setSliceOverlay(other->sliceId(), mhz,
            other->filterLow(), other->filterHigh(),
            other->isTxSlice(), false,
            other->mode(), other->rttyMark(), other->rttyShift(),
            other->ritOn(), other->ritFreq(),
            other->xitOn(), other->xitFreq());
    }
}

void MainWindow::applyTuneRequest(SliceModel* slice, double mhz,
                                  TuneIntent intent, const char* source)
{
    if (!slice)
        return;

    const double oldFreqMhz = slice->frequency();
    auto* sw = spectrumForSlice(slice);
    if (slice->isLocked()) {
        if (slice->sliceId() == m_activeSliceId && sw) {
            m_updatingFromModel = true;
            sw->setVfoFrequency(oldFreqMhz);
            m_updatingFromModel = false;
        }
        return;
    }

    if (slice->sliceId() == m_activeSliceId && sw)
        sw->setVfoFrequency(mhz);

    slice->setFrequency(mhz);
    mirrorDiversityChildFrequency(slice, mhz);

    const TuneCenteringResult result =
        (intent == TuneIntent::IncrementalTune)
            ? panFollowVfo(slice, mhz, source)
            : revealFrequencyIfNeeded(slice, mhz, intent, source);
    logTunePolicyDecision(source, intent, oldFreqMhz, mhz, result);
}

void MainWindow::applyPanRangeRequest(const QString& panId, double centerMhz,
                                      double bandwidthMhz, const char* source)
{
    if (panId.isEmpty() || bandwidthMhz <= 0.0)
        return;

    auto* pan = m_radioModel.panadapter(panId);
    const QString centerStr = QString::number(centerMhz, 'f', 6);
    const QString bandwidthStr = QString::number(bandwidthMhz, 'f', 6);

    if (pan) {
        if (qFuzzyCompare(pan->centerMhz(), centerMhz)
            && qFuzzyCompare(pan->bandwidthMhz(), bandwidthMhz)) {
            return;
        }
        // Update both values together before the radio echo arrives. Explicit
        // zoom workflows are especially sensitive to center/bandwidth skew;
        // splitting them produced the P1/P2 waterfall-loss and zoom-drift bugs.
        pan->applyPanStatus({
            {"center", centerStr},
            {"bandwidth", bandwidthStr},
        });
    }

    m_radioModel.sendCommand(
        QString("display pan set %1 center=%2 bandwidth=%3")
            .arg(panId, centerStr, bandwidthStr));

    qDebug() << "Pan range request:" << source
             << "center" << centerMhz
             << "bandwidth" << bandwidthMhz;
}

void MainWindow::setActiveSlice(int sliceId)
{
    setActiveSliceInternal(sliceId, true);
}

void MainWindow::queueActiveSliceForSpectrumTarget(int sliceId)
{
    if (sliceId < 0 || sliceId == m_activeSliceId) {
        m_pendingSpectrumTargetSliceId = -1;
        return;
    }

    m_pendingSpectrumTargetSliceId = sliceId;
    QTimer::singleShot(0, this, [this, sliceId]() {
        if (m_pendingSpectrumTargetSliceId != sliceId)
            return;
        m_pendingSpectrumTargetSliceId = -1;
        if (auto* s = m_radioModel.slice(sliceId))
            setActiveSliceInternal(s->sliceId(), false);
    });
}

void MainWindow::setActiveSliceInternal(int sliceId, bool revealOffscreen)
{
    auto* s = m_radioModel.slice(sliceId);
    if (!s) return;

    // Auto-activate the panadapter that owns this slice
    if (m_panStack && !s->panId().isEmpty())
        m_panStack->setActivePan(s->panId());

    const int prevId = m_activeSliceId;
    m_activeSliceId = sliceId;

    // Send "slice set N active=1" only when switching to a different slice
    // (matches SmartSDR pcap — sent on VFO flag click, not on every tune).
    // Guard: don't send if triggered by the radio's own activeChanged echo
    // (m_updatingFromModel is set in the activeChanged handler).
    if (sliceId != prevId && !m_updatingFromModel)
        s->setActive(true);

    // Active slice changed → restart dwell window for the new active slice
    if (sliceId != prevId && m_bsAutoSaveTimer) {
        const int dwellSec = BandStackSettings::instance().autoSaveDwellSeconds();
        if (dwellSec > 0)
            m_bsAutoSaveTimer->start(dwellSec * 1000);
        else
            m_bsAutoSaveTimer->stop();
    }

    // Update all overlay isActive flags on each slice's correct spectrum
    for (auto* sl : m_radioModel.slices()) {
        const bool isActive = (sl->sliceId() == sliceId);
        if (auto* sw = spectrumForSlice(sl))
            sw->setSliceOverlay(sl->sliceId(), sl->frequency(),
                sl->filterLow(), sl->filterHigh(), sl->isTxSlice(), isActive,
                sl->mode(), sl->rttyMark(), sl->rttyShift(),
                sl->ritOn(), sl->ritFreq(), sl->xitOn(), sl->xitFreq());
    }

    // QSO recorder: track active slice for frequency/mode metadata (#1297)
    m_qsoRecorder->setSlice(s);

    // Re-wire applet panel, overlay menu to the new active slice
    if (m_panStack) {
        if (auto* applet = m_panStack->panadapter(s->panId()))
            applet->setSliceId(sliceId);
        else if (m_panStack->activeApplet())
            m_panStack->activeApplet()->setSliceId(sliceId);
    }
    m_appletPanel->setSlice(s);
    m_appletPanel->updateSliceButtons(m_radioModel.slices(), sliceId);
    auto* sw = spectrum();
    if (sw) {
        if (revealOffscreen) {
            const TuneCenteringResult result =
                revealFrequencyIfNeeded(s, s->frequency(),
                                        TuneIntent::RevealOffscreen,
                                        "setActiveSlice");
            logTunePolicyDecision("setActiveSlice", TuneIntent::RevealOffscreen,
                                  s->frequency(), s->frequency(), result);
        }

        sw->overlayMenu()->setSlice(s);

        // Sync step size from the new active slice
        if (s->stepHz() > 0) {
            sw->setStepSize(s->stepHz());
            m_appletPanel->rxApplet()->syncStepFromSlice(s->stepHz(), s->stepList());
        }

        // Switch active VFO widget display (NR2/RN2/RADE are wired permanently
        // in wireVfoWidget, no disconnect/reconnect needed)
        sw->setActiveVfoWidget(sliceId);
    } else if (s->stepHz() > 0) {
        m_appletPanel->rxApplet()->syncStepFromSlice(s->stepHz(), s->stepList());
    }

    // Update filter limits for the active slice's mode
    updateFilterLimitsForMode(s->mode());

    // Route CW decoder output to the pan owning this slice (#864)
    routeCwDecoderOutput();

    // Show/hide CW decode panel for the active slice's current mode
    bool isCw = (s->mode() == "CW" || s->mode() == "CWL");
    bool decodeOn = AppSettings::instance().value("CwDecodeOverlay", "True").toString() == "True";
    if (m_cwDecoderApplet) m_cwDecoderApplet->setCwPanelVisible(isCw && decodeOn);
    if (isCw && !m_cwDecoder.isRunning())
        m_cwDecoder.start();
    else if (!isCw && m_cwDecoder.isRunning())
        m_cwDecoder.stop();

    // Update CWX/DVK indicator availability for this slice's mode
    updateKeyerAvailability(s->mode());

    // Detect band from frequency
    if (m_bandSettings.currentBand().isEmpty())
        m_bandSettings.setCurrentBand(BandSettings::bandForFrequency(s->frequency()));


    // NOTE: RADE audio mode is now driven by the TX slice (in updateDaxTxMode),
    // not the active/selected slice. Switching which slice the user is looking at
    // should not change the TX audio routing.

    updateSplitState();

    // TX follows active slice (#441) — auto-assign TX when switching slices
    if (!m_splitActive && sliceId != prevId && !s->isTxSlice()
        && AppSettings::instance().value("TxFollowsActiveSlice", "False").toString() == "True") {
        s->setTxSlice(true);
    }

    // Update MEM button target-slice badge on every overlay (#1781)
    refreshMemoryBrowsePanel();

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
    if (auto* s = spectrum()) {
        s->setFilterLimits(minHz, maxHz);
        s->setMode(mode);
    }
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
    if (auto* sw = spectrum()) sw->setSplitPair(-1, -1);

    updateSplitState();
}

void MainWindow::updateSplitState()
{
    auto* sw = spectrum();
    if (!sw) return;
    for (auto* s : m_radioModel.slices()) {
        if (auto* w = sw->vfoWidget(s->sliceId())) {
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

void MainWindow::setActivePanApplet(PanadapterApplet* applet)
{
    if (applet == m_panApplet) return;
    m_panApplet = applet;

    // Re-route CW decoder output: the active slice may now belong to this pan
    routeCwDecoderOutput();
}

// Route CW decoder text/stats output to the pan that owns the active slice,
// so decoded text appears in the correct pan's CW widget (#864).
void MainWindow::routeCwDecoderOutput()
{
    // Determine which applet should receive CW decoder output:
    // the pan that owns the active audio slice (whose audio feeds the decoder).
    PanadapterApplet* target = nullptr;
    if (auto* s = activeSlice(); s && m_panStack && !s->panId().isEmpty())
        target = m_panStack->panadapter(s->panId());
    if (!target)
        target = m_panApplet;  // fallback to active pan

    if (target == m_cwDecoderApplet) return;

    // Disconnect from old applet
    if (m_cwDecoderApplet) {
        disconnect(&m_cwDecoder, &CwDecoder::textDecoded,
                   m_cwDecoderApplet, &PanadapterApplet::appendCwText);
        disconnect(&m_cwDecoder, &CwDecoder::statsUpdated,
                   m_cwDecoderApplet, &PanadapterApplet::setCwStats);
        if (auto* pb = m_cwDecoderApplet->lockPitchButton())
            disconnect(pb, &QPushButton::toggled,
                       &m_cwDecoder, &CwDecoder::lockPitch);
        if (auto* sb = m_cwDecoderApplet->lockSpeedButton())
            disconnect(sb, &QPushButton::toggled,
                       &m_cwDecoder, &CwDecoder::lockSpeed);
        disconnect(m_cwDecoderApplet, &PanadapterApplet::pitchRangeChanged,
                   &m_cwDecoder, &CwDecoder::setPitchRange);
        disconnect(m_cwDecoderApplet, &PanadapterApplet::cwPanelCloseRequested,
                   &m_cwDecoder, &CwDecoder::stop);
    }

    m_cwDecoderApplet = target;

    // Connect to new applet
    if (m_cwDecoderApplet) {
        connect(&m_cwDecoder, &CwDecoder::textDecoded,
                m_cwDecoderApplet, &PanadapterApplet::appendCwText);
        connect(&m_cwDecoder, &CwDecoder::statsUpdated,
                m_cwDecoderApplet, &PanadapterApplet::setCwStats);
        connect(m_cwDecoderApplet->lockPitchButton(), &QPushButton::toggled,
                &m_cwDecoder, &CwDecoder::lockPitch);
        connect(m_cwDecoderApplet->lockSpeedButton(), &QPushButton::toggled,
                &m_cwDecoder, &CwDecoder::lockSpeed);
        connect(m_cwDecoderApplet, &PanadapterApplet::pitchRangeChanged,
                &m_cwDecoder, &CwDecoder::setPitchRange);
        connect(m_cwDecoderApplet, &PanadapterApplet::cwPanelCloseRequested,
                &m_cwDecoder, &CwDecoder::stop);
    }
}

void MainWindow::wirePanadapter(PanadapterApplet* applet)
{
    auto* sw = applet->spectrumWidget();
    auto* menu = sw->overlayMenu();

    // Guard: wirePanadapter() is called once at startup (for m_panApplet) and
    // again from panadapterAdded for the same widget.  Without these disconnects
    // every sw/menu/applet → this signal would be connected twice, causing each
    // user gesture to fire two identical radio commands (e.g. duplicate
    // "display pan set … min_dbm= max_dbm=").  Clearing prior connections here
    // replaces the scattered per-signal disconnect guards below.
    sw->disconnect(this);
    menu->disconnect(this);
    applet->disconnect(this);

    // Wire band plan manager to this spectrum widget
    sw->setBandPlanManager(m_bandPlanMgr);

    // Set panadapter bandwidth zoom limits based on radio model
    sw->setBandwidthLimits(m_radioModel.minPanBandwidthMhz(),
                           m_radioModel.maxPanBandwidthMhz());

    // Set panId on the overlay menu so +RX routes to the correct pan
    menu->setPanId(applet->panId());
    menu->setMemories(m_radioModel.memories());

    // Antenna list → this overlay menu (per-pan, mirrors VfoWidget pattern) (#1260)
    connect(&m_radioModel, &RadioModel::antListChanged,
            menu, &SpectrumOverlayMenu::setAntennaList);
    menu->setAntennaList(m_radioModel.antennaList());

    // Apply current prop forecast state to this (possibly new) panadapter
    if (m_propForecast) {
        sw->setPropForecastVisible(m_propForecast->isEnabled());
        if (m_propForecast->isEnabled()) {
            PropForecast fc = m_propForecast->lastForecast();
            sw->setPropForecast(fc.kIndex, fc.aIndex, fc.sfi);
        }
    }

    // Click on K/A/SFI overlay → open prop dashboard
    connect(sw, &SpectrumWidget::propForecastClicked,
            this, &MainWindow::showPropDashboard);

    if (auto* pan = m_radioModel.panadapter(applet->panId())) {
        connect(pan, &PanadapterModel::wideChanged,
                sw, &SpectrumWidget::setWideActive);
        sw->setWideActive(pan->wideActive());

        // Route confirmed level changes (min_dbm / max_dbm) from the radio to
        // this pan's spectrum widget.  Using the per-pan PanadapterModel signal
        // (guarded by change detection) rather than the RadioModel-level
        // panadapterLevelChanged signal ensures that:
        //   a) stale echo-backs (same value) don't overwrite the user's in-flight
        //      local change while waiting for the radio to confirm the command, and
        //   b) in multi-pan setups, a level update on pan B doesn't incorrectly
        //      update pan A's dBm scale.
        connect(pan, &PanadapterModel::levelChanged,
                sw, &SpectrumWidget::setDbmRange);
    }

    // ── Debounced resize → re-push xpixels/ypixels to the radio (#1511) ───
    // When the layout changes (e.g. adding a second pan, splitting, resizing),
    // the widget dimensions change but the radio keeps encoding FFT bins to the
    // old yPixels scale.  This causes a ~5 dB noise-floor offset until restart.
    // A 300ms debounce avoids flooding the radio during animated resizes.
    {
        auto* resizeTimer = new QTimer(sw);  // parented to sw → auto-deleted
        resizeTimer->setSingleShot(true);
        resizeTimer->setInterval(300);
        connect(sw, &SpectrumWidget::dimensionsChanged,
                resizeTimer, [resizeTimer](int, int) { resizeTimer->start(); });
        connect(resizeTimer, &QTimer::timeout,
                this, [this, applet, sw]() {
            auto* pan = m_radioModel.panadapter(applet->panId());
            if (!pan || !sw) return;
            int xpix = sw->width();
            int ypix = sw->height();
            if (xpix < 100 || ypix < 100) return;
            m_radioModel.sendCommand(
                QString("display pan set %1 xpixels=%2 ypixels=%3")
                    .arg(pan->panId()).arg(xpix).arg(ypix));
            if (pan->panStreamId())
                m_radioModel.panStream()->setYPixels(pan->panStreamId(), ypix);
        });
    }

    // ── Tuning step size → this pan's spectrum widget ─────────────────────
    // The global connection only covers AppSettings + radio command.
    // Each pan must also receive stepSizeChanged so scroll-to-tune
    // uses the correct step regardless of which pan is active.
    connect(m_appletPanel->rxApplet(), &RxApplet::stepSizeChanged,
            sw, &SpectrumWidget::setStepSize);

    // ── Pan activation: clicking on this pan makes it active ─────────────
    connect(applet, &PanadapterApplet::activated,
            m_panStack, &PanadapterStack::setActivePan);

    // ── Close pan: X button on title bar closes this pan ────────────────
    connect(applet, &PanadapterApplet::closeRequested,
            this, [this](const QString& panId) {
        // Don't close the last pan
        if (m_panStack->count() <= 1) return;
        m_radioModel.sendCommand(QString("display pan remove %1").arg(panId));
    });

    // ── User drag actions from spectrum → radio (per-pan) ──────────────────
    connect(sw, &SpectrumWidget::frequencyRangeChangeRequested,
            this, [this, applet](double center, double bandwidth) {
        applyPanRangeRequest(applet->panId(), center, bandwidth, "explicit-pan-range");
    });
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
    connect(sw, &SpectrumWidget::bandZoomRequested,
            this, [this, applet, bandZoomOn = std::make_shared<bool>(false)]() mutable {
        *bandZoomOn = !*bandZoomOn;
        m_radioModel.sendCommand(
            QString("display pan set %1 band_zoom=%2")
                .arg(applet->panId()).arg(*bandZoomOn ? 1 : 0));
    });
    connect(sw, &SpectrumWidget::segmentZoomRequested,
            this, [this, applet, segZoomOn = std::make_shared<bool>(false)]() mutable {
        *segZoomOn = !*segZoomOn;
        m_radioModel.sendCommand(
            QString("display pan set %1 segment_zoom=%2")
                .arg(applet->panId()).arg(*segZoomOn ? 1 : 0));
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
    // QPointer guards against dangling sw when a pan is removed (#381)
    QPointer<SpectrumWidget> swGuard(sw);
    auto* tnf = &m_radioModel.tnfModel();
    auto rebuildTnfMarkers = [this, swGuard]() {
        if (!swGuard) return;
        auto* t = &m_radioModel.tnfModel();
        QVector<SpectrumWidget::TnfMarker> markers;
        for (const auto& e : t->tnfs())
            markers.append({e.id, e.freqMhz, e.widthHz, e.depthDb, e.permanent});
        swGuard->setTnfMarkers(markers);
    };
    connect(tnf, &TnfModel::tnfChanged,  this, rebuildTnfMarkers);
    connect(tnf, &TnfModel::tnfRemoved,  this, rebuildTnfMarkers);
    connect(tnf, &TnfModel::globalEnabledChanged,
            sw, &SpectrumWidget::setTnfGlobalEnabled);
    connect(tnf, &TnfModel::globalEnabledChanged,
            this, [this](bool on) {
        m_tnfIndicator->setStyleSheet(on
            ? "QLabel { color: #00e060; font-weight: bold; font-size: 24px; }"
            : "QLabel { color: #404858; font-weight: bold; font-size: 24px; }");
    });

    // FDX indicator style update
    connect(&m_radioModel, &RadioModel::infoChanged, this, [this]() {
        bool fdx = m_radioModel.fullDuplexEnabled();
        m_fdxIndicator->setStyleSheet(fdx
            ? "QLabel { color: #00e060; font-weight: bold; font-size: 24px; }"
            : "QLabel { color: #404858; font-weight: bold; font-size: 24px; }");
    });
    connect(sw, &SpectrumWidget::tnfCreateRequested,   tnf, &TnfModel::createTnf);
    connect(sw, &SpectrumWidget::tnfMoveRequested,     tnf, &TnfModel::setTnfFreq);
    connect(sw, &SpectrumWidget::tnfRemoveRequested,   tnf, &TnfModel::requestRemoveTnf);
    connect(sw, &SpectrumWidget::tnfWidthRequested,    tnf, &TnfModel::setTnfWidth);
    connect(sw, &SpectrumWidget::tnfDepthRequested,    tnf, &TnfModel::setTnfDepth);
    connect(sw, &SpectrumWidget::tnfPermanentRequested,tnf, &TnfModel::setTnfPermanent);

    // ── Spot markers ─────────────────────────────────────────────────────
    auto* spots = &m_radioModel.spotModel();
    auto rebuildSpots = [this, swGuard]() {
        if (!swGuard) return;  // widget destroyed (layout change)
        auto* s = &m_radioModel.spotModel();
        const bool showMemories =
            AppSettings::instance().value("IsMemorySpotsEnabled", "False").toString() == "True";
        QVector<SpectrumWidget::SpotMarker> markers;
        for (const auto& spot : s->spots()) {
            if (spot.source == "Memory" && !showMemories)
                continue;
            qint64 tsMs = 0;
            if (spot.source != "Memory") {
                tsMs = (spot.timestamp.isValid() && spot.timestamp.toMSecsSinceEpoch() > 0)
                    ? spot.timestamp.toMSecsSinceEpoch()
                    : spot.addedMs;
            }
            QColor dxccCol;
            if (m_dxccProvider.isEnabled() && spot.source != "Memory")
                dxccCol = m_dxccProvider.colorForSpot(spot.callsign, spot.rxFreqMhz, spot.mode);
            markers.append({spot.index, spot.callsign, spot.rxFreqMhz, spot.color, spot.mode,
                            dxccCol, spot.source, spot.spotterCallsign, spot.comment, tsMs});
        }
        swGuard->setSpotMarkers(markers);
    };
    connect(spots, &SpotModel::spotAdded,   this, rebuildSpots);
    connect(spots, &SpotModel::spotUpdated, this, rebuildSpots);
    connect(spots, &SpotModel::spotRemoved, this, rebuildSpots);
    connect(spots, &SpotModel::spotsCleared,this, rebuildSpots);
    connect(spots, &SpotModel::spotsRefreshed, this, rebuildSpots);
    {
        auto& s = AppSettings::instance();
        sw->setShowSpots(s.value("IsSpotsEnabled", "True").toString() == "True");
        sw->setSpotFontSize(s.value("SpotFontSize", "16").toInt());
        sw->setSpotMaxLevels(s.value("SpotsMaxLevel", "3").toInt());
        sw->setSpotStartPct(s.value("SpotsStartingHeightPercentage", "50").toInt());
        sw->setSpotOverrideColors(s.value("IsSpotsOverrideColorsEnabled", "False").toString() == "True");
        sw->setSpotOverrideBg(s.value("IsSpotsOverrideBackgroundColorsEnabled", "True").toString() == "True");
        sw->setSpotColor(QColor(s.value("SpotsOverrideColor", "#FFFF00").toString()));
        sw->setSpotBgColor(QColor(s.value("SpotsOverrideBgColor", "#000000").toString()));
        sw->setSpotBgOpacity(s.value("SpotsBackgroundOpacity", 48).toInt());
    }

    // ── Per-pan display controls (client-side) ───────────────────────────
    connect(menu, &SpectrumOverlayMenu::fftFillAlphaChanged,
            sw, &SpectrumWidget::setFftFillAlpha);
    connect(menu, &SpectrumOverlayMenu::fftFillColorChanged,
            sw, &SpectrumWidget::setFftFillColor);
    connect(menu, &SpectrumOverlayMenu::fftHeatMapChanged,
            sw, &SpectrumWidget::setFftHeatMap);
    connect(menu, &SpectrumOverlayMenu::showGridChanged,
            sw, &SpectrumWidget::setShowGrid);
    connect(menu, &SpectrumOverlayMenu::freqGridSpacingChanged,
            sw, &SpectrumWidget::setFreqGridSpacing);
    connect(menu, &SpectrumOverlayMenu::fftLineWidthChanged,
            sw, &SpectrumWidget::setFftLineWidth);
    connect(menu, &SpectrumOverlayMenu::noiseFloorPositionChanged,
            sw, &SpectrumWidget::setNoiseFloorPosition);
    connect(menu, &SpectrumOverlayMenu::noiseFloorEnableChanged,
            sw, &SpectrumWidget::setNoiseFloorEnable);

    // Pop out / dock panadapter
    connect(sw, &SpectrumWidget::popOutRequested, this, [this, applet](bool popOut) {
        if (popOut) {
            m_panStack->floatPanadapter(applet->panId());
        } else {
            m_panStack->dockPanadapter(applet->panId());
        }
    });
    connect(applet, &PanadapterApplet::popOutClicked, this, [this, applet]() {
        m_panStack->floatPanadapter(applet->panId());
    });

    // ── DAX IQ pan routing from overlay menu ───────────────────────────
    // The overlay controls which pan feeds which IQ channel (routing only).
    // Stream create/destroy and rate are managed by the DIGI applet.
    connect(menu, &SpectrumOverlayMenu::daxIqChannelChanged,
            this, [this, applet](int channel) {
        auto* pan = m_radioModel.panadapter(applet->panId());
        if (!pan) return;
        m_radioModel.sendCommand(
            QString("display pan set %1 daxiq_channel=%2")
                .arg(applet->panId()).arg(channel));
    });

    // Sync DAX IQ combo from radio status + restore saved assignment (#1221)
    {
        auto* pan = m_radioModel.panadapter(applet->panId());
        if (pan) {
            connect(pan, &PanadapterModel::daxiqChannelChanged,
                    menu, &SpectrumOverlayMenu::syncDaxIqChannel);
            menu->syncDaxIqChannel(pan->daxiqChannel());

            // DAX IQ channel restore deferred — the radio persists
            // daxiq_channel on the pan, so it arrives via status echo.
            // Overlay combo syncs automatically via daxiqChannelChanged.
        }
    }

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
    connect(menu, &SpectrumOverlayMenu::wfColorSchemeChanged,
            sw, &SpectrumWidget::setWfColorScheme);
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
    // NB Waterfall Blanker (#277)
    connect(menu, &SpectrumOverlayMenu::wfBlankerEnabledChanged,
            sw, &SpectrumWidget::setWfBlankerEnabled);
    connect(menu, &SpectrumOverlayMenu::wfBlankerThresholdChanged,
            sw, &SpectrumWidget::setWfBlankerThreshold);
    connect(menu, &SpectrumOverlayMenu::backgroundImageRequested,
            this, [this, sw] {
        QString path = QFileDialog::getOpenFileName(sw, "Choose Background Image",
            QString(), "Images (*.png *.jpg *.jpeg *.bmp)");
        if (path.isEmpty()) return;
        sw->setBackgroundImage(path);
        auto& s = AppSettings::instance();
        s.setValue(sw->settingsKey("BackgroundImage"), path);
        s.save();
    });
    connect(menu, &SpectrumOverlayMenu::backgroundImageCleared,
            this, [sw] {
        sw->setBackgroundImage(":/bg-default.jpg");
        auto& s = AppSettings::instance();
        s.setValue(sw->settingsKey("BackgroundImage"), ":/bg-default.jpg");
        s.save();
    });
    connect(menu, &SpectrumOverlayMenu::backgroundOpacityChanged,
            this, [sw](int pct) {
        sw->setBackgroundOpacity(pct);
        auto& s = AppSettings::instance();
        s.setValue(sw->settingsKey("BackgroundOpacity"), QString::number(pct));
        s.save();
    });
    connect(menu, &SpectrumOverlayMenu::displaySettingsReset,
            this, [this, applet, sw, menu] {
        // Apply all SpectrumWidget defaults
        sw->setFftAverage(0);
        sw->setFftFps(25);
        sw->setFftFillAlpha(0.70f);
        sw->setFftFillColor(QColor(0x00, 0xe5, 0xff));
        sw->setFftWeightedAvg(false);
        sw->setFftHeatMap(true);
        sw->setWfColorScheme(0);
        sw->setWfColorGain(50);
        sw->setWfBlackLevel(15);
        sw->setWfAutoBlack(true);
        sw->setWfLineDuration(100);
        sw->setWfBlankerEnabled(false);
        sw->setWfBlankerThreshold(1.15f);
        sw->setWfBlankerMode(0);
        sw->setShowCursorFreq(false);
        sw->setBackgroundImage(":/bg-default.jpg");
        sw->setBackgroundOpacity(80);
        sw->setNoiseFloorEnable(false);
        sw->setNoiseFloorPosition(75);
        sw->setFreqGridSpacing(0);  // Auto (#1390)

        // Radio commands for radio-authoritative display settings
        m_radioModel.sendCommand(
            QString("display pan set %1 average=0").arg(applet->panId()));
        m_radioModel.sendCommand(
            QString("display pan set %1 fps=25").arg(applet->panId()));
        m_radioModel.sendCommand(
            QString("display pan set %1 weighted_average=0").arg(applet->panId()));
        auto* pan = m_radioModel.panadapter(applet->panId());
        if (pan && !pan->waterfallId().isEmpty()) {
            m_radioModel.sendCommand(
                QString("display panafall set %1 color_gain=50").arg(pan->waterfallId()));
            m_radioModel.sendCommand(
                QString("display panafall set %1 black_level=15").arg(pan->waterfallId()));
            m_radioModel.sendCommand(
                QString("display panafall set %1 line_duration=100").arg(pan->waterfallId()));
        }

        // Persist all defaults to AppSettings
        auto& s = AppSettings::instance();
        s.setValue(sw->settingsKey("DisplayFftAverage"),          "0");
        s.setValue(sw->settingsKey("DisplayFftFps"),              "25");
        s.setValue(sw->settingsKey("DisplayFftFillAlpha"),        "0.70");
        s.setValue(sw->settingsKey("DisplayFftFillColor"),        "#00e5ff");
        s.setValue(sw->settingsKey("DisplayFftWeightedAvg"),      "False");
        s.setValue(sw->settingsKey("DisplayFftHeatMap"),          "True");
        s.setValue(sw->settingsKey("DisplayWfColorScheme"),       "0");
        s.setValue(sw->settingsKey("DisplayWfColorGain"),         "50");
        s.setValue(sw->settingsKey("DisplayWfBlackLevel"),        "15");
        s.setValue(sw->settingsKey("DisplayWfAutoBlack"),         "True");
        s.setValue(sw->settingsKey("DisplayWfLineDuration"),      "100");
        s.setValue(sw->settingsKey("WaterfallBlankingEnabled"),   "False");
        s.setValue(sw->settingsKey("WaterfallBlankingThreshold"), "1.15");
        s.setValue(sw->settingsKey("WaterfallBlankingMode"),      "0");
        s.setValue(sw->settingsKey("CursorFreqLabel"),            "False");
        s.setValue(sw->settingsKey("BackgroundImage"),            ":/bg-default.jpg");
        s.setValue(sw->settingsKey("BackgroundOpacity"),          "80");
        s.setValue(sw->settingsKey("DisplayFreqGridSpacing"),     "0");
        s.save();

        // Sync all Display panel UI controls
        menu->syncDisplaySettings(0, 25, 70, false, QColor(0x00, 0xe5, 0xff),
                                  50, 15, true, 100, 75, false, true, 0);
        menu->syncExtraDisplaySettings(false, 1.15f, 80, 0);
    });

    auto resolveSpectrumTuneTarget = [this, sw]() -> SliceModel* {
        QString panId;
        for (auto* panApplet : m_panStack->allApplets()) {
            if (panApplet->spectrumWidget() == sw) {
                panId = panApplet->panId();
                break;
            }
        }

        if (panId.isEmpty())
            return activeSlice();

        if (auto* current = activeSlice(); current && current->panId() == panId)
            return current;

        for (auto* candidate : m_radioModel.slices()) {
            if (candidate->panId() == panId)
                return candidate;
        }

        return activeSlice();
    };

    // ── Click-to-tune ────────────────────────────────────────────────────
    // Same-pan and cross-pan tune requests both resolve a target slice first,
    // then run through the shared absolute/incremental tuning policy.
    connect(sw, &SpectrumWidget::frequencyClicked,
            this, [this, resolveSpectrumTuneTarget](double mhz) {
        if (auto* target = resolveSpectrumTuneTarget()) {
            queueActiveSliceForSpectrumTarget(target->sliceId());
            applyTuneRequest(target, mhz, TuneIntent::AbsoluteJump, "spectrum-click");
        }
    });
    connect(sw, &SpectrumWidget::incrementalTuneRequested,
            this, [this, resolveSpectrumTuneTarget](double mhz) {
        if (auto* target = resolveSpectrumTuneTarget()) {
            queueActiveSliceForSpectrumTarget(target->sliceId());
            applyTuneRequest(target, mhz, TuneIntent::IncrementalTune, "spectrum-incremental");
        }
    });

    // ── Spot trigger — notify the radio when a spot label is clicked (#341)
    connect(sw, &SpectrumWidget::spotTriggered, this, [this, applet](int spotIndex) {
        const auto& spots = m_radioModel.spotModel().spots();
        auto it = spots.find(spotIndex);
        if (it == spots.end()) return;

        if (it->source == "Memory") {
            int memoryIndex = memoryIndexFromSpotId(spotIndex);
            if (memoryIndex >= 0)
                activateMemorySpot(memoryIndex, applet->panId());
            return;
        }

        if (!isPassiveLocalSpotId(spotIndex))
            m_radioModel.sendCommand(QString("spot trigger %1").arg(spotIndex));

        // Auto-switch mode from spot metadata (#424)
        if (AppSettings::instance().value("SpotAutoSwitchMode", "False").toString() != "True")
            return;
        auto* s = activeSlice();
        if (!s) return;

        // Extract mode: prefer explicit mode field, fall back to comment text
        QString spotMode = it->mode.toUpper().trimmed();
        if (spotMode.isEmpty() && !it->comment.isEmpty()) {
            // RBN: mode is first word ("CW  6 dB 28 WPM CQ")
            // POTA/Cluster: mode is often last word ("JP-1277 Higashimurayama CW")
            static const QSet<QString> knownModes = {
                "CW", "SSB", "USB", "LSB", "AM", "FM", "FT8", "FT4",
                "JS8", "RTTY", "PSK31", "PSK63", "PSK", "OLIVIA",
                "JT65", "JT9", "SAM", "NFM", "DIGU", "DIGL"
            };
            QStringList words = it->comment.split(' ', Qt::SkipEmptyParts);
            // Check first word (RBN format)
            if (!words.isEmpty() && knownModes.contains(words.first().toUpper()))
                spotMode = words.first().toUpper();
            // Check last word (POTA/Cluster format)
            else if (!words.isEmpty() && knownModes.contains(words.last().toUpper()))
                spotMode = words.last().toUpper();
        }
        // Fallback: infer mode from frequency using band plan
        if (spotMode.isEmpty()) {
            double f = it->rxFreqMhz;
            // CW sub-bands (bottom of each HF band)
            if ((f >= 1.800 && f < 1.850) || (f >= 3.500 && f < 3.600) ||
                (f >= 7.000 && f < 7.050) || (f >= 10.100 && f < 10.140) ||
                (f >= 14.000 && f < 14.070) || (f >= 18.068 && f < 18.095) ||
                (f >= 21.000 && f < 21.070) || (f >= 24.890 && f < 24.920) ||
                (f >= 28.000 && f < 28.070) || (f >= 50.000 && f < 50.100))
                spotMode = "CW";
            // Digital sub-bands
            else if ((f >= 1.840 && f < 1.850) || (f >= 3.570 && f < 3.600) ||
                     (f >= 7.040 && f < 7.050) || (f >= 10.130 && f < 10.150) ||
                     (f >= 14.070 && f < 14.100) || (f >= 18.095 && f < 18.110) ||
                     (f >= 21.070 && f < 21.100) || (f >= 24.915 && f < 24.930) ||
                     (f >= 28.070 && f < 28.150))
                spotMode = "DIGU";
            // Phone — default by convention
            else if (f >= 10.0)
                spotMode = "USB";
            else if (f >= 1.8)
                spotMode = "LSB";
        }
        if (spotMode.isEmpty()) return;

        // Map spot mode string → radio mode
        static const QMap<QString, QString> modeMap = {
            {"CW", "CW"}, {"CWL", "CW"}, {"CWU", "CW"},
            {"USB", "USB"}, {"LSB", "LSB"},
            {"FT8", "DIGU"}, {"FT4", "DIGU"}, {"JS8", "DIGU"},
            {"PSK31", "DIGU"}, {"PSK63", "DIGU"}, {"PSK", "DIGU"},
            {"OLIVIA", "DIGU"}, {"JT65", "DIGU"}, {"JT9", "DIGU"},
            {"RTTY", "DIGL"},
            {"AM", "AM"}, {"SAM", "SAM"},
            {"FM", "FM"}, {"NFM", "NFM"},
        };
        QString radioMode;
        if (modeMap.contains(spotMode)) {
            radioMode = modeMap[spotMode];
        } else if (spotMode == "SSB") {
            // SSB without sideband: ≥10 MHz → USB, <10 MHz → LSB
            radioMode = (it->rxFreqMhz >= 10.0) ? "USB" : "LSB";
        }
        if (!radioMode.isEmpty() && radioMode != s->mode())
            s->setMode(radioMode);
    });

    // ── Manual spot add/remove (#36)
    connect(sw, &SpectrumWidget::spotAddRequested, this,
            [this](double freqMhz, const QString& callsign, const QString& comment,
                   int lifetimeSec, bool forwardToCluster) {
        auto& as = AppSettings::instance();
        QString call = QString(callsign).replace(' ', QChar(0x7f));
        QString freq = QString::number(freqMhz, 'f', 6);
        QString myCall = as.value("DxClusterCallsign").toString();
        if (myCall.isEmpty()) myCall = "AetherSDR";
        QString cmd = "spot add callsign=" + call + " rx_freq=" + freq
                     + " tx_freq=" + freq
                     + " source=Manual"
                     + " spotter_callsign=" + myCall
                     + " lifetime_seconds=" + QString::number(lifetimeSec);
        if (!comment.isEmpty())
            cmd += " comment=" + QString(comment).replace(' ', QChar(0x7f));
        QString spotColor = as.value("ManualSpotColor", "#00FF00").toString();
        if (spotColor.length() == 7) spotColor = "#FF" + spotColor.mid(1);
        cmd += " color=" + spotColor;
        if (SpotCommandPolicy::shouldSendSpotAddCommands()) {
            m_radioModel.sendCommand(cmd);
        } else {
            QMap<QString, QString> kvs;
            kvs["callsign"] = QString(callsign).replace(' ', QChar(0x7f));
            kvs["rx_freq"] = freq;
            kvs["tx_freq"] = freq;
            kvs["source"] = "Manual";
            kvs["spotter_callsign"] = myCall;
            kvs["lifetime_seconds"] = QString::number(lifetimeSec);
            kvs["timestamp"] = QString::number(QDateTime::currentSecsSinceEpoch());
            if (!comment.isEmpty())
                kvs["comment"] = QString(comment).replace(' ', QChar(0x7f));
            kvs["color"] = spotColor;

            const int spotId = m_nextPassiveSpotId--;
            m_radioModel.spotModel().applySpotStatus(spotId, kvs);
            if (lifetimeSec > 0) {
                m_passiveSpotExpiryMs.insert(
                    spotId,
                    QDateTime::currentMSecsSinceEpoch() + qint64(lifetimeSec) * 1000);
            }
        }

        // Forward to DX cluster if requested
        if (forwardToCluster && m_dxCluster && m_dxCluster->isConnected()) {
            QString dxCmd = QString("DX %1 %2 %3")
                .arg(freqMhz * 1000.0, 0, 'f', 1)
                .arg(callsign)
                .arg(comment);
            QMetaObject::invokeMethod(m_dxCluster, [this, dxCmd] {
                m_dxCluster->sendCommand(dxCmd);
            });
        }
    });
    connect(sw, &SpectrumWidget::spotRemoveRequested, this, [this](int spotIndex) {
        if (isPassiveLocalSpotId(spotIndex)) {
            m_passiveSpotExpiryMs.remove(spotIndex);
            m_radioModel.spotModel().removeSpot(spotIndex);
            return;
        }
        m_radioModel.sendCommand(QString("spot remove %1").arg(spotIndex));
    });

    // ── +RX / +TNF buttons ───────────────────────────────────────────────
    connect(menu, &SpectrumOverlayMenu::addRxClicked,
            this, [this](const QString& panId) {
        int limit = m_radioModel.maxSlices();
        if (m_radioModel.slices().size() < limit) {
            m_radioModel.addSliceOnPan(panId);
        } else {
            statusBar()->showMessage(
                QString("%1 supports a maximum of %2 slices")
                    .arg(m_radioModel.model()).arg(limit), 4000);
        }
    });
    connect(menu, &SpectrumOverlayMenu::addTnfClicked,
            this, [this]() {
        auto* s = activeSlice();
        if (!s) return;
        double tnfFreq = s->frequency()
            + (s->filterLow() + s->filterHigh()) / 2.0 / 1.0e6;
        m_radioModel.tnfModel().createTnf(tnfFreq);
    });
    connect(menu, &SpectrumOverlayMenu::memoryActivated,
            this, [this](int memoryIndex, const QString& panId) {
        activateMemorySpot(memoryIndex, panId);
    });
    connect(menu, &SpectrumOverlayMenu::quickAddMemoryRequested,
            this, [this](const QString& panId) {
        showQuickAddMemoryDialog(panId);
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
    connect(sw, &SpectrumWidget::sliceTuneRequested,
            this, [this](int sliceId, double freqMhz) {
        if (auto* s = m_radioModel.slice(sliceId))
            applyTuneRequest(s, freqMhz, TuneIntent::AbsoluteJump, "slice-move-here");
    });

    // ── Band selection ───────────────────────────────────────────────────
    connect(menu, &SpectrumOverlayMenu::bandSelected,
            this, [this, applet](const QString& bandName, double freqMhz, const QString& mode) {
        qDebug() << "MainWindow: switching to band" << bandName
                 << "freq:" << freqMhz << "mode:" << mode;

        // Maintainer note: keep band changes radio-authoritative.
        //
        // The Flex band stack owns the state users expect to survive a band
        // jump: frequency, mode, filters, pan center, bandwidth/zoom, and
        // built-in antenna selection. Aether should therefore send exactly one
        // `display pan set <panId> band=<key>` command when it can form a
        // spec-correct key. Do not use the `freqMhz` / `mode` arguments as a
        // local fallback; those are static UI defaults, and the old fallback
        // reset users to band-center SSB instead of restoring their saved stack
        // state (#1876, #1852, #1856, #1849).
        //
        // UI band names are not always protocol keys:
        //   - Native bands are displayed as "20m", "630m", etc., but Flex
        //     expects bare keys such as "20" and "630".
        //   - XVTR names are user labels such as "2m" or "70cm"; do not strip
        //     those into native-band keys. Flex expects `X<order>`, where
        //     `order` is the radio's XVTR setup order, not the xvtr status
        //     object index, RF frequency, or display name.
        //   - WWV / GEN use numeric band-stack slots 33 / 34 from SmartSDR
        //     capture history (#1540/#1211).
        //
        // If no exact mapping exists, refuse the band change and leave the
        // current slice/pan state untouched. Guessing is worse than failing
        // visibly because a wrong tune destroys the very band-stack state this
        // path exists to preserve.
        const auto xvtrs = xvtrPolicyBandsFrom(m_radioModel.xvtrList());
        const auto stackKeyResult = XvtrPolicy::resolveBandStackKey(bandName, xvtrs);
        const QString stackKey = stackKeyResult.key;
        QString unsupportedBandReason = stackKeyResult.unsupportedReason;

        if (stackKey.isEmpty()) {
            qCWarning(lcProtocol).noquote().nospace()
                << "MainWindow: refusing unsupported band change band=" << bandName
                << " reason=" << unsupportedBandReason
                << " available_xvtrs=" << xvtrListSummary(xvtrs);
            statusBar()->showMessage(unsupportedBandReason, 5000);
            return;
        } else {
            qCDebug(lcProtocol).noquote().nospace()
                << "MainWindow: band switch band=" << bandName
                << " pan=" << applet->panId()
                << " key=" << stackKey
                << " freq_hint_mhz=" << QString::number(freqMhz, 'f', 6)
                << " mode_hint=" << mode
                << " xvtr=" << xvtrForBandSummary(bandName, xvtrs);
            m_bandSettings.setCurrentBand(bandName);
            m_radioModel.sendCommand(
                QString("display pan set %1 band=%2").arg(applet->panId()).arg(stackKey));
        }
    });

    // XVTR button → open Radio Setup XVTR tab (#571)
    connect(menu, &SpectrumOverlayMenu::xvtrSetupRequested,
            this, [this]() {
        auto* dlg = new RadioSetupDialog(&m_radioModel, m_audio, &m_tgxlConn, &m_pgxlConn, &m_antennaGenius, this);
        connect(dlg, &RadioSetupDialog::txBandSettingsRequested,
                m_txBandAction, &QAction::trigger);
        dlg->selectTab("XVTR");
        dlg->show();
    });

    // ── WNB / RF Gain ────────────────────────────────────────────────────
    connect(menu, &SpectrumOverlayMenu::wnbToggled,
            this, [this, sw, applet](bool on) {
        m_radioModel.sendCommand(
            QString("display pan set %1 wnb=%2").arg(applet->panId()).arg(on ? 1 : 0));
        sw->setWnbActive(on);
        auto& s = AppSettings::instance();
        s.setValue(sw->settingsKey("DisplayWnbEnabled"), on ? "True" : "False");
        s.save();
    });
    connect(menu, &SpectrumOverlayMenu::wnbLevelChanged,
            this, [this, sw, applet](int level) {
        m_radioModel.sendCommand(
            QString("display pan set %1 wnb_level=%2").arg(applet->panId()).arg(level));
        auto& s = AppSettings::instance();
        s.setValue(sw->settingsKey("DisplayWnbLevel"), QString::number(level));
        s.save();
    });
    connect(menu, &SpectrumOverlayMenu::rfGainChanged,
            this, [this, sw, applet](int gain) {
        m_radioModel.sendCommand(
            QString("display pan set %1 rfgain=%2").arg(applet->panId()).arg(gain));
        sw->setRfGain(gain);
        auto& s = AppSettings::instance();
        s.setValue(sw->settingsKey("DisplayRfGain"), QString::number(gain));
        s.save();
    });

    // ── NR2/RN2 overlay toggle → AudioEngine ───────────────────────────
    // Button sync back to overlay + VFO handled by nr2/rn2EnabledChanged above.
    connect(menu, &SpectrumOverlayMenu::nr2Toggled,
            this, [this, menu](bool on) {
        if (on) {
            // NR2 amplifies Opus codec artifacts → disable when compressed audio active (#1597)
            if (m_radioModel.audioCompressionParam() == "opus") {
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr2Enabled(false); });
                statusBar()->showMessage("NR2 is not available with compressed (SmartLink) audio", 4000);
                return;
            }
            QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setRn2Enabled(false); });
            enableNr2WithWisdom();
        } else {
            QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr2Enabled(false); });
        }
        // VFO button sync happens via AudioEngine::nr2EnabledChanged signal
    });
    connect(menu, &SpectrumOverlayMenu::nr2RightClicked,
            this, [this](const QPoint& pos) { showNr2ParamPopup(pos); });
    connect(menu, &SpectrumOverlayMenu::rn2Toggled,
            this, [this](bool on) {
        if (on) {
            QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr2Enabled(false); });
            QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setRn2Enabled(true); });
        } else {
            QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setRn2Enabled(false); });
        }
        // VFO button sync happens via AudioEngine::rn2EnabledChanged signal
    });
    connect(menu, &SpectrumOverlayMenu::bnrToggled,
            this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setBnrEnabled(on); });
        // VFO button sync happens via AudioEngine::bnrEnabledChanged signal
    });
    connect(menu, &SpectrumOverlayMenu::bnrIntensityChanged,
            this, [this](float ratio) {
        m_audio->setBnrIntensity(ratio);
    });
    connect(menu, &SpectrumOverlayMenu::nr4Toggled,
            this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr4Enabled(on); });
        // VFO button sync happens via AudioEngine::nr4EnabledChanged signal
    });
    connect(menu, &SpectrumOverlayMenu::nr4RightClicked,
            this, [this](const QPoint& pos) { showNr4ParamPopup(pos); });
    connect(menu, &SpectrumOverlayMenu::mnrToggled,
            this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setMnrEnabled(on); });
        // VFO button sync happens via AudioEngine::mnrEnabledChanged signal
    });
    connect(menu, &SpectrumOverlayMenu::mnrRightClicked,
            this, [this](const QPoint&) { showMnrSettings(); });
    connect(menu, &SpectrumOverlayMenu::dfnrToggled,
            this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setDfnrEnabled(on); });
    });
    connect(menu, &SpectrumOverlayMenu::dfnrRightClicked,
            this, [this](const QPoint& pos) { showDfnrParamPopup(pos); });
}

MainWindow::TuneCenteringResult MainWindow::revealFrequencyIfNeeded(
    SliceModel* slice, double mhz, TuneIntent intent, const char* source)
{
    TuneCenteringResult result;
    if (!slice)
        return result;

    auto* pan = m_radioModel.panadapter(slice->panId());
    if (!pan)
        return result;

    // Prefer the visible spectrum state when available so reveal/follow stays
    // aligned with recent manual pan/zoom gestures even if the model echo lags.
    auto* sw = spectrumForSlice(slice);
    result.oldCenterMhz = sw ? sw->centerMhz() : pan->centerMhz();
    result.newCenterMhz = result.oldCenterMhz;
    result.bandwidthMhz = sw ? sw->bandwidthMhz() : pan->bandwidthMhz();

    const double bandwidthMhz = result.bandwidthMhz;
    const double halfBw = bandwidthMhz / 2.0;
    if (halfBw <= 0.0)
        return result;

    if (intent == TuneIntent::CommandedTargetCenter) {
        result.newCenterMhz = mhz;
        if (qFuzzyCompare(result.oldCenterMhz, result.newCenterMhz))
            return result;

        result.followRevealTriggered = true;
        result.hardCenterUsed = true;
        const double centerDelta = std::abs(result.newCenterMhz - result.oldCenterMhz);
        result.animationDurationMs = (centerDelta <= halfBw * 0.25)
            ? kPanFollowAnimationDurationMs
            : 0;

        pan->applyPanStatus({{"center", QString::number(result.newCenterMhz, 'f', 6)}});
        m_radioModel.sendCommand(
            QString("display pan set %1 center=%2")
                .arg(pan->panId()).arg(result.newCenterMhz, 0, 'f', 6));
        return result;
    }

    const bool incremental = (intent == TuneIntent::IncrementalTune);
    if (incremental && !panFollowEnabled())
        return result;

    const bool isSpectrumClick = source && qstrcmp(source, "spectrum-click") == 0;
    const bool revealOffscreen = (intent == TuneIntent::RevealOffscreen);
    const double comfortMargin =
        revealOffscreen   ? 0.0
        : isSpectrumClick ? kSpectrumClickEdgeMarginFrac
                          : kRevealComfortEdgeMarginFrac;
    const double triggerEdgeMarginFrac =
        incremental ? kIncrementalTriggerEdgeMarginFrac : comfortMargin;
    const double settleEdgeMarginFrac =
        incremental ? kIncrementalSettleEdgeMarginFrac : comfortMargin;
    const double triggerDistanceFromCenter = halfBw - bandwidthMhz * triggerEdgeMarginFrac;
    const double settleDistanceFromCenter = halfBw - bandwidthMhz * settleEdgeMarginFrac;
    const double center = result.oldCenterMhz;
    const int stepHz = incremental
        ? (slice->stepHz() > 0 ? slice->stepHz() : (sw ? sw->stepSize() : 0))
        : 0;
    const double stepMhz = stepHz > 0 ? stepHz / 1e6 : 0.0;

    auto incrementalFollowCenter = [&](double triggerBoundaryCenter,
                                       double settleCenterTarget) {
        const double direction = (triggerBoundaryCenter >= center) ? 1.0 : -1.0;
        const double overshootMhz = std::abs(triggerBoundaryCenter - center);
        const double quantizedDeltaMhz = quantizeIncrementalFollowDelta(overshootMhz, stepMhz);
        const double cappedDeltaMhz =
            std::min(quantizedDeltaMhz, std::abs(settleCenterTarget - center));
        return center + direction * cappedDeltaMhz;
    };

    if (mhz > center + triggerDistanceFromCenter) {
        const double settleCenterTarget = mhz - settleDistanceFromCenter;
        if (incremental && stepMhz > 0.0) {
            const double triggerBoundaryCenter = mhz - triggerDistanceFromCenter;
            result.newCenterMhz =
                incrementalFollowCenter(triggerBoundaryCenter, settleCenterTarget);
        } else {
            result.newCenterMhz = settleCenterTarget;
        }
    } else if (mhz < center - triggerDistanceFromCenter) {
        const double settleCenterTarget = mhz + settleDistanceFromCenter;
        if (incremental && stepMhz > 0.0) {
            const double triggerBoundaryCenter = mhz + triggerDistanceFromCenter;
            result.newCenterMhz =
                incrementalFollowCenter(triggerBoundaryCenter, settleCenterTarget);
        } else {
            result.newCenterMhz = settleCenterTarget;
        }
    } else {
        return result;
    }

    if (qFuzzyCompare(result.oldCenterMhz, result.newCenterMhz))
        return result;

    result.followRevealTriggered = true;
    const double centerDelta = std::abs(result.newCenterMhz - result.oldCenterMhz);
    result.animationDurationMs = (centerDelta <= halfBw * 0.25)
        ? kPanFollowAnimationDurationMs
        : 0;

    // Apply the center optimistically so the owning spectrum repaints
    // immediately; SpectrumWidget decides whether that becomes a short
    // retargetable animation or an immediate snap based on shift size.
    pan->applyPanStatus({{"center", QString::number(result.newCenterMhz, 'f', 6)}});
    m_radioModel.sendCommand(
        QString("display pan set %1 center=%2")
            .arg(pan->panId()).arg(result.newCenterMhz, 0, 'f', 6));
    return result;
}

MainWindow::TuneCenteringResult MainWindow::panFollowVfo(
    SliceModel* s, double mhz, const char* source)
{
    // Incremental tuning uses a trigger margin and a slightly wider settle
    // margin so the slice glides back into a comfortable visible area without
    // dead-centering or waiting until it actually crosses the pan edge.
    return revealFrequencyIfNeeded(s, mhz, TuneIntent::IncrementalTune, source);
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
    connect(w, &VfoWidget::stepTuneRequested, this, [this, sliceId](double mhz) {
        if (auto* sl = m_radioModel.slice(sliceId))
            applyTuneRequest(sl, mhz, TuneIntent::IncrementalTune, "vfo-wheel");
    });
    connect(w, &VfoWidget::directEntryCommitted, this, [this, sliceId](double mhz, const QString& source) {
        if (auto* sl = m_radioModel.slice(sliceId)) {
            const QByteArray sourceUtf8 = source.toUtf8();
            applyTuneRequest(sl, mhz, TuneIntent::CommandedTargetCenter, sourceUtf8.constData());
        }
    });
    connect(w, &VfoWidget::lockToggled, this, [this, sliceId](bool locked) {
        if (auto* sl = m_radioModel.slice(sliceId))
            sl->setLocked(locked);
    });
    connect(w, &VfoWidget::afGainChanged, this, [this, sliceId](int v) {
        if (auto* sl = m_radioModel.slice(sliceId))
            sl->setAudioGain(v);
    });
    // Record/playback — route to radio or client-side QsoRecorder (#1297)
    connect(w, &VfoWidget::recordToggled, this, [this, w, sliceId](bool on) {
        bool clientSide = AppSettings::instance().value("RecordingMode", "Radio").toString() == "Client";
        if (clientSide) {
            if (on)
                m_qsoRecorder->startRecording();
            else
                m_qsoRecorder->stopRecording();
            w->setRecordOn(on);  // drive pulse animation for client-side
        } else {
            if (auto* sl = m_radioModel.slice(sliceId))
                sl->setRecordOn(on);
        }
    });
    // Client-side recording stopped by idle timeout → update VFO button
    connect(m_qsoRecorder, &QsoRecorder::recordingStopped, w, [w]() {
        w->setRecordOn(false);
        w->setPlayEnabled(true);
    });
    // Client-side playback
    connect(w, &VfoWidget::playToggled, this, [this, w, sliceId](bool on) {
        bool clientSide = AppSettings::instance().value("RecordingMode", "Radio").toString() == "Client";
        if (clientSide) {
            if (on)
                m_qsoRecorder->startPlayback();
            else
                m_qsoRecorder->stopPlayback();
        } else {
            if (auto* sl = m_radioModel.slice(sliceId))
                sl->setPlayOn(on);
        }
    });
    connect(m_qsoRecorder, &QsoRecorder::playbackStopped, w, [w]() {
        w->setPlayOn(false);
    });
    connect(s, &SliceModel::recordOnChanged, w, &VfoWidget::setRecordOn);
    connect(s, &SliceModel::playOnChanged, w, &VfoWidget::setPlayOn);
    connect(s, &SliceModel::playEnabledChanged, w, &VfoWidget::setPlayEnabled);
    connect(w, &VfoWidget::autotuneRequested, this, [this, sliceId](bool intermittent) {
        if (m_radioModel.slice(sliceId))
            m_radioModel.cwAutoTune(sliceId, intermittent);
    });
    connect(w, &VfoWidget::autotuneOnceRequested, this, [this, sliceId]() {
        if (m_radioModel.slice(sliceId))
            m_radioModel.cwAutoTuneOnce(sliceId);
    });
    connect(w, &VfoWidget::zeroBeatRequested, this, [this]() {
        SliceModel* slice = activeSlice();
        if (!slice) return;
        float detected = m_cwDecoder.estimatedPitch();
        if (detected <= 0.0f) return;
        int configured = m_radioModel.transmitModel().cwPitch();
        double offsetMhz = (detected - configured) / 1.0e6;
        applyTuneRequest(slice, slice->frequency() + offsetMhz,
                         TuneIntent::IncrementalTune, "zero-beat");
    });
    connect(w, &VfoWidget::addSpotRequested, this, [this](double freqMhz) {
        if (auto* sw = spectrum()) sw->showAddSpotDialog(freqMhz);
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
        applyTuneRequest(rx, txFreq, TuneIntent::IncrementalTune, "split-swap-rx");
        applyTuneRequest(tx, rxFreq, TuneIntent::IncrementalTune, "split-swap-tx");
    });

    // Split toggle — per-widget, slice-aware (#328)
    connect(w, &VfoWidget::splitToggled, this, [this, sliceId]() {
        if (!m_splitActive) {
            // Entering split: this slice becomes RX, create a new TX slice
            if (m_radioModel.slices().size() >= m_radioModel.maxSlices())
                return;
            auto* rxSlice = m_radioModel.slice(sliceId);
            if (!rxSlice) return;

            // Create TX slice on the SAME pan as the RX slice
            QString panId = rxSlice->panId();
            if (panId.isEmpty())
                panId = m_panStack ? m_panStack->activePanId() : m_radioModel.panId();

            // CW split: offset 1 kHz up (convention). Other modes: 5 kHz up.
            const QString mode = rxSlice->mode();
            bool isCw = mode == "CW" || mode == "CWL";
            double offsetMhz = isCw ? 0.001 : 0.005;
            double txFreq = rxSlice->frequency() + offsetMhz;

            m_splitActive = true;
            m_splitRxSliceId = sliceId;
            m_radioModel.sendCommand(
                QString("slice create pan=%1 freq=%2")
                    .arg(panId).arg(txFreq, 0, 'f', 6));
        } else if (sliceId == m_splitRxSliceId) {
            // Clicking SPLIT on the RX VFO again → disable split, destroy TX slice
            disableSplit();
        }
    });

    // NR2 toggle with FFTW wisdom generation — wired once per VFO, never disconnected
    connect(w, &VfoWidget::nr2Toggled, this, [this, w](bool on) {
        if (!on) { QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr2Enabled(false); }); return; }
        // NR2 amplifies Opus codec artifacts → disable when compressed audio is active (#1597)
        if (m_radioModel.audioCompressionParam() == "opus") {
            QSignalBlocker blocker(w->nr2Button());
            w->nr2Button()->setChecked(false);
            statusBar()->showMessage("NR2 is not available with compressed (SmartLink) audio", 4000);
            return;
        }
        enableNr2WithWisdom();
    });
    connect(w, &VfoWidget::nr2RightClicked,
            this, [this](const QPoint& pos) { showNr2ParamPopup(pos); });

    // RN2 toggle
    connect(w, &VfoWidget::rn2Toggled, this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setRn2Enabled(on); });
    });
    connect(w, &VfoWidget::bnrToggled, this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setBnrEnabled(on); });
    });
    connect(w, &VfoWidget::nr4Toggled, this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr4Enabled(on); });
    });
    connect(w, &VfoWidget::nr4RightClicked,
            this, [this](const QPoint& pos) { showNr4ParamPopup(pos); });
    connect(w, &VfoWidget::mnrToggled, this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setMnrEnabled(on); });
    });
    connect(w, &VfoWidget::mnrRightClicked, this, [this](const QPoint&) { showMnrSettings(); });
    connect(w, &VfoWidget::dfnrToggled, this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setDfnrEnabled(on); });
    });
    connect(w, &VfoWidget::dfnrRightClicked,
            this, [this](const QPoint& pos) { showDfnrParamPopup(pos); });

#ifdef HAVE_RADE
    connect(w, &VfoWidget::radeActivated, this, [this](bool on, int sliceId) {
        if (on) activateRADE(sliceId); else deactivateRADE();
    });
#endif

    // Per-slice VFO marker style (#1526): push user's saved line thickness and
    // filter-edge visibility preferences to this slice's overlay whenever they
    // change. Also fires once on setSlice() below to apply loaded defaults.
    connect(w, &VfoWidget::markerStyleChanged, this,
            [this, sliceId](bool thin, bool hideEdges) {
        auto* sl = m_radioModel.slice(sliceId);
        if (!sl) return;
        if (auto* sw = spectrumForSlice(sl))
            sw->setSliceOverlayMarkerStyle(sliceId, thin, hideEdges);
    });

    // Pan re-apply after NR mono-mix (#1460, #1796): keep AudioEngine in sync
    // with the radio-side pan value from ALL sources (VFO panel, RX applet,
    // MIDI, radio echo-back on connect).  Connecting SliceModel::audioPanChanged
    // covers every path that calls setAudioPan(); only the active slice's value
    // matters because AudioEngine plays one slice at a time.
    connect(s, &SliceModel::audioPanChanged, this, [this, sliceId](int v) {
        if (sliceId == m_activeSliceId)
            m_audio->setRxPan(v);
    });

    // Per-slice AF mute persistence (#1560): save state to AppSettings on each
    // toggle so it survives the session; restore it once the slice is ready
    // (the radio does not persist audio_mute between client connections).
    // Use the user-visible slice letter (A/B/C/D) as the key so the saved state
    // survives sessions where the radio assigns different numeric IDs (#1560).
    const QChar sliceLetter = QChar('A' + sliceId);
    connect(w, &VfoWidget::audioMuteToggled, this, [this, sliceLetter](bool on) {
        AppSettings::instance().setValue(
            QString("SliceAudioMuted_%1").arg(sliceLetter), on ? "True" : "False");
    });
    {
        bool savedMute = AppSettings::instance()
            .value(QString("SliceAudioMuted_%1").arg(sliceLetter), "False")
            .toString() == "True";
        if (savedMute)
            s->setAudioMute(true);  // send audio_mute=1 to radio for this slice
    }

    // Wire slice data into widget
    w->setSlice(s);
    w->setAntennaList(m_radioModel.antennaList());
    w->setTransmitModel(&m_radioModel.transmitModel());
}

// wireActiveVfoSignals removed — NR2/RN2/RADE are now wired permanently
// in wireVfoWidget() so connections survive focus switches (#227).

void MainWindow::updateNr2Availability()
{
    bool opusActive = (m_radioModel.audioCompressionParam() == "opus");
    const QString tooltip = opusActive
        ? "NR2 is not available with compressed (SmartLink) audio"
        : "Client-side spectral noise reduction (Ephraim-Malah MMSE). Right-click for NR2 settings.";

    // If Opus just became active and NR2 is running, disable it
    if (opusActive && m_audio->nr2Enabled()) {
        QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr2Enabled(false); });
        statusBar()->showMessage("NR2 disabled — not available with compressed (SmartLink) audio", 4000);
    }

    // Update all NR2 buttons: VFO widgets and spectrum overlay menus
    if (m_panStack) {
        for (auto* applet : m_panStack->allApplets()) {
            // Overlay menu NR2 button
            if (auto* menu = applet->spectrumWidget()->overlayMenu()) {
                if (auto* btn = menu->dspNr2Button()) {
                    btn->setEnabled(!opusActive);
                    btn->setToolTip(tooltip);
                }
            }
            // VFO NR2 buttons (one per slice on this pan)
            for (auto* s : m_radioModel.slices()) {
                if (auto* vfo = applet->spectrumWidget()->vfoWidget(s->sliceId())) {
                    if (auto* btn = vfo->nr2Button()) {
                        btn->setEnabled(!opusActive);
                        btn->setToolTip(tooltip);
                    }
                }
            }
        }
    }
}

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
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr2Enabled(true); });
            });
        });
        thread->start();
    } else {
        QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr2Enabled(true); });
    }
}

SpectrumWidget* MainWindow::spectrum() const
{
    return m_panStack ? m_panStack->activeSpectrum()
                      : (m_panApplet ? m_panApplet->spectrumWidget() : nullptr);
}

// ── UI Scale helpers ────────────────────────────────────────────────────
static constexpr int kScaleSteps[] = {75, 85, 100, 110, 125, 150, 175, 200};
static constexpr int kScaleStepCount = sizeof(kScaleSteps) / sizeof(kScaleSteps[0]);

void MainWindow::applyUiScale(int pct)
{
    int current = AppSettings::instance().value("UiScalePercent", "100").toInt();
    if (pct == current)
        return;

    AppSettings::instance().setValue("UiScalePercent", QString::number(pct));
    AppSettings::instance().save();

    auto answer = QMessageBox::question(this, "UI Scale",
        QString("UI scale changed to %1%. Restart AetherSDR now to apply?").arg(pct),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                QCoreApplication::arguments().mid(1));
        QCoreApplication::quit();
    }
}

void MainWindow::stepUiScale(int direction)
{
    int current = AppSettings::instance().value("UiScalePercent", "100").toInt();
    // Find nearest step in the requested direction
    int best = current;
    if (direction > 0) {
        for (int i = 0; i < kScaleStepCount; ++i) {
            if (kScaleSteps[i] > current) { best = kScaleSteps[i]; break; }
        }
    } else {
        for (int i = kScaleStepCount - 1; i >= 0; --i) {
            if (kScaleSteps[i] < current) { best = kScaleSteps[i]; break; }
        }
    }
    if (best != current)
        applyUiScale(best);
}

void MainWindow::setFramelessWindow(bool on)
{
    auto& s = AppSettings::instance();
    s.setValue("FramelessWindow", on ? "True" : "False");
    s.save();

    // setWindowFlags() re-creates the native window — save and restore
    // geometry so the window stays where the user put it.
    const QRect geom = geometry();
    const bool wasVisible = isVisible();
    Qt::WindowFlags flags = windowFlags();
    if (on)
        flags |= Qt::FramelessWindowHint;
    else
        flags &= ~Qt::FramelessWindowHint;
    setWindowFlags(flags);
    setGeometry(geom);
    if (wasVisible)
        show();

    // Keep the bottom-right size grip in sync — only useful when frameless.
    if (m_sizeGrip) m_sizeGrip->setVisible(on);
}

void MainWindow::toggleMinimalMode(bool on)
{
    m_minimalMode = on;
    auto& s = AppSettings::instance();

    if (on) {
        // Save full-mode geometry
        s.setValue("FullModeGeometry", saveGeometry().toBase64());

        // Save splitter sizes for restore
        s.setValue("MinimalModeSplitterSizes",
            QString::fromLatin1(m_splitter->saveState().toBase64()));

        // Suspend spectrum rendering to save CPU (skip floating pans —
        // they remain visible in their own top-level window)
        if (m_panStack) {
            for (auto* a : m_panStack->allApplets()) {
                if (!m_panStack->isFloating(a->panId()))
                    a->spectrumWidget()->setUpdatesEnabled(false);
            }
        }

        // Hide the splitter (contains spectrum + applet panel) and reparent
        // the applet panel directly into the central layout
        m_splitter->hide();
        m_appletPanel->setParent(centralWidget());
        centralWidget()->layout()->addWidget(m_appletPanel);
        m_appletPanel->show();

        // Strip title bar to heartbeat + logo + restore/feature buttons
        m_titleBar->setMinimalMode(true);
        statusBar()->hide();

        // Force window to applet width
        setMinimumSize(0, 0);
        setFixedWidth(260);

        QByteArray geom = QByteArray::fromBase64(
            s.value("MinimalModeGeometry", "").toByteArray());
        if (!geom.isEmpty())
            restoreGeometry(geom);

    } else {
        // Save minimal geometry
        s.setValue("MinimalModeGeometry", saveGeometry().toBase64());

        // Reparent applet panel back into the splitter and restore layout
        m_splitter->addWidget(m_appletPanel);
        m_appletPanel->show();
        QByteArray splitterState = QByteArray::fromBase64(
            s.value("MinimalModeSplitterSizes", "").toByteArray());
        if (!splitterState.isEmpty())
            m_splitter->restoreState(splitterState);
        m_splitter->show();

        // Resume spectrum rendering
        if (m_panStack) {
            for (auto* a : m_panStack->allApplets())
                a->spectrumWidget()->setUpdatesEnabled(true);
        }

        // Release fixed width and restore minimum size
        setFixedWidth(QWIDGETSIZE_MAX);
        setMinimumSize(1024, 400);

        // Restore title bar and status bar
        m_titleBar->setMinimalMode(false);
        statusBar()->show();

        // Restore full geometry
        QByteArray geom = QByteArray::fromBase64(
            s.value("FullModeGeometry", "").toByteArray());
        if (!geom.isEmpty())
            restoreGeometry(geom);
    }

    s.setValue("MinimalModeEnabled", on ? "True" : "False");
    s.save();
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

void MainWindow::updateKeyerAvailability(const QString& mode)
{
    static const QString kActive   = "QLabel { color: #00b4d8; font-weight: bold; font-size: 24px; }";
    static const QString kAvail    = "QLabel { color: rgba(255,255,255,40); font-weight: bold; font-size: 24px; }";
    static const QString kDisabled = "QLabel { color: #252530; font-weight: bold; font-size: 24px; }";

    bool isCw  = (mode == "CW" || mode == "CWL");
    bool isSsb = (mode == "USB" || mode == "LSB" || mode == "AM" || mode == "SAM"
                  || mode == "FM" || mode == "NFM" || mode == "DFM");

    // CWX: available in CW modes only
    m_cwxIndicator->setEnabled(isCw);
    if (!isCw && m_cwxPanel->isVisible()) {
        m_cwxPanel->hide();
        m_cwxIndicator->setStyleSheet(kDisabled);
    } else if (m_cwxPanel->isVisible()) {
        m_cwxIndicator->setStyleSheet(kActive);
    } else {
        m_cwxIndicator->setStyleSheet(isCw ? kAvail : kDisabled);
    }
    m_cwxIndicator->setCursor(isCw ? Qt::PointingHandCursor : Qt::ArrowCursor);

    // DVK: available in voice modes (SSB, AM, FM — not DIGU/DIGL)
    m_dvkIndicator->setEnabled(isSsb);
    if (!isSsb && m_dvkPanel->isVisible()) {
        m_dvkPanel->hide();
        m_dvkIndicator->setStyleSheet(kDisabled);
    } else if (m_dvkPanel->isVisible()) {
        m_dvkIndicator->setStyleSheet(kActive);
    } else {
        m_dvkIndicator->setStyleSheet(isSsb ? kAvail : kDisabled);
    }
    m_dvkIndicator->setCursor(isSsb ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

void MainWindow::centerActiveSliceInPanadapter(bool forceRadioCenter, double centerMhz)
{
    auto* s = activeSlice();
    if (!s || s->panId().isEmpty()) return;

    auto* sw = spectrumForSlice(s);
    if (!sw) return;

    auto* pan = m_radioModel.panadapter(s->panId());
    const double bandwidthMhz = pan ? pan->bandwidthMhz() : m_radioModel.panBandwidthMhz();
    const double targetMhz = (centerMhz > 0.0) ? centerMhz : s->frequency();

    if (m_panStack) {
        if (auto* applet = m_panStack->panadapter(s->panId()))
            m_panStack->setActivePan(applet->panId());
    }

    // Keep the local spectrum centered immediately so the active slice marker
    // is visible before the radio's status echo arrives.
    sw->setFrequencyRange(targetMhz, bandwidthMhz);
    sw->setVfoFrequency(targetMhz);

    if (forceRadioCenter && m_radioModel.isConnected()) {
        m_radioModel.sendCommand(
            QString("display pan set %1 center=%2")
                .arg(s->panId()).arg(targetMhz, 0, 'f', 6));
    }

    TuneCenteringResult result;
    result.oldCenterMhz = pan ? pan->centerMhz() : targetMhz;
    result.newCenterMhz = targetMhz;
    result.bandwidthMhz = bandwidthMhz;
    result.followRevealTriggered = true;
    result.hardCenterUsed = true;
    logTunePolicyDecision("center-active-slice", TuneIntent::AbsoluteJump,
                          s->frequency(), s->frequency(), result);
}

void MainWindow::registerShortcutActions()
{
    // Helper: nudge active slice frequency by N steps.
    // Share the incremental tune policy with wheel/knob/VFO tuning so pan
    // follow uses the same trigger + settle margins everywhere.
    auto nudgeFreq = [this](int steps) {
        if (!m_radioModel.isConnected()) return;
        auto* s = activeSlice();
        if (!s || s->isLocked()) return;
        int stepHz = spectrum() ? spectrum()->stepSize() : 100;
        double newMhz = s->frequency() + steps * stepHz / 1e6;
        applyTuneRequest(s, newMhz, TuneIntent::IncrementalTune, "keyboard-step");
    };

    // Step cycle helper
    auto cycleStep = [this](int dir) {
        auto* sw = spectrum();
        if (!sw) return;
        static const int steps[] = {10, 50, 100, 250, 500, 1000, 2500, 5000, 10000};
        int cur = sw->stepSize();
        if (dir > 0) {
            for (int i = 0; i < static_cast<int>(std::size(steps)); ++i)
                if (steps[i] > cur) { sw->setStepSize(steps[i]); return; }
        } else {
            for (int i = static_cast<int>(std::size(steps)) - 1; i >= 0; --i)
                if (steps[i] < cur) { sw->setStepSize(steps[i]); return; }
        }
    };

    // ── Frequency ───────────────────────────────────────────────────────
    // autoRepeat=true so holding the key continuously tunes (accessibility).
    m_shortcutManager.registerAction("tune_up_1", "Tune Up (1 step)", "Frequency",
        QKeySequence(Qt::Key_Right), [nudgeFreq]() { nudgeFreq(1); }, true);
    m_shortcutManager.registerAction("tune_down_1", "Tune Down (1 step)", "Frequency",
        QKeySequence(Qt::Key_Left), [nudgeFreq]() { nudgeFreq(-1); }, true);
    m_shortcutManager.registerAction("tune_up_10", "Tune Up (10 steps)", "Frequency",
        QKeySequence(Qt::SHIFT | Qt::Key_Right), [nudgeFreq]() { nudgeFreq(10); }, true);
    m_shortcutManager.registerAction("tune_down_10", "Tune Down (10 steps)", "Frequency",
        QKeySequence(Qt::SHIFT | Qt::Key_Left), [nudgeFreq]() { nudgeFreq(-10); }, true);
    m_shortcutManager.registerAction("tune_up_1mhz", "Tune Up 1 MHz", "Frequency",
        QKeySequence(), [nudgeFreq]() { nudgeFreq(10000); });
    m_shortcutManager.registerAction("tune_down_1mhz", "Tune Down 1 MHz", "Frequency",
        QKeySequence(), [nudgeFreq]() { nudgeFreq(-10000); });
    m_shortcutManager.registerAction("go_to_freq", "Go to Frequency", "Frequency",
        QKeySequence(Qt::Key_G), [this]() {
            auto* s = activeSlice();
            auto* sw = s ? spectrumForSlice(s) : nullptr;
            auto* vfo = (s && sw) ? sw->vfoWidget(s->sliceId()) : nullptr;
            if (!s || !vfo) return;
            QPointer<VfoWidget> vfoGuard = vfo;
            QTimer::singleShot(0, this, [vfoGuard]() {
                if (vfoGuard)
                    vfoGuard->beginDirectEntry("go-to-frequency");
            });
        });

    // ── Band ────────────────────────────────────────────────────────────
    struct BandDef { const char* id; const char* name; double mhz; };
    static const BandDef bands[] = {
        {"band_160m", "160m", 1.900}, {"band_80m", "80m", 3.800},
        {"band_60m", "60m", 5.357},   {"band_40m", "40m", 7.200},
        {"band_30m", "30m", 10.125},  {"band_20m", "20m", 14.225},
        {"band_17m", "17m", 18.118},  {"band_15m", "15m", 21.300},
        {"band_12m", "12m", 24.940},  {"band_10m", "10m", 28.400},
        {"band_6m",  "6m",  50.125},  {"band_2m",  "2m",  146.000},
    };
    for (const auto& b : bands) {
        double freq = b.mhz;
        m_shortcutManager.registerAction(b.id, b.name, "Band",
            QKeySequence(), [this, freq]() {
                if (!m_radioModel.isConnected()) return;
                auto* s = activeSlice();
                if (s && !s->isLocked()) {
                    TuneCenteringResult result;
                    if (auto* pan = m_radioModel.panadapter(s->panId())) {
                        result.oldCenterMhz = pan->centerMhz();
                        result.bandwidthMhz = pan->bandwidthMhz();
                    }
                    result.newCenterMhz = freq;
                    result.followRevealTriggered = true;
                    result.hardCenterUsed = true;
                    logTunePolicyDecision("band-shortcut", TuneIntent::AbsoluteJump,
                                          s->frequency(), freq, result);
                    s->tuneAndRecenter(freq);
                }
            });
    }

    // ── Mode ────────────────────────────────────────────────────────────
    static const char* modes[] = {"USB", "LSB", "CW", "CWL", "AM", "SAM", "FM", "NFM", "DFM", "DIGU", "DIGL", "RTTY"};
    for (const char* mode : modes) {
        QString m = mode;
        m_shortcutManager.registerAction(
            QString("mode_%1").arg(m.toLower()), m, "Mode",
            QKeySequence(), [this, m]() {
                if (!m_radioModel.isConnected()) return;
                auto* s = activeSlice();
                if (s) s->setMode(m);
            });
    }

    // ── TX ──────────────────────────────────────────────────────────────
    m_shortcutManager.registerAction("mox_toggle", "MOX Toggle", "TX",
        QKeySequence(Qt::Key_T), [this]() {
            if (!m_radioModel.isConnected()) return;
            m_radioModel.setTransmit(!m_radioModel.transmitModel().isTransmitting());
        });
    // PTT (Hold) via Space is handled by the app-level event filter
    // because QShortcut has no "released" signal. Register with null
    // handler so the keyboard map shows it as bound.
    m_shortcutManager.registerAction("ptt_hold", "PTT (Hold)", "TX",
        QKeySequence(Qt::Key_Space), nullptr);
    m_shortcutManager.registerAction("tune_toggle", "TUNE Toggle", "TX",
        QKeySequence(), [this]() {
            if (!m_radioModel.isConnected()) return;
            if (m_radioModel.transmitModel().isTuning())
                m_radioModel.transmitModel().stopTune();
            else
                m_radioModel.transmitModel().startTune();
        });
    m_shortcutManager.registerAction("two_tone_tune", "Two-Tone Tune", "TX",
        QKeySequence(), [this]() {
            if (!m_radioModel.isConnected()) return;
            m_radioModel.transmitModel().toggleTwoToneTune();
        });

    // ── Audio ───────────────────────────────────────────────────────────
    m_shortcutManager.registerAction("af_gain_up", "AF Gain Up", "Audio",
        QKeySequence(Qt::Key_Up), [this]() {
            auto* s = activeSlice();
            if (s) s->setAudioGain(std::min(100.0f, s->audioGain() + 5.0f));
        });
    m_shortcutManager.registerAction("af_gain_down", "AF Gain Down", "Audio",
        QKeySequence(Qt::Key_Down), [this]() {
            auto* s = activeSlice();
            if (s) s->setAudioGain(std::max(0.0f, s->audioGain() - 5.0f));
        });
    m_shortcutManager.registerAction("mute_toggle", "Mute Toggle", "Audio",
        QKeySequence(Qt::Key_M), [this]() {
            auto* s = activeSlice();
            if (s) s->setAudioMute(!s->audioMute());
        });

    // ── Slice ───────────────────────────────────────────────────────────
    m_shortcutManager.registerAction("next_slice", "Next Slice", "Slice",
        QKeySequence(), [this]() {
            const auto& slices = m_radioModel.slices();
            if (slices.size() <= 1) return;
            int idx = 0;
            for (int i = 0; i < slices.size(); ++i)
                if (slices[i]->sliceId() == m_activeSliceId) { idx = i; break; }
            setActiveSlice(slices[(idx + 1) % slices.size()]->sliceId());
        });
    m_shortcutManager.registerAction("prev_slice", "Prev Slice", "Slice",
        QKeySequence(), [this]() {
            const auto& slices = m_radioModel.slices();
            if (slices.size() <= 1) return;
            int idx = 0;
            for (int i = 0; i < slices.size(); ++i)
                if (slices[i]->sliceId() == m_activeSliceId) { idx = i; break; }
            setActiveSlice(slices[(idx - 1 + slices.size()) % slices.size()]->sliceId());
        });
    m_shortcutManager.registerAction("split_toggle", "Split Toggle", "Slice",
        QKeySequence(), [this]() {
            if (!m_splitActive) {
                if (m_radioModel.slices().size() >= m_radioModel.maxSlices()) return;
                auto* s = activeSlice();
                if (!s) return;
                QString panId = s->panId();
                if (panId.isEmpty())
                    panId = m_panStack ? m_panStack->activePanId() : m_radioModel.panId();
                bool isCw = s->mode() == "CW" || s->mode() == "CWL";
                double txFreq = s->frequency() + (isCw ? 0.001 : 0.005);
                m_splitActive = true;
                m_splitRxSliceId = s->sliceId();
                m_radioModel.sendCommand(
                    QString("slice create pan=%1 freq=%2").arg(panId).arg(txFreq, 0, 'f', 6));
            } else {
                disableSplit();
            }
        });

    // ── Filter ──────────────────────────────────────────────────────────
    m_shortcutManager.registerAction("filter_widen", "Filter Widen", "Filter",
        QKeySequence(), [this]() {
            auto* s = activeSlice();
            if (!s) return;
            s->setFilterWidth(s->filterLow(), s->filterHigh() + 100);
        });
    m_shortcutManager.registerAction("filter_narrow", "Filter Narrow", "Filter",
        QKeySequence(), [this]() {
            auto* s = activeSlice();
            if (!s) return;
            s->setFilterWidth(s->filterLow(), std::max(s->filterLow() + 50, s->filterHigh() - 100));
        });

    // ── Tuning ──────────────────────────────────────────────────────────
    m_shortcutManager.registerAction("step_up", "Step Size Up", "Tuning",
        QKeySequence(Qt::Key_BracketRight), [cycleStep]() { cycleStep(1); });
    m_shortcutManager.registerAction("step_down", "Step Size Down", "Tuning",
        QKeySequence(Qt::Key_BracketLeft), [cycleStep]() { cycleStep(-1); });
    m_shortcutManager.registerAction("lock_toggle", "Tune Lock Toggle", "Tuning",
        QKeySequence(Qt::Key_L), [this]() {
            auto* s = activeSlice();
            if (s) s->setLocked(!s->isLocked());
        });

    static constexpr double kPanZoomFactor = 1.5;
    auto zoomActivePanadapter = [this](double factor) {
        if (!m_radioModel.isConnected()) {
            return;
        }

        auto* s = activeSlice();
        if (!s || s->panId().isEmpty()) {
            return;
        }

        auto* sw = spectrumForSlice(s);
        if (!sw) {
            return;
        }

        const double currentBw = sw->bandwidthMhz();
        // Clamp to limits so the final keypress snaps to exact min/max (#1458).
        const double newBw = std::clamp(currentBw * factor,
                                        m_radioModel.minPanBandwidthMhz(),
                                        m_radioModel.maxPanBandwidthMhz());
        if (newBw == currentBw) return;  // already at the hard limit

        double newCenter = sw->centerMhz();

        // When zooming in, shift center toward the active slice frequency
        // so it stays visible (mirrors Flex SDR behavior)
        if (factor < 1.0) {
            const double vfoMhz = s->frequency();
            const double halfBw = newBw / 2.0;
            if (vfoMhz > newCenter + halfBw)
                newCenter = vfoMhz - halfBw * 0.8;
            else if (vfoMhz < newCenter - halfBw)
                newCenter = vfoMhz + halfBw * 0.8;
        }

        sw->setFrequencyRange(newCenter, newBw);
        // Keep keyboard zoom on the same combined pan-range path as trackpad /
        // on-screen zoom so mode/frequency jumps do not reintroduce stale
        // center-versus-bandwidth transitions.
        applyPanRangeRequest(s->panId(), newCenter, newBw, "keyboard-pan-zoom");
    };

    // ── DSP ─────────────────────────────────────────────────────────────
    m_shortcutManager.registerAction("nb_toggle", "NB Toggle", "DSP",
        QKeySequence(), [this]() {
            auto* s = activeSlice();
            if (s) s->setNb(!s->nbOn());
        });
    m_shortcutManager.registerAction("nr_cycle", "NR Cycle (Off/NR/NR2/NR4/DFNR)", "DSP",
        QKeySequence(), [this]() {
            auto* s = activeSlice();
            if (!s) return;
            if (m_audio->dfnrEnabled()) {
                // DFNR → off
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setDfnrEnabled(false); });
            } else if (m_audio->nr4Enabled()) {
                // NR4 → DFNR
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr4Enabled(false); });
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setDfnrEnabled(true); });
            } else if (m_audio->nr2Enabled()) {
                // NR2 → NR4
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr2Enabled(false); });
                QMetaObject::invokeMethod(m_audio, [this]() { m_audio->setNr4Enabled(true); });
            } else if (s->nrOn()) {
                // NR → NR2
                s->setNr(false);
                enableNr2WithWisdom();
            } else {
                // off → NR
                s->setNr(true);
            }
        });
    m_shortcutManager.registerAction("anf_toggle", "ANF Toggle", "DSP",
        QKeySequence(), [this]() {
            auto* s = activeSlice();
            if (s) s->setAnf(!s->anfOn());
        });

    // ── AGC ─────────────────────────────────────────────────────────────
    m_shortcutManager.registerAction("agc_cycle", "AGC Mode Cycle", "AGC",
        QKeySequence(), [this]() {
            auto* s = activeSlice();
            if (!s) return;
            static const char* modes[] = {"off", "slow", "med", "fast"};
            QString cur = s->agcMode().toLower();
            int idx = 0;
            for (int i = 0; i < 4; ++i)
                if (cur == modes[i]) { idx = i; break; }
            s->setAgcMode(modes[(idx + 1) % 4]);
        });

    // ── Display ─────────────────────────────────────────────────────────
    m_shortcutManager.registerAction("band_zoom", "Band Zoom", "Display",
        QKeySequence(), [this]() {
            if (!m_radioModel.isConnected()) return;
            auto* s = activeSlice();
            if (s) m_radioModel.sendCommand(
                QString("slice set %1 band_zoom=1").arg(s->sliceId()));
        });
    m_shortcutManager.registerAction("segment_zoom", "Segment Zoom", "Display",
        QKeySequence(), [this]() {
            if (!m_radioModel.isConnected()) return;
            auto* s = activeSlice();
            if (s) m_radioModel.sendCommand(
                QString("slice set %1 segment_zoom=1").arg(s->sliceId()));
        });
    m_shortcutManager.registerAction("pan_zoom_in", "Panadapter Zoom In", "Display",
        QKeySequence(Qt::Key_Equal), [zoomActivePanadapter]() { zoomActivePanadapter(1.0 / kPanZoomFactor); });
    m_shortcutManager.registerAction("pan_zoom_out", "Panadapter Zoom Out", "Display",
        QKeySequence(Qt::Key_Minus), [zoomActivePanadapter]() { zoomActivePanadapter(kPanZoomFactor); });
    m_shortcutManager.registerAction("open_memories", "Open Memories Dialog", "Display",
        QKeySequence(Qt::Key_Slash), [this]() { showMemoryDialog(); });

    // ── RIT/XIT ─────────────────────────────────────────────────────────
    m_shortcutManager.registerAction("rit_toggle", "RIT Toggle", "RIT/XIT",
        QKeySequence(), [this]() {
            auto* s = activeSlice();
            if (s) s->setRit(!s->ritOn(), s->ritFreq());
        });
    m_shortcutManager.registerAction("xit_toggle", "XIT Toggle", "RIT/XIT",
        QKeySequence(), [this]() {
            auto* s = activeSlice();
            if (s) s->setXit(!s->xitOn(), s->xitFreq());
        });

    // ── Load user bindings and create QShortcuts ────────────────────────
    m_shortcutManager.loadBindings();
    s_keyboardShortcutsEnabled = m_keyboardShortcutsEnabled;
    m_shortcutManager.rebuildShortcuts(this, shortcutGuard);

    m_sliderShortcutLeaseTimer.setSingleShot(true);
    connect(&m_sliderShortcutLeaseTimer, &QTimer::timeout, this,
            [this]() { releaseSliderShortcutLease(true); });

    // Temporarily yield global shortcuts while a clicked slider is being
    // nudged, then return keyboard control to the operator shortcuts.
    connect(qApp, &QApplication::focusChanged, this,
            [this](QWidget* /*old*/, QWidget* now) {
        if (auto* slider = qobject_cast<QAbstractSlider*>(now))
            beginSliderShortcutLease(slider);
        else
            releaseSliderShortcutLease(false);
    });
}

void MainWindow::showNr2ParamPopup(const QPoint& globalPos)
{
    auto& s = AppSettings::instance();
    auto* popup = new DspParamPopup(this);

    popup->addSlider("Reduction (dB)", 10, 300,
        static_cast<int>(s.value("NR2GainMax", "1.50").toFloat() * 100),
        [](int v) { return QString::number(v / 100.0f, 'f', 2); },
        [this](int v) {
            float val = v / 100.0f;
            auto& s = AppSettings::instance();
            s.setValue("NR2GainMax", QString::number(val, 'f', 2));
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, val]() { m_audio->setNr2GainMax(val); });
        });

    popup->addSlider("Smoothing",  50, 98,
        static_cast<int>(s.value("NR2GainSmooth", "0.85").toFloat() * 100),
        [](int v) { return QString::number(v / 100.0f, 'f', 2); },
        [this](int v) {
            float val = v / 100.0f;
            auto& s = AppSettings::instance();
            s.setValue("NR2GainSmooth", QString::number(val, 'f', 2));
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, val]() { m_audio->setNr2GainSmooth(val); });
        });

    popup->addSlider("Voice Threshold", 1, 50,
        static_cast<int>(s.value("NR2Qspp", "0.20").toFloat() * 100),
        [](int v) { return QString::number(v / 100.0f, 'f', 2); },
        [this](int v) {
            float val = v / 100.0f;
            auto& s = AppSettings::instance();
            s.setValue("NR2Qspp", QString::number(val, 'f', 2));
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, val]() { m_audio->setNr2Qspp(val); });
        });

    popup->addCheckbox("AE Filter",
        s.value("NR2AeFilter", "True").toString() == "True",
        [this](bool on) {
            auto& s = AppSettings::instance();
            s.setValue("NR2AeFilter", on ? "True" : "False");
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr2AeFilter(on); });
        });

    popup->finalize(
        [this]() {
            // Open AetherDSP Settings dialog
            if (m_dspDialog) {
                m_dspDialog->raise();
                m_dspDialog->activateWindow();
                return;
            }
            auto* dlg = new AetherDspDialog(m_audio, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &AetherDspDialog::nr2GainMaxChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2GainMax(v); });
            });
            connect(dlg, &AetherDspDialog::nr2GainSmoothChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2GainSmooth(v); });
            });
            connect(dlg, &AetherDspDialog::nr2QsppChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2Qspp(v); });
            });
            connect(dlg, &AetherDspDialog::nr2GainMethodChanged, this, [this](int m) {
                QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr2GainMethod(m); });
            });
            connect(dlg, &AetherDspDialog::nr2NpeMethodChanged, this, [this](int m) {
                QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr2NpeMethod(m); });
            });
            connect(dlg, &AetherDspDialog::nr2AeFilterChanged, this, [this](bool on) {
                QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr2AeFilter(on); });
            });
            // Wire NR4 parameter signals
            connect(dlg, &AetherDspDialog::nr4ReductionChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4ReductionAmount(v); });
            });
            connect(dlg, &AetherDspDialog::nr4SmoothingChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4SmoothingFactor(v); });
            });
            connect(dlg, &AetherDspDialog::nr4WhiteningChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4WhiteningFactor(v); });
            });
            connect(dlg, &AetherDspDialog::nr4AdaptiveNoiseChanged, this, [this](bool on) {
                QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr4AdaptiveNoise(on); });
            });
            connect(dlg, &AetherDspDialog::nr4NoiseMethodChanged, this, [this](int m) {
                QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr4NoiseEstimationMethod(m); });
            });
            connect(dlg, &AetherDspDialog::nr4MaskingDepthChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4MaskingDepth(v); });
            });
            connect(dlg, &AetherDspDialog::nr4SuppressionChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4SuppressionStrength(v); });
            });
            connect(dlg, &AetherDspDialog::dfnrAttenLimitChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrAttenLimit(v); });
            });
            connect(dlg, &AetherDspDialog::dfnrPostFilterBetaChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrPostFilterBeta(v); });
            });
            // Wire MNR enable / strength signals
            connect(dlg, &AetherDspDialog::mnrEnabledChanged, this, [this](bool on) {
                QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setMnrEnabled(on); });
            });
            connect(dlg, &AetherDspDialog::mnrStrengthChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setMnrStrength(v); });
            });
            m_dspDialog = dlg;
            dlg->show();
        },
        nullptr  // Reset handled by individual control resetters
    );

    popup->showAt(globalPos);
}

void MainWindow::showNr4ParamPopup(const QPoint& globalPos)
{
    auto& s = AppSettings::instance();
    auto* popup = new DspParamPopup(this);

    popup->addSlider("Reduction (dB)", 0, 400,
        static_cast<int>(s.value("NR4ReductionAmount", "10.0").toFloat() * 10),
        [](int v) { return QString::number(v / 10.0f, 'f', 1); },
        [this](int v) {
            float val = v / 10.0f;
            auto& s = AppSettings::instance();
            s.setValue("NR4ReductionAmount", QString::number(val, 'f', 1));
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, val]() { m_audio->setNr4ReductionAmount(val); });
        });

    popup->addSlider("Smoothing (%)", 0, 100,
        static_cast<int>(s.value("NR4SmoothingFactor", "0.0").toFloat()),
        [](int v) { return QString::number(v); },
        [this](int v) {
            auto& s = AppSettings::instance();
            s.setValue("NR4SmoothingFactor", QString::number(static_cast<float>(v), 'f', 1));
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4SmoothingFactor(static_cast<float>(v)); });
        });

    popup->addSlider("Masking Depth", 0, 100,
        static_cast<int>(s.value("NR4MaskingDepth", "0.50").toFloat() * 100),
        [](int v) { return QString::number(v / 100.0f, 'f', 2); },
        [this](int v) {
            float val = v / 100.0f;
            auto& s = AppSettings::instance();
            s.setValue("NR4MaskingDepth", QString::number(val, 'f', 2));
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, val]() { m_audio->setNr4MaskingDepth(val); });
        });

    popup->addSlider("Suppression", 0, 100,
        static_cast<int>(s.value("NR4SuppressionStrength", "0.50").toFloat() * 100),
        [](int v) { return QString::number(v / 100.0f, 'f', 2); },
        [this](int v) {
            float val = v / 100.0f;
            auto& s = AppSettings::instance();
            s.setValue("NR4SuppressionStrength", QString::number(val, 'f', 2));
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, val]() { m_audio->setNr4SuppressionStrength(val); });
        });

    popup->addCheckbox("Adaptive Noise",
        s.value("NR4AdaptiveNoise", "True").toString() == "True",
        [this](bool on) {
            auto& s = AppSettings::instance();
            s.setValue("NR4AdaptiveNoise", on ? "True" : "False");
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr4AdaptiveNoise(on); });
        });

    popup->finalize(
        [this]() {
            // Open AetherDSP Settings dialog (NR4 tab)
            if (m_dspDialog) {
                m_dspDialog->raise();
                m_dspDialog->activateWindow();
                return;
            }
            auto* dlg = new AetherDspDialog(m_audio, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &AetherDspDialog::nr2GainMaxChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2GainMax(v); });
            });
            connect(dlg, &AetherDspDialog::nr2GainSmoothChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2GainSmooth(v); });
            });
            connect(dlg, &AetherDspDialog::nr2QsppChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2Qspp(v); });
            });
            connect(dlg, &AetherDspDialog::nr2GainMethodChanged, this, [this](int m) {
                QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr2GainMethod(m); });
            });
            connect(dlg, &AetherDspDialog::nr2NpeMethodChanged, this, [this](int m) {
                QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr2NpeMethod(m); });
            });
            connect(dlg, &AetherDspDialog::nr2AeFilterChanged, this, [this](bool on) {
                QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr2AeFilter(on); });
            });
            connect(dlg, &AetherDspDialog::nr4ReductionChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4ReductionAmount(v); });
            });
            connect(dlg, &AetherDspDialog::nr4SmoothingChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4SmoothingFactor(v); });
            });
            connect(dlg, &AetherDspDialog::nr4WhiteningChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4WhiteningFactor(v); });
            });
            connect(dlg, &AetherDspDialog::nr4AdaptiveNoiseChanged, this, [this](bool on) {
                QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr4AdaptiveNoise(on); });
            });
            connect(dlg, &AetherDspDialog::nr4NoiseMethodChanged, this, [this](int m) {
                QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr4NoiseEstimationMethod(m); });
            });
            connect(dlg, &AetherDspDialog::nr4MaskingDepthChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4MaskingDepth(v); });
            });
            connect(dlg, &AetherDspDialog::nr4SuppressionChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4SuppressionStrength(v); });
            });
            connect(dlg, &AetherDspDialog::dfnrAttenLimitChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrAttenLimit(v); });
            });
            connect(dlg, &AetherDspDialog::dfnrPostFilterBetaChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrPostFilterBeta(v); });
            });
            // Wire MNR enable / strength signals
            connect(dlg, &AetherDspDialog::mnrEnabledChanged, this, [this](bool on) {
                QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setMnrEnabled(on); });
            });
            connect(dlg, &AetherDspDialog::mnrStrengthChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setMnrStrength(v); });
            });
            m_dspDialog = dlg;
            dlg->show();
        },
        nullptr  // Reset handled by individual control resetters
    );

    popup->showAt(globalPos);
}

void MainWindow::showDfnrParamPopup(const QPoint& globalPos)
{
    auto& s = AppSettings::instance();
    auto* popup = new DspParamPopup(this);

    popup->addSlider("Attenuation Limit (dB)", 0, 100,
        static_cast<int>(s.value("DfnrAttenLimit", "100").toFloat()),
        [](int v) { return QString::number(v); },
        [this](int v) {
            float db = static_cast<float>(v);
            auto& s = AppSettings::instance();
            s.setValue("DfnrAttenLimit", QString::number(db, 'f', 0));
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, db]() { m_audio->setDfnrAttenLimit(db); });
        });

    popup->addSlider("Post-Filter Beta", 0, 30,
        static_cast<int>(s.value("DfnrPostFilterBeta", "0.0").toFloat() * 100),
        [](int v) { return QString::number(v / 100.0f, 'f', 2); },
        [this](int v) {
            float beta = v / 100.0f;
            auto& s = AppSettings::instance();
            s.setValue("DfnrPostFilterBeta", QString::number(beta, 'f', 2));
            s.save();
            QMetaObject::invokeMethod(m_audio, [this, beta]() { m_audio->setDfnrPostFilterBeta(beta); });
        });

    popup->finalize(
        [this]() {
            // Open AetherDSP Settings dialog (DFNR tab)
            if (m_dspDialog) {
                m_dspDialog->raise();
                m_dspDialog->activateWindow();
                return;
            }
            auto* dlg = new AetherDspDialog(m_audio, this);
            dlg->setAttribute(Qt::WA_DeleteOnClose);
            connect(dlg, &AetherDspDialog::dfnrAttenLimitChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrAttenLimit(v); });
            });
            connect(dlg, &AetherDspDialog::dfnrPostFilterBetaChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrPostFilterBeta(v); });
            });
            // Wire NR2/NR4 signals too (shared dialog)
            connect(dlg, &AetherDspDialog::nr2GainMaxChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2GainMax(v); });
            });
            connect(dlg, &AetherDspDialog::nr2GainSmoothChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2GainSmooth(v); });
            });
            connect(dlg, &AetherDspDialog::nr2QsppChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2Qspp(v); });
            });
            connect(dlg, &AetherDspDialog::nr2GainMethodChanged, this, [this](int m) {
                QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr2GainMethod(m); });
            });
            connect(dlg, &AetherDspDialog::nr2NpeMethodChanged, this, [this](int m) {
                QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr2NpeMethod(m); });
            });
            connect(dlg, &AetherDspDialog::nr2AeFilterChanged, this, [this](bool on) {
                QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr2AeFilter(on); });
            });
            connect(dlg, &AetherDspDialog::nr4ReductionChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4ReductionAmount(v); });
            });
            connect(dlg, &AetherDspDialog::nr4SmoothingChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4SmoothingFactor(v); });
            });
            connect(dlg, &AetherDspDialog::nr4WhiteningChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4WhiteningFactor(v); });
            });
            connect(dlg, &AetherDspDialog::nr4AdaptiveNoiseChanged, this, [this](bool on) {
                QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr4AdaptiveNoise(on); });
            });
            connect(dlg, &AetherDspDialog::nr4NoiseMethodChanged, this, [this](int m) {
                QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr4NoiseEstimationMethod(m); });
            });
            connect(dlg, &AetherDspDialog::nr4MaskingDepthChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4MaskingDepth(v); });
            });
            connect(dlg, &AetherDspDialog::nr4SuppressionChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4SuppressionStrength(v); });
            });
            // Wire MNR enable / strength signals
            connect(dlg, &AetherDspDialog::mnrEnabledChanged, this, [this](bool on) {
                QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setMnrEnabled(on); });
            });
            connect(dlg, &AetherDspDialog::mnrStrengthChanged, this, [this](float v) {
                QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setMnrStrength(v); });
            });
            m_dspDialog = dlg;
            dlg->show();
        },
        nullptr
    );

    popup->showAt(globalPos);
}

void MainWindow::showMnrSettings()
{
    if (m_dspDialog) {
        m_dspDialog->raise();
        m_dspDialog->activateWindow();
        if (auto* d = qobject_cast<AetherDspDialog*>(m_dspDialog))
            d->selectTab("MNR");
        return;
    }
    auto* dlg = new AetherDspDialog(m_audio, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &AetherDspDialog::mnrEnabledChanged, this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setMnrEnabled(on); });
    });
    connect(dlg, &AetherDspDialog::mnrStrengthChanged, this, [this](float v) {
        QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setMnrStrength(v); });
    });
    connect(dlg, &AetherDspDialog::nr2GainMaxChanged, this, [this](float v) {
        QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2GainMax(v); });
    });
    connect(dlg, &AetherDspDialog::nr2GainSmoothChanged, this, [this](float v) {
        QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2GainSmooth(v); });
    });
    connect(dlg, &AetherDspDialog::nr2QsppChanged, this, [this](float v) {
        QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2Qspp(v); });
    });
    connect(dlg, &AetherDspDialog::nr2GainMethodChanged, this, [this](int m) {
        QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr2GainMethod(m); });
    });
    connect(dlg, &AetherDspDialog::nr2NpeMethodChanged, this, [this](int m) {
        QMetaObject::invokeMethod(m_audio, [this, m]() { m_audio->setNr2NpeMethod(m); });
    });
    connect(dlg, &AetherDspDialog::nr2AeFilterChanged, this, [this](bool on) {
        QMetaObject::invokeMethod(m_audio, [this, on]() { m_audio->setNr2AeFilter(on); });
    });
    connect(dlg, &AetherDspDialog::dfnrAttenLimitChanged, this, [this](float v) {
        QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrAttenLimit(v); });
    });
    connect(dlg, &AetherDspDialog::dfnrPostFilterBetaChanged, this, [this](float v) {
        QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrPostFilterBeta(v); });
    });
    m_dspDialog = dlg;
    dlg->selectTab("MNR");
    dlg->show();
}

void MainWindow::applyPanLayout(const QString& layoutId)
{
    if (!m_radioModel.isConnected()) return;

    static const QMap<QString, int> kPanCounts = {
        {"1", 1}, {"2v", 2}, {"2h", 2}, {"2h1", 3}, {"12h", 3}, {"3v", 3},
        {"2x2", 4}, {"4v", 4}, {"3h2", 5}, {"2x3", 6}, {"4h3", 7}, {"2x4", 8}
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
    // Center and bandwidth are radio-authoritative — don't capture.
    if (auto* sw = spectrum()) {
        snap.minDbm          = sw->refLevel() - sw->dynamicRange();
        snap.maxDbm          = sw->refLevel();
        snap.spectrumFrac    = sw->spectrumFrac();
        snap.rfGain          = sw->rfGainValue();
        snap.wnbOn           = sw->wnbActive();
    }
    return snap;
}

void MainWindow::restoreBandState(const BandSnapshot& snap)
{
    m_updatingFromModel = true;
    if (auto* s = activeSlice()) {
        s->setMode(snap.mode);
        TuneCenteringResult result;
        if (auto* pan = m_radioModel.panadapter(s->panId())) {
            result.oldCenterMhz = pan->centerMhz();
            result.bandwidthMhz = pan->bandwidthMhz();
        }
        result.newCenterMhz = snap.frequencyMhz;
        result.followRevealTriggered = true;
        result.hardCenterUsed = true;
        logTunePolicyDecision("restore-band-state", TuneIntent::AbsoluteJump,
                              s->frequency(), snap.frequencyMhz, result);
        s->tuneAndRecenter(snap.frequencyMhz);
        if (!snap.rxAntenna.isEmpty())
            s->setRxAntenna(snap.rxAntenna);
        s->setFilterWidth(snap.filterLow, snap.filterHigh);
        if (!snap.agcMode.isEmpty())
            s->setAgcMode(snap.agcMode);
        s->setAgcThreshold(snap.agcThreshold);
    }
    if (auto* pan = m_radioModel.activePanadapter()) {
        // Don't push center or bandwidth — slice tune recenters the pan and
        // the radio determines bandwidth. Pushing stale saved values causes
        // FFT/waterfall misalignment during the transition (#279, #291).
        // Only restore dBm scale (client-side display preference).
        m_radioModel.sendCommand(
            QString("display pan set %1 min_dbm=%2 max_dbm=%3").arg(pan->panId())
                .arg(static_cast<double>(snap.minDbm), 0, 'f', 2)
                .arg(static_cast<double>(snap.maxDbm), 0, 'f', 2));
    }
    m_radioModel.setPanRfGain(snap.rfGain);
    m_radioModel.setPanWnb(snap.wnbOn);
    if (auto* sw = spectrum()) {
        sw->setSpectrumFrac(snap.spectrumFrac);
        sw->setRfGain(snap.rfGain);
        sw->setWnbActive(snap.wnbOn);
    }
    m_updatingFromModel = false;
}

// ─── GUI control handlers ─────────────────────────────────────────────────────

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

    // RADE needs to be the TX slice so it can transmit modem audio.
    // Move TX badge to the RADE slice automatically.
    if (!s->isTxSlice())
        s->setTxSlice(true);

    // Set radio mode to DIGU/DIGL (passthrough for OFDM modem).
    // Use band convention from BandDefs to pick sideband — 60m is USB
    // despite being below 10 MHz (#875).
    double freqMhz = s->frequency();
    QString mode = "DIGU";
    for (const auto& band : AetherSDR::kBands) {
        if (freqMhz >= band.lowMhz && freqMhz <= band.highMhz) {
            mode = (QString(band.defaultMode) == "LSB") ? "DIGL" : "DIGU";
            break;
        }
    }
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

    // Only route mic→RADE when the RADE slice IS the TX slice.
    // If another slice is TX (e.g. USB voice), leave its audio path alone.
    m_audio->setRadeMode(s->isTxSlice());

    // TX path: mic -> RADEEngine (worker) -> sendModemTxAudio (main)
    connect(m_audio, &AudioEngine::txRawPcmReady,
            m_radeEngine, &RADEEngine::feedTxAudio, Qt::QueuedConnection);
    connect(m_radeEngine, &RADEEngine::txModemReady,
            m_audio, &AudioEngine::sendModemTxAudio, Qt::QueuedConnection);

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
            m_audio, &AudioEngine::feedDecodedSpeech, Qt::QueuedConnection);

    // On platforms without a DAX audio bridge (Windows, Linux without PipeWire),
    // startDax() is not compiled in so no dax_rx stream is ever created. RADE
    // needs those VITA-49 IF audio packets regardless of whether a DAX bridge
    // is in use — create the stream directly so feedRxAudio() gets called.
    // If TCI already created a stream for this channel, reuse it rather than
    // creating a duplicate (which would cause RADEEngine to receive double audio).
#if !defined(Q_OS_MAC) && !defined(HAVE_PIPEWIRE)
    {
        SliceModel* radeSlice = m_radioModel.slice(sliceId);
        int daxCh = radeSlice ? radeSlice->daxChannel() : 0;
        if (daxCh >= 1 && daxCh <= 4) {
            quint32 existing = m_radioModel.panStream()->daxStreamIdForChannel(daxCh);
            if (existing) {
                // TCI or another path already registered a stream — RADE rides it.
                qDebug() << "MainWindow: RADE reusing existing dax_rx ch" << daxCh
                         << "stream" << Qt::hex << existing;
            } else {
                m_radeDaxStreamConn = connect(
                    &m_radioModel, &RadioModel::statusReceived,
                    this, [this, daxCh](const QString& obj, const QMap<QString,QString>& kvs) {
                        if (!obj.startsWith("stream ")) return;
                        if (kvs.value("type") != "dax_rx") return;
                        quint32 streamId = obj.mid(7).toUInt(nullptr, 16);
                        int ch = kvs.value("dax_channel").toInt();
                        if (streamId && ch == daxCh) {
                            m_radioModel.panStream()->registerDaxStream(streamId, ch);
                            m_radeDaxStreamId = streamId;
                            disconnect(m_radeDaxStreamConn);
                            qDebug() << "MainWindow: RADE registered dax_rx ch" << ch
                                     << "stream" << Qt::hex << streamId;
                        }
                    });
                m_radioModel.sendCommand(
                    QString("stream create type=dax_rx dax_channel=%1").arg(daxCh));
            }
        } else {
            qWarning() << "MainWindow: RADE slice" << sliceId
                       << "has no DAX channel assigned — RX audio will not flow";
        }
    }
#endif

    // Start mic capture if not already running
    if (!m_audio->isTxStreaming()) {
        audioStartTx(m_radioModel.radioAddress(), 4991);
    }

    // RADE status indicator in VFO widget.
    // Use vfoWidget(sliceId) — the no-arg alias (m_vfoWidget) may be null
    // if setActiveVfoWidget() hasn't been called yet for this slice.
    if (auto* sw = spectrum()) {
        if (auto* vfo = sw->vfoWidget(sliceId)) {
            vfo->setRadeActive(true);
            // Show initial unsynchronised state immediately — syncChanged only fires
            // from feedRxAudio() which requires DAX audio to be flowing first.
            vfo->setRadeSynced(false);
            connect(m_radeEngine, &RADEEngine::syncChanged,
                    vfo, &VfoWidget::setRadeSynced);
            connect(m_radeEngine, &RADEEngine::snrChanged,
                    vfo, &VfoWidget::setRadeSnr);
            connect(m_radeEngine, &RADEEngine::freqOffsetChanged,
                    vfo, &VfoWidget::setRadeFreqOffset);
        }
    }

    qInfo() << "MainWindow: RADE mode activated on slice" << sliceId;
}

void MainWindow::deactivateRADE()
{
    // Restore audio mute state on the RADE slice
    if (m_radeSliceId >= 0) {
        if (auto* s = m_radioModel.slice(m_radeSliceId))
            s->setAudioMute(m_radePrevMute);
        // Clear RADE status label before resetting sliceId
        if (auto* sw = spectrum()) {
            if (auto* vfo = sw->vfoWidget(m_radeSliceId))
                vfo->setRadeActive(false);
        }
        m_radeSliceId = -1;
    }

    m_audio->setRadeMode(false);
    m_audio->clearTxAccumulators();  // flush stale RADE modem data

    if (m_radeEngine) {
        disconnect(m_audio, &AudioEngine::txRawPcmReady,
                   m_radeEngine, nullptr);
        disconnect(m_radeEngine, &RADEEngine::txModemReady,
                   m_audio, nullptr);
        disconnect(m_radioModel.panStream(), &PanadapterStream::daxAudioReady,
                   m_radeEngine, nullptr);
        disconnect(m_radeEngine, &RADEEngine::rxSpeechReady,
                   m_audio, nullptr);
#if !defined(Q_OS_MAC) && !defined(HAVE_PIPEWIRE)
        if (m_radeDaxStreamId) {
            // Only send stream remove if TCI has no active clients. If TCI is
            // connected it may have borrowed this stream — removing it would
            // silently kill TCI audio. Leave TCI responsible for cleanup in
            // that case. TODO: replace with proper ref-counting in PanadapterStream
            // so any creator/borrower can safely release independently (#stream-lifecycle).
            bool tciActive = m_tciServer && m_tciServer->clientCount() > 0;
            if (!tciActive) {
                m_radioModel.panStream()->unregisterDaxStream(m_radeDaxStreamId);
                if (m_radioModel.isConnected()) {
                    m_radioModel.sendCommand(
                        QString("stream remove 0x%1")
                            .arg(m_radeDaxStreamId, 8, 16, QChar('0')));
                }
            }
            m_radeDaxStreamId = 0;
        }
        disconnect(m_radeDaxStreamConn);
#endif
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
bool MainWindow::startDax()
{
    if (m_daxBridge) return true;

#ifdef Q_OS_MAC
    // Only start if the macOS HAL driver bundle is installed.
    if (!macDaxDriverInstalled()) {
        qWarning() << "MainWindow: DAX HAL plugin not installed";
        QMessageBox::warning(this, "DAX Audio Driver Missing",
            "The AetherSDR DAX audio driver is not installed on this Mac.\n\n"
            "Install the DAX Virtual Audio Driver from the AetherSDR DMG package, "
            "then enable DAX again.");
        return false;
    }
#endif

    m_daxBridge = new DaxBridge(this);
    if (!m_daxBridge->open()) {
        qWarning() << "MainWindow: failed to open DAX audio bridge";
        QMessageBox::warning(this, "DAX Audio Bridge Error",
            "AetherSDR could not open the DAX audio bridge.\n\n"
            "If the DAX driver was just installed, quit and relaunch AetherSDR and try again.");
        delete m_daxBridge;
        m_daxBridge = nullptr;
        return false;
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

    // Create DAX RX streams only for channels with slices assigned.
    // FlexLib creates streams on demand, not all 4 unconditionally.
    // Creating unused streams causes the radio to round-robin audio
    // across all of them, starving the active channels.
    for (auto* s : m_radioModel.slices()) {
        int ch = s->daxChannel();
        if (ch >= 1 && ch <= 4) {
            m_radioModel.sendCommand(
                QString("stream create type=dax_rx dax_channel=%1").arg(ch));
        }
    }

    // Wire DAX RX: PanadapterStream routes registered DAX streams here
    connect(m_radioModel.panStream(), &PanadapterStream::daxAudioReady,
            m_daxBridge, &DaxBridge::feedDaxAudio);

    // ── DAX IQ stream status + VITA routing ─────────────────────────────
    connect(&m_radioModel, &RadioModel::statusReceived,
            this, [this](const QString& obj, const QMap<QString,QString>& kvs) {
        if (!obj.startsWith("stream ")) return;
        QString type = kvs.value("type");
        if (type == "dax_iq") {
            qDebug() << "DAX IQ STREAM STATUS:" << obj << "keys=" << kvs.keys()
                     << "ch=" << kvs.value("daxiq_channel") << "ip=" << kvs.value("ip");
            quint32 streamId = obj.mid(7).toUInt(nullptr, 0);
            if (kvs.contains("removed")) {
                m_radioModel.panStream()->unregisterIqStream(streamId);
                m_radioModel.daxIqModel().handleStreamRemoved(streamId);
            } else {
                m_radioModel.daxIqModel().applyStreamStatus(streamId, kvs);
                int ch = kvs.value("daxiq_channel").toInt();
                if (streamId && ch >= 1 && ch <= 4)
                    m_radioModel.panStream()->registerIqStream(streamId, ch);
            }
        }
    });

    // Route IQ VITA-49 packets to DaxIqModel worker thread
    connect(m_radioModel.panStream(), &PanadapterStream::iqDataReady,
            &m_radioModel.daxIqModel(), &DaxIqModel::feedRawIqPacket);

    // Wire DAX IQ level meters to DAX IQ applet
    connect(&m_radioModel.daxIqModel(), &DaxIqModel::iqLevelReady,
            m_appletPanel->daxIqApplet(), &DaxIqApplet::setDaxIqLevel);

    // Wire DAX IQ enable/disable/rate from DAX IQ applet to DaxIqModel
    connect(m_appletPanel->daxIqApplet(), &DaxIqApplet::iqEnableRequested,
            &m_radioModel.daxIqModel(), &DaxIqModel::createStream);
    connect(m_appletPanel->daxIqApplet(), &DaxIqApplet::iqDisableRequested,
            &m_radioModel.daxIqModel(), &DaxIqModel::removeStream);
    connect(m_appletPanel->daxIqApplet(), &DaxIqApplet::iqRateChanged,
            &m_radioModel.daxIqModel(), &DaxIqModel::setSampleRate);

    // Wire DAX level meters
    connect(m_daxBridge, &DaxBridge::daxRxLevel,
            m_appletPanel->daxApplet(), &DaxApplet::setDaxRxLevel);
    connect(m_daxBridge, &DaxBridge::daxTxLevel,
            m_appletPanel->daxApplet(), &DaxApplet::setDaxTxLevel);

    // Wire DAX gain sliders
    connect(m_appletPanel->daxApplet(), &DaxApplet::daxRxGainChanged,
            m_daxBridge, &DaxBridge::setChannelGain);
    connect(m_appletPanel->daxApplet(), &DaxApplet::daxTxGainChanged,
            m_daxBridge, &DaxBridge::setTxGain);

    // Apply saved gains to the bridge
    auto& ss = AppSettings::instance();
    for (int i = 1; i <= 4; ++i)
        m_daxBridge->setChannelGain(i, ss.value(QStringLiteral("DaxRxGain%1").arg(i), "0.5").toString().toFloat());
    m_daxBridge->setTxGain(ss.value("DaxTxGain", "0.5").toString().toFloat());

    // Wire DAX TX: apps → bridge → AudioEngine → VITA-49.
    // AudioEngine chooses packet format/routing based on DaxTxLowLatency.
    connect(m_daxBridge, &DaxBridge::txAudioReady,
            this, [this](const QByteArray& pcm) {
        if (m_audio->isRadeMode()) return;
        if (!m_audio->isDaxTxMode()) return;
        QMetaObject::invokeMethod(m_audio, [this, pcm]() { m_audio->feedDaxTxAudio(pcm); });
    });

    // Save current mic selection before forcing PC audio source.
    m_savedMicSelection = m_radioModel.transmitModel().micSelection();

    // Default to the radio-native DAX route (dax=1, int16 mono).  RADE
    // mode overrides this to the low-latency route via setRadeMode()
    // when the user enters RADE — see AudioEngine::setRadeMode().
    m_audio->setDaxTxUseRadioRoute(true);
    m_radioModel.sendCommand("transmit set mic_selection=PC");
    // Don't force dax=1 here — radio-side DAX flag follows mode changes
    // via updateDaxTxMode(). Bridge up ≠ DAX TX active. (#534)

    qInfo() << "MainWindow: starting DAX audio bridge";
    return true;
}

void MainWindow::stopDax()
{
    if (!m_daxBridge) return;

    m_audio->setDaxTxMode(false);
    m_audio->clearTxAccumulators();

    disconnect(m_radioModel.panStream(), &PanadapterStream::daxAudioReady,
               m_daxBridge, nullptr);
    disconnect(m_daxBridge, &DaxBridge::txAudioReady,
               this, nullptr);

    // Remove DAX RX streams from radio and unregister from PanadapterStream
    const auto daxIds = m_radioModel.panStream()->daxStreamIds();
    for (quint32 id : daxIds) {
        m_radioModel.sendCommand(QString("stream remove 0x%1").arg(id, 0, 16));
        m_radioModel.panStream()->unregisterDaxStream(id);
    }

    // Restore original mic selection
    if (!m_savedMicSelection.isEmpty() && m_savedMicSelection != "PC")
        m_radioModel.sendCommand(QString("transmit set mic_selection=%1").arg(m_savedMicSelection));

    m_daxBridge->close();
    delete m_daxBridge;
    m_daxBridge = nullptr;
    qInfo() << "MainWindow: stopping DAX audio bridge";
}
#endif

#ifdef HAVE_MIDI
void MainWindow::registerMidiParams()
{
    using P = MidiParamType;
    // Setters/getters stored on MainWindow for main-thread dispatch (#502).
    // MidiControlManager gets metadata only (no lambdas that capture main-thread objects).
    auto reg = [this](const char* id, const char* name, const char* cat,
                      MidiParamType type, float lo, float hi,
                      std::function<void(float)> setter,
                      std::function<float()> getter = {}) {
        m_midiSetters[id] = setter;
        if (getter) m_midiGetters[id] = getter;
        m_midiControl->registerParam({id, name, cat, type, lo, hi, std::move(setter), std::move(getter)});
    };

    // ── RX ──────────────────────────────────────────────────────────────
    reg("rx.afGain", "AF Gain", "RX", P::Slider, 0, 200,
        [this](float v) { if (auto* s = activeSlice()) s->setAudioGain(v); },
        [this]() -> float { auto* s = activeSlice(); return s ? s->audioGain() : 0; });

    reg("rx.squelch", "Squelch Level", "RX", P::Slider, 0, 100,
        [this](float v) { if (auto* s = activeSlice()) s->setSquelch(s->squelchOn(), static_cast<int>(v)); },
        [this]() -> float { auto* s = activeSlice(); return s ? s->squelchLevel() : 0; });

    reg("rx.agcThreshold", "AGC Threshold", "RX", P::Slider, 0, 100,
        [this](float v) { if (auto* s = activeSlice()) s->setAgcThreshold(static_cast<int>(v)); },
        [this]() -> float { auto* s = activeSlice(); return s ? s->agcThreshold() : 0; });

    reg("rx.audioPan", "Audio Pan", "RX", P::Slider, 0, 100,
        [this](float v) { if (auto* s = activeSlice()) s->setAudioPan(static_cast<int>(v)); },
        [this]() -> float { auto* s = activeSlice(); return s ? s->audioPan() : 50; });

    reg("rx.nbEnable", "Noise Blanker", "RX", P::Toggle, 0, 1,
        [this](float v) { if (auto* s = activeSlice()) s->setNb(v > 0.5f); },
        [this]() -> float { auto* s = activeSlice(); return s && s->nbOn() ? 1 : 0; });

    reg("rx.nrEnable", "Noise Reduction", "RX", P::Toggle, 0, 1,
        [this](float v) { if (auto* s = activeSlice()) s->setNr(v > 0.5f); },
        [this]() -> float { auto* s = activeSlice(); return s && s->nrOn() ? 1 : 0; });

    reg("rx.anfEnable", "Auto Notch", "RX", P::Toggle, 0, 1,
        [this](float v) { if (auto* s = activeSlice()) s->setAnf(v > 0.5f); },
        [this]() -> float { auto* s = activeSlice(); return s && s->anfOn() ? 1 : 0; });

    reg("rx.squelchEnable", "Squelch Enable", "RX", P::Toggle, 0, 1,
        [this](float v) { if (auto* s = activeSlice()) s->setSquelch(v > 0.5f, s->squelchLevel()); },
        [this]() -> float { auto* s = activeSlice(); return s && s->squelchOn() ? 1 : 0; });

    reg("rx.mute", "Audio Mute", "RX", P::Toggle, 0, 1,
        [this](float v) { m_audio->setMuted(v > 0.5f); },
        [this]() -> float { return m_audio->isMuted() ? 1 : 0; });

    reg("rx.tuneLock", "Tune Lock", "RX", P::Toggle, 0, 1,
        [this](float v) { if (auto* s = activeSlice()) s->setLocked(v > 0.5f); },
        [this]() -> float { auto* s = activeSlice(); return s && s->isLocked() ? 1 : 0; });

    reg("rx.ritEnable", "RIT Enable", "RX", P::Toggle, 0, 1,
        [this](float v) { if (auto* s = activeSlice()) s->setRit(v > 0.5f, s->ritFreq()); },
        [this]() -> float { auto* s = activeSlice(); return s && s->ritOn() ? 1 : 0; });

    reg("rx.xitEnable", "XIT Enable", "RX", P::Toggle, 0, 1,
        [this](float v) { if (auto* s = activeSlice()) s->setXit(v > 0.5f, s->xitFreq()); },
        [this]() -> float { auto* s = activeSlice(); return s && s->xitOn() ? 1 : 0; });

    reg("rx.nr2Enable", "NR2 (Spectral)", "RX", P::Toggle, 0, 1,
        [this](float v) { QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr2Enabled(v > 0.5f); }); },
        [this]() -> float { return m_audio->nr2Enabled() ? 1 : 0; });

    reg("rx.rn2Enable", "RN2 (RNNoise)", "RX", P::Toggle, 0, 1,
        [this](float v) { QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setRn2Enabled(v > 0.5f); }); },
        [this]() -> float { return m_audio->rn2Enabled() ? 1 : 0; });

    reg("rx.nr4Enable", "NR4 (Spectral Bleach)", "RX", P::Toggle, 0, 1,
        [this](float v) { QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setNr4Enabled(v > 0.5f); }); },
        [this]() -> float { return m_audio->nr4Enabled() ? 1 : 0; });

    reg("rx.dfnrEnable", "DFNR (DeepFilter)", "RX", P::Toggle, 0, 1,
        [this](float v) { QMetaObject::invokeMethod(m_audio, [this, v]() { m_audio->setDfnrEnabled(v > 0.5f); }); },
        [this]() -> float { return m_audio->dfnrEnabled() ? 1 : 0; });

    reg("rx.stepUp", "Step Size Up", "RX", P::Trigger, 0, 1,
        [this](float) { if (auto* rx = m_appletPanel->rxApplet()) rx->cycleStepUp(); });

    reg("rx.stepDown", "Step Size Down", "RX", P::Trigger, 0, 1,
        [this](float) { if (auto* rx = m_appletPanel->rxApplet()) rx->cycleStepDown(); });

    // rx.tuneKnob: bind a relative MIDI knob for VFO tuning.
    // Set the binding to "relative" mode in MIDI Mapping dialog.
    // Steps are coalesced every 20ms with acceleration:
    //   Slow spin → ½ rate (fine tuning), Fast spin → 4× (band scanning)
    reg("rx.tuneKnob", "VFO Tune Knob", "RX", P::Slider, 0, 127,
        [this](float v) {
            // Absolute fallback (non-relative bindings): center=64
            auto* s = activeSlice();
            if (!s || s->isLocked()) return;
            int steps = static_cast<int>(v) - 64;
            if (steps == 0) return;
            int stepHz = spectrum() ? spectrum()->stepSize() : 100;
            double newMhz = s->frequency() + steps * stepHz / 1e6;
            applyTuneRequest(s, newMhz, TuneIntent::IncrementalTune, "midi-absolute");
        });

    // ── TX ──────────────────────────────────────────────────────────────
    reg("tx.rfPower", "RF Power", "TX", P::Slider, 0, 100,
        [this](float v) { m_radioModel.transmitModel().setRfPower(static_cast<int>(v)); },
        [this]() -> float { return m_radioModel.transmitModel().rfPower(); });

    reg("tx.tunePower", "Tune Power", "TX", P::Slider, 0, 100,
        [this](float v) { m_radioModel.transmitModel().setTunePower(static_cast<int>(v)); },
        [this]() -> float { return m_radioModel.transmitModel().tunePower(); });

    reg("tx.mox", "MOX", "TX", P::Toggle, 0, 1,
        [this](float v) { m_radioModel.setTransmit(v > 0.5f); },
        [this]() -> float { return m_radioModel.transmitModel().isMox() ? 1 : 0; });

    reg("tx.tune", "TUNE", "TX", P::Toggle, 0, 1,
        [this](float v) {
            if (v > 0.5f)
                m_radioModel.transmitModel().startTune();
            else
                m_radioModel.transmitModel().stopTune();
        },
        [this]() -> float { return m_radioModel.transmitModel().isTuning() ? 1 : 0; });

    reg("tx.atuStart", "ATU Start", "TX", P::Trigger, 0, 1,
        [this](float) { m_radioModel.sendCommand("atu start"); });

    // ── Phone/CW ────────────────────────────────────────────────────────
    reg("phone.micLevel", "Mic Level", "Phone/CW", P::Slider, 0, 100,
        [this](float v) { m_radioModel.transmitModel().setMicLevel(static_cast<int>(v)); },
        [this]() -> float { return m_radioModel.transmitModel().micLevel(); });

    reg("phone.monGain", "Monitor Volume", "Phone/CW", P::Slider, 0, 100,
        [this](float v) { m_radioModel.transmitModel().setMonGainSb(static_cast<int>(v)); },
        [this]() -> float { return m_radioModel.transmitModel().monGainSb(); });

    reg("phone.procEnable", "Speech Processor", "Phone/CW", P::Toggle, 0, 1,
        [this](float v) { m_radioModel.transmitModel().setSpeechProcessorEnable(v > 0.5f); },
        [this]() -> float { return m_radioModel.transmitModel().companderOn() ? 1 : 0; });

    reg("phone.daxEnable", "DAX", "Phone/CW", P::Toggle, 0, 1,
        [this](float v) { m_radioModel.transmitModel().setDax(v > 0.5f); },
        [this]() -> float { return m_radioModel.transmitModel().daxOn() ? 1 : 0; });

    reg("phone.monEnable", "Monitor", "Phone/CW", P::Toggle, 0, 1,
        [this](float v) { m_radioModel.transmitModel().setSbMonitor(v > 0.5f); },
        [this]() -> float { return m_radioModel.transmitModel().sbMonitor() ? 1 : 0; });

    reg("phone.voxEnable", "VOX Enable", "Phone/CW", P::Toggle, 0, 1,
        [this](float v) { m_radioModel.transmitModel().setVoxEnable(v > 0.5f); },
        [this]() -> float { return m_radioModel.transmitModel().voxEnable() ? 1 : 0; });

    reg("phone.voxLevel", "VOX Level", "Phone/CW", P::Slider, 0, 100,
        [this](float v) { m_radioModel.transmitModel().setVoxLevel(static_cast<int>(v)); },
        [this]() -> float { return m_radioModel.transmitModel().voxLevel(); });

    reg("phone.amCarrier", "AM Carrier", "Phone/CW", P::Slider, 0, 100,
        [this](float v) { m_radioModel.transmitModel().setAmCarrierLevel(static_cast<int>(v)); },
        [this]() -> float { return m_radioModel.transmitModel().amCarrierLevel(); });

    reg("cw.speed", "CW Speed", "Phone/CW", P::Slider, 5, 100,
        [this](float v) { m_radioModel.transmitModel().setCwSpeed(static_cast<int>(v)); },
        [this]() -> float { return m_radioModel.transmitModel().cwSpeed(); });

    reg("cw.key", "CW Key (straight)", "Phone/CW", P::Gate, 0, 1,
        [this](float v) { m_radioModel.sendCwKey(v > 0.5f); });

    // Iambic paddle: dit and dah are separate MIDI notes.
    // The radio handles iambic A/B mode and timing — we just send both states.
    {
        auto dit = std::make_shared<bool>(false);
        auto dah = std::make_shared<bool>(false);

        reg("cw.dit", "CW Paddle Dit", "Phone/CW", P::Gate, 0, 1,
            [this, dit, dah](float v) {
                *dit = (v > 0.5f);
                m_radioModel.sendCwPaddle(*dit, *dah);
            },
            [dit]() -> float { return *dit ? 1.0f : 0.0f; });

        reg("cw.dah", "CW Paddle Dah", "Phone/CW", P::Gate, 0, 1,
            [this, dit, dah](float v) {
                *dah = (v > 0.5f);
                m_radioModel.sendCwPaddle(*dit, *dah);
            },
            [dah]() -> float { return *dah ? 1.0f : 0.0f; });
    }

    reg("cw.ptt", "PTT (hold)", "Phone/CW", P::Gate, 0, 1,
        [this](float v) { m_radioModel.setTransmit(v > 0.5f); });

    // ── EQ ──────────────────────────────────────────────────────────────
    reg("eq.txEnable", "TX EQ Enable", "EQ", P::Toggle, 0, 1,
        [this](float v) { m_radioModel.equalizerModel().setTxEnabled(v > 0.5f); },
        [this]() -> float { return m_radioModel.equalizerModel().txEnabled() ? 1 : 0; });

    reg("eq.rxEnable", "RX EQ Enable", "EQ", P::Toggle, 0, 1,
        [this](float v) { m_radioModel.equalizerModel().setRxEnabled(v > 0.5f); },
        [this]() -> float { return m_radioModel.equalizerModel().rxEnabled() ? 1 : 0; });

    {
        using B = EqualizerModel::Band;
        static const B bands[] = {B::B63, B::B125, B::B250, B::B500, B::B1k, B::B2k, B::B4k, B::B8k};
        static const int freqs[] = {63, 125, 250, 500, 1000, 2000, 4000, 8000};
        static const char* names[] = {"63 Hz", "125 Hz", "250 Hz", "500 Hz",
                                       "1 kHz", "2 kHz", "4 kHz", "8 kHz"};
        for (int i = 0; i < 8; ++i) {
            B band = bands[i];
            QString id = QString("eq.band%1").arg(freqs[i]);
            reg(id.toUtf8().constData(), names[i], "EQ", P::Slider, -10, 10,
                [this, band](float v) { m_radioModel.equalizerModel().setTxBand(band, static_cast<int>(v)); },
                [this, band]() -> float { return m_radioModel.equalizerModel().txBand(band); });
        }
    }

    // ── Global ──────────────────────────────────────────────────────────
    reg("global.masterVolume", "Master Volume", "Global", P::Slider, 0, 100,
        [this](float v) { m_radioModel.sendCommand(QString("mixer lineout gain %1").arg(static_cast<int>(v))); },
        [this]() -> float { return m_radioModel.lineoutGain(); });

    reg("global.hpVolume", "Headphone Volume", "Global", P::Slider, 0, 100,
        [this](float v) { m_radioModel.sendCommand(QString("mixer headphone gain %1").arg(static_cast<int>(v))); },
        [this]() -> float { return m_radioModel.headphoneGain(); });

    reg("global.masterMute", "Master Mute", "Global", P::Toggle, 0, 1,
        [this](float v) { m_audio->setMuted(v > 0.5f); },
        [this]() -> float { return m_audio->isMuted() ? 1 : 0; });

    reg("global.txButton", "TX Button", "Global", P::Toggle, 0, 1,
        [this](float v) { m_radioModel.setTransmit(v > 0.5f); },
        [this]() -> float { return m_radioModel.transmitModel().isMox() ? 1 : 0; });

    reg("global.tnfEnable", "TNF Global", "Global", P::Toggle, 0, 1,
        [this](float v) { m_radioModel.sendCommand(QString("radio set tnf_enabled=%1").arg(v > 0.5f ? 1 : 0)); });

    reg("global.bandUp", "Band Up", "Global", P::Trigger, 0, 1,
        [this](float) {
            // Placeholder — band cycling requires overlay menu integration
            qDebug() << "MIDI: Band Up triggered";
        });

    reg("global.bandDown", "Band Down", "Global", P::Trigger, 0, 1,
        [this](float) {
            qDebug() << "MIDI: Band Down triggered";
        });

    reg("global.nextSlice", "Next Slice", "Global", P::Trigger, 0, 1,
        [this](float) {
            const auto& slices = m_radioModel.slices();
            if (slices.size() > 1) {
                int idx = 0;
                for (int i = 0; i < slices.size(); ++i) {
                    if (slices[i]->sliceId() == m_activeSliceId) { idx = i; break; }
                }
                setActiveSlice(slices[(idx + 1) % slices.size()]->sliceId());
            }
        });

    reg("global.prevSlice", "Previous Slice", "Global", P::Trigger, 0, 1,
        [this](float) {
            const auto& slices = m_radioModel.slices();
            if (slices.size() > 1) {
                int idx = 0;
                for (int i = 0; i < slices.size(); ++i) {
                    if (slices[i]->sliceId() == m_activeSliceId) { idx = i; break; }
                }
                int prev = (idx - 1 + slices.size()) % slices.size();
                setActiveSlice(slices[prev]->sliceId());
            }
        });
}
#endif

// StreamDeck native integration removed — use TCI StreamController plugin instead.

// ─── Applet-panel pop-out (#1713 Phase 6) ───────────────────────────────────
//
// The whole AppletPanel widget (tray buttons + S-Meter + scrollable stack)
// can live either inside m_splitter at the end of the row, or as the sole
// content of its own top-level window.  Reparenting transfers Qt ownership
// cleanly; the splitter pane collapses to zero width when the panel floats,
// and restores to its remembered width when it docks back.

void MainWindow::floatAppletPanel()
{
    if (!m_appletPanel || !m_splitter) return;
    if (m_appletPanelFloatWindow) return;  // already floating

    m_appletPanelFloatWindow = new QWidget(nullptr, Qt::Window);
    m_appletPanelFloatWindow->setWindowTitle("AetherSDR — Applet Panel");
    m_appletPanelFloatWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    auto* layout = new QVBoxLayout(m_appletPanelFloatWindow);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_appletPanel->setParent(m_appletPanelFloatWindow);
    layout->addWidget(m_appletPanel);
    m_appletPanel->show();
    // Qt auto-redistributes remaining splitter slots once the panel
    // is reparented out; we don't need to touch the sizes manually.

    // Restore last-known window geometry or default.
    const QByteArray geom = QByteArray::fromBase64(
        AppSettings::instance()
            .value("AppletPanelFloatGeometry", "").toByteArray());
    if (!geom.isEmpty()) {
        m_appletPanelFloatWindow->restoreGeometry(geom);
    } else {
        m_appletPanelFloatWindow->resize(320, 720);
    }

    // Track geometry changes live so the saved position/size stays
    // current without a per-tick timer.
    m_appletPanelFloatWindow->installEventFilter(this);

    // On close → dock back (unchecks the menu action via its own
    // toggled handler, which re-enters dockAppletPanel).  Using
    // QObject::connect with a lambda so we don't need to subclass.
    connect(m_appletPanelFloatWindow, &QObject::destroyed,
            this, [this]() {
        m_appletPanelFloatWindow = nullptr;
    });
    m_appletPanelFloatWindow->show();
}

void MainWindow::dockAppletPanel()
{
    if (!m_appletPanel || !m_splitter) return;
    if (!m_appletPanelFloatWindow) return;  // already docked

    // Save geometry before tearing the window down.
    AppSettings::instance().setValue(
        "AppletPanelFloatGeometry",
        m_appletPanelFloatWindow->saveGeometry().toBase64());

    // Reparent back to the splitter.  addWidget appends, which is
    // the correct position (last slot) matching the pre-float layout.
    m_appletPanel->setParent(m_splitter);
    m_splitter->addWidget(m_appletPanel);
    m_appletPanel->show();

    // Re-apply the canonical 4-slot layout the app uses at startup:
    // CWX=0, DVK=0, center=stretch, applet=260px.  Using fixed sizes
    // instead of saveState()/restoreState() because the saved state
    // is unreliable in the launch-with-float case — saveState() fires
    // at a QTimer::singleShot(0) turn before the splitter has fully
    // laid out, producing captured sizes that don't match the
    // post-show window width.  The splitter isn't user-draggable for
    // applet width anyway (startup always forces 260), so recomputing
    // is both simpler and deterministic.
    const int centerWidth = std::max(400, m_splitter->width() - 260);
    m_splitter->setSizes({0, 0, centerWidth, 260});

    m_appletPanelFloatWindow->removeEventFilter(this);
    m_appletPanelFloatWindow->deleteLater();
    m_appletPanelFloatWindow = nullptr;
}

} // namespace AetherSDR
