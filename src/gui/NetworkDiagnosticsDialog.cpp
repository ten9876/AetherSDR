#include "NetworkDiagnosticsDialog.h"
#include "core/AudioEngine.h"
#include "models/RadioModel.h"

#include <QScrollArea>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QPushButton>

namespace AetherSDR {

NetworkDiagnosticsDialog::NetworkDiagnosticsDialog(RadioModel* model, AudioEngine* audio, QWidget* parent)
    : QDialog(parent), m_model(model), m_audio(audio)
{
    setWindowTitle("Network Diagnostics");
    setMinimumSize(920, 680);
    resize(980, 760);

    auto* root = new QVBoxLayout(this);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    root->addWidget(scroll);

    auto* content = new QWidget;
    scroll->setWidget(content);
    auto* contentLayout = new QGridLayout(content);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setColumnStretch(0, 1);
    contentLayout->setColumnStretch(1, 1);
    contentLayout->setHorizontalSpacing(16);
    contentLayout->setVerticalSpacing(14);

    auto makeVal = [](const QString& init = "") {
        auto* l = new QLabel(init);
        l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        l->setWordWrap(true);
        l->setMinimumHeight(l->fontMetrics().height() + 1);
        l->setStyleSheet("QLabel { color: #c8d8e8; font-weight: bold; }");
        return l;
    };

    auto makeDim = [&](const QString& init = "") {
        return makeVal(init);
    };

    auto makeNote = [](const QString& text) {
        auto* l = new QLabel(text);
        l->setWordWrap(true);
        l->setStyleSheet("QLabel { color: #8aa8c0; font-size: 11px; line-height: 1.2; }");
        return l;
    };

    // ── Network Status group ─────────────────────────────────────────────
    auto* statusGroup = new QGroupBox("Network Status");
    auto* statusGrid = new QGridLayout(statusGroup);
    statusGrid->setColumnStretch(1, 1);
    statusGrid->setVerticalSpacing(2);
    statusGrid->setHorizontalSpacing(12);

    int row = 0;
    statusGrid->addWidget(makeNote(
        "Connection path and TCP latency. Use this to confirm the selected route "
        "to the radio is stable."), row++, 0, 1, 2);
    statusGrid->addWidget(new QLabel("Status:"), row, 0);
    m_statusLabel = makeVal();
    statusGrid->addWidget(m_statusLabel, row++, 1);

    statusGrid->addWidget(new QLabel("Target Radio IP:"), row, 0);
    m_targetIpLabel = makeVal();
    statusGrid->addWidget(m_targetIpLabel, row++, 1);

    statusGrid->addWidget(new QLabel("Selected Source:"), row, 0);
    m_sourcePathLabel = makeVal();
    m_sourcePathLabel->setWordWrap(true);
    statusGrid->addWidget(m_sourcePathLabel, row++, 1);

    statusGrid->addWidget(new QLabel("Local TCP:"), row, 0);
    m_tcpEndpointLabel = makeVal();
    statusGrid->addWidget(m_tcpEndpointLabel, row++, 1);

    statusGrid->addWidget(new QLabel("Local UDP:"), row, 0);
    m_udpEndpointLabel = makeVal();
    statusGrid->addWidget(m_udpEndpointLabel, row++, 1);

    statusGrid->addWidget(new QLabel("First UDP Packet:"), row, 0);
    m_udpSeenLabel = makeVal();
    statusGrid->addWidget(m_udpSeenLabel, row++, 1);

    statusGrid->addWidget(new QLabel("Latency (RTT):"), row, 0);
    m_rttLabel = makeVal();
    statusGrid->addWidget(m_rttLabel, row++, 1);

    statusGrid->addWidget(new QLabel("Max Latency (RTT):"), row, 0);
    m_maxRttLabel = makeVal();
    statusGrid->addWidget(m_maxRttLabel, row++, 1);

    // ── Stream Rates group ───────────────────────────────────────────────
    auto* rateGroup = new QGroupBox("Incoming Stream Rates");
    auto* rateGrid = new QGridLayout(rateGroup);
    rateGrid->setColumnStretch(1, 1);
    rateGrid->setVerticalSpacing(2);
    rateGrid->setHorizontalSpacing(12);

    row = 0;
    rateGrid->addWidget(makeNote(
        "Current receive/transmit bitrates by stream type. Large swings can indicate "
        "bursty delivery even when no packets are lost."), row++, 0, 1, 2);
    rateGrid->addWidget(new QLabel("Audio:"), row, 0);
    m_audioRateLabel = makeVal();
    rateGrid->addWidget(m_audioRateLabel, row++, 1);

    rateGrid->addWidget(new QLabel("FFT:"), row, 0);
    m_fftRateLabel = makeVal();
    rateGrid->addWidget(m_fftRateLabel, row++, 1);

    rateGrid->addWidget(new QLabel("Waterfall:"), row, 0);
    m_wfRateLabel = makeVal();
    rateGrid->addWidget(m_wfRateLabel, row++, 1);

    rateGrid->addWidget(new QLabel("Meters:"), row, 0);
    m_meterRateLabel = makeVal();
    rateGrid->addWidget(m_meterRateLabel, row++, 1);

    rateGrid->addWidget(new QLabel("DAX:"), row, 0);
    m_daxRateLabel = makeDim();
    rateGrid->addWidget(m_daxRateLabel, row++, 1);

    rateGrid->addWidget(new QLabel("Total RX:"), row, 0);
    m_rxRateLabel = makeVal();
    rateGrid->addWidget(m_rxRateLabel, row++, 1);

    rateGrid->addWidget(new QLabel("Total TX:"), row, 0);
    m_txRateLabel = makeDim();
    rateGrid->addWidget(m_txRateLabel, row++, 1);

    // ── Packet Loss group ────────────────────────────────────────────────
    auto* dropGroup = new QGroupBox("Packet Loss (Sequence Gaps)");
    auto* dropGrid = new QGridLayout(dropGroup);
    dropGrid->setColumnStretch(1, 1);
    dropGrid->setVerticalSpacing(2);
    dropGrid->setHorizontalSpacing(12);

    row = 0;
    dropGrid->addWidget(makeNote(
        "Inferred packet loss from missing VITA sequence numbers. Zero loss here does "
        "not rule out jitter or late bursts."), row++, 0, 1, 2);
    dropGrid->addWidget(new QLabel("Audio:"), row, 0);
    m_audioDropLabel = makeDim();
    dropGrid->addWidget(m_audioDropLabel, row++, 1);

    dropGrid->addWidget(new QLabel("FFT:"), row, 0);
    m_fftDropLabel = makeDim();
    dropGrid->addWidget(m_fftDropLabel, row++, 1);

    dropGrid->addWidget(new QLabel("Waterfall:"), row, 0);
    m_wfDropLabel = makeDim();
    dropGrid->addWidget(m_wfDropLabel, row++, 1);

    dropGrid->addWidget(new QLabel("Meters:"), row, 0);
    m_meterDropLabel = makeDim();
    dropGrid->addWidget(m_meterDropLabel, row++, 1);

    dropGrid->addWidget(new QLabel("DAX:"), row, 0);
    m_daxDropLabel = makeDim();
    dropGrid->addWidget(m_daxDropLabel, row++, 1);

    m_droppedLabel = new QLabel;
    m_droppedLabel->setAlignment(Qt::AlignCenter);
    m_droppedLabel->setStyleSheet("QLabel { color: #c8d8e8; font-weight: bold; }");
    dropGrid->addWidget(m_droppedLabel, row++, 0, 1, 2);

    // ── Audio Playback group ──────────────────────────────────────────────
    auto* audioGroup = new QGroupBox("Audio Playback");
    auto* audioGrid = new QGridLayout(audioGroup);
    audioGrid->setColumnStretch(1, 1);
    audioGrid->setVerticalSpacing(2);
    audioGrid->setHorizontalSpacing(12);

    row = 0;
    audioGrid->addWidget(makeNote(
        "Speaker-side buffer health. If underruns rise while the buffer stays near zero, "
        "playback is starving. Arrival gap and jitter measure timing, not packet loss."),
        row++, 0, 1, 2);
    audioGrid->addWidget(new QLabel("RX Buffer Now:"), row, 0);
    m_audioBufferLabel = makeVal();
    audioGrid->addWidget(m_audioBufferLabel, row++, 1);

    audioGrid->addWidget(new QLabel("RX Buffer Peak:"), row, 0);
    m_audioBufferPeakLabel = makeVal();
    audioGrid->addWidget(m_audioBufferPeakLabel, row++, 1);

    audioGrid->addWidget(new QLabel("Underruns (total):"), row, 0);
    m_audioUnderrunLabel = makeVal();
    audioGrid->addWidget(m_audioUnderrunLabel, row++, 1);

    audioGrid->addWidget(new QLabel("Underruns (last sec):"), row, 0);
    m_audioUnderrunRateLabel = makeVal();
    audioGrid->addWidget(m_audioUnderrunRateLabel, row++, 1);

    audioGrid->addWidget(new QLabel("Audio Arrival Gap:"), row, 0);
    m_audioPacketGapLabel = makeVal();
    audioGrid->addWidget(m_audioPacketGapLabel, row++, 1);

    audioGrid->addWidget(new QLabel("Max Arrival Gap:"), row, 0);
    m_audioPacketGapMaxLabel = makeVal();
    audioGrid->addWidget(m_audioPacketGapMaxLabel, row++, 1);

    audioGrid->addWidget(new QLabel("Jitter Estimate:"), row, 0);
    m_audioJitterLabel = makeVal();
    audioGrid->addWidget(m_audioJitterLabel, row++, 1);

    contentLayout->addWidget(statusGroup, 0, 0);
    contentLayout->addWidget(rateGroup, 0, 1);
    contentLayout->addWidget(dropGroup, 1, 0);
    contentLayout->addWidget(audioGroup, 1, 1);

    // ── Close button ─────────────────────────────────────────────────────
    auto* closeBtn = new QPushButton("Close");
    closeBtn->setFixedWidth(80);
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    root->addLayout(btnRow);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    // Snapshot initial byte counts for rate calculation
    m_lastRxBytes = m_model->rxBytes();
    m_lastTxBytes = m_model->txBytes();
    m_lastAudioUnderrunCount = m_audio ? m_audio->rxBufferUnderrunCount() : 0;
    for (int i = 0; i < PanadapterStream::CatCount; ++i)
        m_lastCatBytes[i] = m_model->categoryStats(static_cast<PanadapterStream::StreamCategory>(i)).bytes;

    // Refresh every second
    connect(&m_refreshTimer, &QTimer::timeout, this, &NetworkDiagnosticsDialog::refresh);
    m_refreshTimer.start(1000);
    refresh();
}

static QString formatDrop(const PanadapterStream::CategoryStats& cs)
{
    if (cs.packets == 0) return "0 / 0";
    const double pct = (cs.errors * 100.0) / cs.packets;
    return QString("%1 / %2 (%3%)").arg(cs.errors).arg(cs.packets).arg(pct, 0, 'f', 2);
}

static QString formatRate(qint64 bytesDelta)
{
    const int kbps = static_cast<int>((bytesDelta * 8) / 1000);
    return QString("%1 kbps").arg(kbps);
}

static QString formatAudioBuffer(qsizetype bytes, int sampleRate)
{
    if (sampleRate <= 0) {
        return QString("%1 bytes").arg(bytes);
    }

    static constexpr int kStereoChannels = 2;
    static constexpr int kFloatBytesPerSample = 4;
    const double ms = (bytes * 1000.0) / (sampleRate * kStereoChannels * kFloatBytesPerSample);
    return QString("%1 bytes (%2 ms)").arg(bytes).arg(ms, 0, 'f', 1);
}

static QString formatMsValue(int value)
{
    return value < 1 ? "< 1 ms" : QString("%1 ms").arg(value);
}

void NetworkDiagnosticsDialog::refresh()
{
    // Status and RTT
    m_statusLabel->setText(m_model->networkQuality());
    m_targetIpLabel->setText(m_model->targetRadioIp().isEmpty()
                                 ? "Not connected"
                                 : m_model->targetRadioIp());
    m_sourcePathLabel->setText(m_model->selectedSourcePath());
    m_tcpEndpointLabel->setText(m_model->localTcpEndpoint());
    m_udpEndpointLabel->setText(m_model->localUdpEndpoint());
    m_udpSeenLabel->setText(m_model->firstUdpPacketSeen() ? "Yes" : "No");

    const int rtt = m_model->lastPingRtt();
    m_rttLabel->setText(rtt < 1 ? "< 1 ms" : QString("%1 ms").arg(rtt));

    const int maxRtt = m_model->maxPingRtt();
    m_maxRttLabel->setText(maxRtt < 1 ? "< 1 ms" : QString("%1 ms").arg(maxRtt));

    // Per-category rates
    static constexpr PanadapterStream::StreamCategory cats[] = {
        PanadapterStream::CatAudio, PanadapterStream::CatFFT,
        PanadapterStream::CatWaterfall, PanadapterStream::CatMeter
    };
    QLabel* rateLabels[] = { m_audioRateLabel, m_fftRateLabel, m_wfRateLabel, m_meterRateLabel };
    QLabel* dropLabels[] = { m_audioDropLabel, m_fftDropLabel, m_wfDropLabel, m_meterDropLabel };

    for (int i = 0; i < 4; ++i) {
        auto cs = m_model->categoryStats(cats[i]);
        const qint64 delta = cs.bytes - m_lastCatBytes[cats[i]];
        rateLabels[i]->setText(formatRate(delta));
        m_lastCatBytes[cats[i]] = cs.bytes;
        dropLabels[i]->setText(formatDrop(cs));
    }

    // DAX traffic
    {
        auto cs = m_model->categoryStats(PanadapterStream::CatDAX);
        const qint64 delta = cs.bytes - m_lastCatBytes[PanadapterStream::CatDAX];
        m_daxRateLabel->setText(formatRate(delta));
        m_lastCatBytes[PanadapterStream::CatDAX] = cs.bytes;
        m_daxDropLabel->setText(formatDrop(cs));
    }

    // Total RX: all UDP bytes received on our VITA-49 socket
    const qint64 curRx = m_model->rxBytes();
    m_rxRateLabel->setText(formatRate(curRx - m_lastRxBytes));
    m_lastRxBytes = curRx;
    const qint64 curTx = m_model->txBytes();
    m_txRateLabel->setText(formatRate(curTx - m_lastTxBytes));
    m_lastTxBytes = curTx;

    // Total dropped (across all owned streams)
    const int dropped = m_model->packetDropCount();
    const int total = m_model->packetTotalCount();
    if (total > 0) {
        const double pct = (dropped * 100.0) / total;
        m_droppedLabel->setText(
            QString("Total: %1 / %2 dropped (%3%)")
                .arg(dropped).arg(total).arg(pct, 0, 'f', 2));
    } else {
        m_droppedLabel->setText("No packets received yet");
    }

    if (m_audio) {
        const int sampleRate = m_audio->rxBufferSampleRate();
        const quint64 underruns = m_audio->rxBufferUnderrunCount();
        m_audioBufferLabel->setText(formatAudioBuffer(m_audio->rxBufferBytes(), sampleRate));
        m_audioBufferPeakLabel->setText(formatAudioBuffer(m_audio->rxBufferPeakBytes(), sampleRate));
        m_audioUnderrunLabel->setText(QString::number(underruns));
        m_audioUnderrunRateLabel->setText(QString::number(underruns - m_lastAudioUnderrunCount));
        m_lastAudioUnderrunCount = underruns;

        auto* panStream = m_model->panStream();
        m_audioPacketGapLabel->setText(formatMsValue(panStream->audioPacketGapMs()));
        m_audioPacketGapMaxLabel->setText(formatMsValue(panStream->audioPacketGapMaxMs()));
        m_audioJitterLabel->setText(formatMsValue(panStream->audioPacketJitterMs()));
    } else {
        m_audioBufferLabel->setText("Unavailable");
        m_audioBufferPeakLabel->setText("Unavailable");
        m_audioUnderrunLabel->setText("Unavailable");
        m_audioUnderrunRateLabel->setText("Unavailable");
        m_audioPacketGapLabel->setText("Unavailable");
        m_audioPacketGapMaxLabel->setText("Unavailable");
        m_audioJitterLabel->setText("Unavailable");
    }

    // Color the status label
    const QString q = m_model->networkQuality();
    if (q == "Excellent" || q == "Very Good")
        m_statusLabel->setStyleSheet("QLabel { color: #00cc66; font-weight: bold; }");
    else if (q == "Good")
        m_statusLabel->setStyleSheet("QLabel { color: #88cc00; font-weight: bold; }");
    else if (q == "Fair")
        m_statusLabel->setStyleSheet("QLabel { color: #ccaa00; font-weight: bold; }");
    else if (q == "Poor")
        m_statusLabel->setStyleSheet("QLabel { color: #cc3300; font-weight: bold; }");
    else
        m_statusLabel->setStyleSheet("QLabel { color: #c8d8e8; font-weight: bold; }");
}

} // namespace AetherSDR
