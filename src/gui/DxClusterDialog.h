#pragma once

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QPushButton;
class QLabel;
class QPlainTextEdit;

namespace AetherSDR {

class DxClusterClient;

// Settings and connection dialog for built-in DX cluster telnet client.
// Accessed via Settings → DX Cluster...
class DxClusterDialog : public QDialog {
    Q_OBJECT

public:
    explicit DxClusterDialog(DxClusterClient* client, QWidget* parent = nullptr);

    void updateStatus();

signals:
    void connectRequested(const QString& host, quint16 port, const QString& callsign);
    void disconnectRequested();

private:
    DxClusterClient* m_client;

    QLineEdit*      m_hostEdit;
    QSpinBox*       m_portSpin;
    QLineEdit*      m_callEdit;
    QPushButton*    m_connectBtn;
    QPushButton*    m_autoConnectBtn;
    QLabel*         m_statusLabel;
    QPlainTextEdit* m_console;
    QLineEdit*      m_cmdEdit;
};

} // namespace AetherSDR
