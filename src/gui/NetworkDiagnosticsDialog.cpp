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
    setFixedSize(340, 240);

    auto* root = new QVBoxLayout(this);

    // ── Network Diagnostics group ─────────────────────────────────────────
    auto* group = new QGroupBox("Network Diagnostics");
    auto* grid = new QGridLayout(group);
    grid->setColumnStretch(1, 1);

    auto makeVal = [](const QString& init = "") {
        auto* l = new QLabel(init);
        l->setAlignment(Qt::AlignRight);
        l->setStyleSheet("QLabel { color: #c8d8e8; font-weight: bold; }");
        return l;
    };

    int row = 0;
    grid->addWidget(new QLabel("Network Status:"), row, 0);
    m_statusLabel = makeVal();
    grid->addWidget(m_statusLabel, row++, 1);

    grid->addWidget(new QLabel("Latency (RTT):"), row, 0);
    m_rttLabel = makeVal();
    grid->addWidget(m_rttLabel, row++, 1);

    grid->addWidget(new QLabel("Max Latency (RTT):"), row, 0);
    m_maxRttLabel = makeVal();
    grid->addWidget(m_maxRttLabel, row++, 1);

    grid->addWidget(new QLabel("Remote RX Rate:"), row, 0);
    m_rxRateLabel = makeVal();
    grid->addWidget(m_rxRateLabel, row++, 1);

    grid->addWidget(new QLabel("Remote TX Rate:"), row, 0);
    m_txRateLabel = makeVal();
    grid->addWidget(m_txRateLabel, row++, 1);

    root->addWidget(group);

    // ── Dropped packets line ──────────────────────────────────────────────
    m_droppedLabel = new QLabel;
    m_droppedLabel->setAlignment(Qt::AlignCenter);
    m_droppedLabel->setStyleSheet("QLabel { color: #a0b0c0; }");
    root->addWidget(m_droppedLabel);

    root->addStretch();

    // ── Close button ──────────────────────────────────────────────────────
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

    // Refresh every second
    connect(&m_refreshTimer, &QTimer::timeout, this, &NetworkDiagnosticsDialog::refresh);
    m_refreshTimer.start(1000);
    refresh(); // initial populate
}

void NetworkDiagnosticsDialog::refresh()
{
    // Status and RTT
    m_statusLabel->setText(m_model->networkQuality());

    const int rtt = m_model->lastPingRtt();
    m_rttLabel->setText(rtt < 1 ? "< 1 ms" : QString("%1 ms").arg(rtt));

    const int maxRtt = m_model->maxPingRtt();
    m_maxRttLabel->setText(maxRtt < 1 ? "< 1 ms" : QString("%1 ms").arg(maxRtt));

    // Byte rates (delta over 1 second = bytes/sec → kbps)
    const qint64 curRx = m_model->rxBytes();
    const qint64 curTx = m_model->txBytes();
    const qint64 rxDelta = curRx - m_lastRxBytes;
    const qint64 txDelta = curTx - m_lastTxBytes;
    m_lastRxBytes = curRx;
    m_lastTxBytes = curTx;

    const int rxKbps = static_cast<int>((rxDelta * 8) / 1000);
    const int txKbps = static_cast<int>((txDelta * 8) / 1000);
    m_rxRateLabel->setText(QString("%1 kbps").arg(rxKbps));
    m_txRateLabel->setText(QString("%1 kbps").arg(txKbps));

    // Dropped packets
    const int dropped = m_model->packetDropCount();
    const int total = m_model->packetTotalCount();
    if (total > 0) {
        const double pct = (dropped * 100.0) / total;
        m_droppedLabel->setText(
            QString("Dropped %1 out of %2 packets (%3%)")
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
