#pragma once

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
    QLabel* m_rttLabel;
    QLabel* m_maxRttLabel;
    QLabel* m_rxRateLabel;
    QLabel* m_txRateLabel;
    QLabel* m_droppedLabel;

    qint64 m_lastRxBytes{0};
    qint64 m_lastTxBytes{0};
};

} // namespace AetherSDR
