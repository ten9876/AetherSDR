#pragma once

#include "core/PanadapterStream.h"

#include <QDialog>
#include <QLabel>
#include <QTimer>

namespace AetherSDR {

class RadioModel;

class NetworkDiagnosticsDialog : public QDialog {
    Q_OBJECT

public:
    explicit NetworkDiagnosticsDialog(RadioModel* model, QWidget* parent = nullptr);

private:
    void refresh();

    RadioModel* m_model;
    QTimer      m_refreshTimer;

    QLabel* m_statusLabel;
    QLabel* m_targetIpLabel;
    QLabel* m_sourcePathLabel;
    QLabel* m_tcpEndpointLabel;
    QLabel* m_udpEndpointLabel;
    QLabel* m_udpSeenLabel;
    QLabel* m_rttLabel;
    QLabel* m_maxRttLabel;
    QLabel* m_rxRateLabel;
    QLabel* m_txRateLabel;
    QLabel* m_droppedLabel;

    // Per-category rate labels
    QLabel* m_audioRateLabel;
    QLabel* m_fftRateLabel;
    QLabel* m_wfRateLabel;
    QLabel* m_meterRateLabel;
    QLabel* m_daxRateLabel;

    // Per-category drop labels
    QLabel* m_audioDropLabel;
    QLabel* m_fftDropLabel;
    QLabel* m_wfDropLabel;
    QLabel* m_meterDropLabel;
    QLabel* m_daxDropLabel;

    qint64 m_lastRxBytes{0};
    qint64 m_lastTxBytes{0};

    // Per-category byte snapshots for rate calculation
    qint64 m_lastCatBytes[PanadapterStream::CatCount]{};
};

} // namespace AetherSDR
