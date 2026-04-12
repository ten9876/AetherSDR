#include "NetworkDiagnosticsDialog.h"
#include "models/RadioModel.h"

#include <QVBoxLayout>
#include <QGroupBox>
#include <QGridLayout>
#include <QPushButton>

namespace AetherSDR {

NetworkDiagnosticsDialog::NetworkDiagnosticsDialog(RadioModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle("Network Diagnostics");
    setFixedSize(460, 640);

    auto* root = new QVBoxLayout(this);

    auto makeVal = [](const QString& init = "") {
        auto* l = new QLabel(init);
        l->setAlignment(Qt::AlignRight);
        l->setStyleSheet("QLabel { color: #c8d8e8; font-weight: bold; }");
        return l;
    };

    auto makeDim = [&](const QString& init = "") {
        return makeVal(init);
    };

    // ── Network Status group ─────────────────────────────────────────────
    auto* statusGroup = new QGroupBox("Network Status");
    auto* statusGrid = new QGridLayout(statusGroup);
    statusGrid->setColumnStretch(1, 1);

    int row = 0;
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

    root->addWidget(statusGroup);

    // ── Stream Rates group ───────────────────────────────────────────────
    auto* rateGroup = new QGroupBox("Stream Rates");
    auto* rateGrid = new QGridLayout(rateGroup);
    rateGrid->setColumnStretch(1, 1);

    row = 0;
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

    root->addWidget(rateGroup);

    // ── Packet Loss group ────────────────────────────────────────────────
    auto* dropGroup = new QGroupBox("Packet Loss");
    auto* dropGrid = new QGridLayout(dropGroup);
    dropGrid->setColumnStretch(1, 1);

    row = 0;
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

    root->addWidget(dropGroup);

    root->addStretch();

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
