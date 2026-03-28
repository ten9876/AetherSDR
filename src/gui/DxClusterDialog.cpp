#include "DxClusterDialog.h"
#include "core/DxClusterClient.h"
#include "core/AppSettings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QPushButton>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QScrollBar>

namespace AetherSDR {

DxClusterDialog::DxClusterDialog(DxClusterClient* client, QWidget* parent)
    : QDialog(parent), m_client(client)
{
    setWindowTitle("DX Cluster");
    setMinimumSize(560, 460);
    resize(620, 520);

    auto& s = AppSettings::instance();

    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);

    // ── Connection settings ─────────────────────────────────────────────
    auto* connGroup = new QGroupBox("Connection");
    auto* connLayout = new QVBoxLayout(connGroup);
    connLayout->setSpacing(4);

    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    int row = 0;

    grid->addWidget(new QLabel("Server:"), row, 0);
    m_hostEdit = new QLineEdit(s.value("DxClusterHost", "dxc.nc7j.com").toString());
    m_hostEdit->setPlaceholderText("dxc.nc7j.com");
    m_hostEdit->setStyleSheet("QLineEdit { background: #1a1a2e; color: #c8d8e8; border: 1px solid #203040; padding: 3px; }");
    grid->addWidget(m_hostEdit, row, 1);
    row++;

    grid->addWidget(new QLabel("Port:"), row, 0);
    m_portSpin = new QSpinBox;
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(s.value("DxClusterPort", 7300).toInt());
    m_portSpin->setStyleSheet("QSpinBox { background: #1a1a2e; color: #c8d8e8; border: 1px solid #203040; padding: 3px; }");
    grid->addWidget(m_portSpin, row, 1);
    row++;

    grid->addWidget(new QLabel("Callsign:"), row, 0);
    m_callEdit = new QLineEdit(s.value("DxClusterCallsign").toString());
    m_callEdit->setPlaceholderText("your callsign");
    m_callEdit->setStyleSheet("QLineEdit { background: #1a1a2e; color: #c8d8e8; border: 1px solid #203040; padding: 3px; }");
    grid->addWidget(m_callEdit, row, 1);
    row++;

    connLayout->addLayout(grid);

    // Button row: auto-connect + connect/disconnect + status
    auto* btnRow = new QHBoxLayout;
    m_autoConnectBtn = new QPushButton(
        s.value("DxClusterAutoConnect", "False").toString() == "True" ? "Auto-Connect: ON" : "Auto-Connect: OFF");
    m_autoConnectBtn->setCheckable(true);
    m_autoConnectBtn->setChecked(s.value("DxClusterAutoConnect", "False").toString() == "True");
    m_autoConnectBtn->setStyleSheet(
        "QPushButton { background: #206030; color: white; border: 1px solid #305040; padding: 4px 10px; }"
        "QPushButton:!checked { background: #603020; }");
    connect(m_autoConnectBtn, &QPushButton::toggled, this, [this](bool on) {
        m_autoConnectBtn->setText(on ? "Auto-Connect: ON" : "Auto-Connect: OFF");
        auto& s = AppSettings::instance();
        s.setValue("DxClusterAutoConnect", on ? "True" : "False");
        s.save();
    });
    btnRow->addWidget(m_autoConnectBtn);

    btnRow->addStretch();

    m_statusLabel = new QLabel("Disconnected");
    m_statusLabel->setStyleSheet("QLabel { color: #808080; font-size: 11px; }");
    btnRow->addWidget(m_statusLabel);

    btnRow->addStretch();

    m_connectBtn = new QPushButton(client->isConnected() ? "Disconnect" : "Connect");
    m_connectBtn->setFixedWidth(100);
    m_connectBtn->setStyleSheet(
        "QPushButton { background: #00b4d8; color: #0f0f1a; font-weight: bold; "
        "border: 1px solid #008ba8; padding: 4px; border-radius: 3px; }"
        "QPushButton:hover { background: #00c8f0; }"
        "QPushButton:disabled { background: #404060; color: #808080; }");
    connect(m_connectBtn, &QPushButton::clicked, this, [this] {
        if (m_client->isConnected()) {
            emit disconnectRequested();
            return;
        }
        QString host = m_hostEdit->text().trimmed();
        QString call = m_callEdit->text().trimmed().toUpper();
        quint16 port = static_cast<quint16>(m_portSpin->value());
        if (host.isEmpty() || call.isEmpty()) {
            m_statusLabel->setText("Server and callsign are required");
            m_statusLabel->setStyleSheet("QLabel { color: #ff4444; font-size: 11px; }");
            return;
        }
        auto& s = AppSettings::instance();
        s.setValue("DxClusterHost", host);
        s.setValue("DxClusterPort", port);
        s.setValue("DxClusterCallsign", call);
        s.save();
        emit connectRequested(host, port, call);
    });
    btnRow->addWidget(m_connectBtn);
    connLayout->addLayout(btnRow);

    root->addWidget(connGroup);

    // ── Console output ──────────────────────────────────────────────────
    auto* consoleLabel = new QLabel("Cluster Console");
    consoleLabel->setStyleSheet("QLabel { color: #00b4d8; font-weight: bold; }");
    root->addWidget(consoleLabel);

    m_console = new QPlainTextEdit;
    m_console->setReadOnly(true);
    m_console->setMaximumBlockCount(2000);
    m_console->setStyleSheet(
        "QPlainTextEdit {"
        "  background: #0a0a14;"
        "  color: #a0b0c0;"
        "  font-family: monospace;"
        "  font-size: 11px;"
        "  border: 1px solid #203040;"
        "  padding: 4px;"
        "}");
    root->addWidget(m_console, 1);  // stretch to fill

    // Command input row
    auto* cmdRow = new QHBoxLayout;
    m_cmdEdit = new QLineEdit;
    m_cmdEdit->setPlaceholderText("Type a cluster command (e.g. sh/dx 20, set/filter, bye)");
    m_cmdEdit->setStyleSheet("QLineEdit { background: #1a1a2e; color: #c8d8e8; border: 1px solid #203040; padding: 3px; font-family: monospace; }");
    m_cmdEdit->setEnabled(client->isConnected());
    connect(m_cmdEdit, &QLineEdit::returnPressed, this, [this] {
        QString cmd = m_cmdEdit->text().trimmed();
        if (cmd.isEmpty() || !m_client->isConnected()) return;
        m_client->sendCommand(cmd);
        m_console->appendPlainText("> " + cmd);
        m_cmdEdit->clear();
    });
    auto* sendBtn = new QPushButton("Send");
    sendBtn->setFixedWidth(60);
    sendBtn->setEnabled(client->isConnected());
    connect(sendBtn, &QPushButton::clicked, this, [this] {
        m_cmdEdit->returnPressed();
    });
    cmdRow->addWidget(m_cmdEdit, 1);
    cmdRow->addWidget(sendBtn);
    root->addLayout(cmdRow);

    // ── Live updates from client ────────────────────────────────────────
    connect(client, &DxClusterClient::rawLineReceived, this, [this](const QString& line) {
        m_console->appendPlainText(line);
        // Auto-scroll to bottom
        auto* sb = m_console->verticalScrollBar();
        sb->setValue(sb->maximum());
    });

    connect(client, &DxClusterClient::connected, this, [this, sendBtn] {
        m_statusLabel->setText(QString("Connected to %1:%2").arg(m_client->host()).arg(m_client->port()));
        m_statusLabel->setStyleSheet("QLabel { color: #00b4d8; font-size: 11px; }");
        m_connectBtn->setText("Disconnect");
        m_cmdEdit->setEnabled(true);
        sendBtn->setEnabled(true);
        m_console->appendPlainText("--- Connected ---");
    });
    connect(client, &DxClusterClient::disconnected, this, [this, sendBtn] {
        m_statusLabel->setText("Disconnected");
        m_statusLabel->setStyleSheet("QLabel { color: #808080; font-size: 11px; }");
        m_connectBtn->setText("Connect");
        m_cmdEdit->setEnabled(false);
        sendBtn->setEnabled(false);
        m_console->appendPlainText("--- Disconnected ---");
    });
    connect(client, &DxClusterClient::connectionError, this, [this](const QString& err) {
        m_statusLabel->setText("Error: " + err);
        m_statusLabel->setStyleSheet("QLabel { color: #ff4444; font-size: 11px; }");
        m_console->appendPlainText("--- Error: " + err + " ---");
    });

    updateStatus();
}

void DxClusterDialog::updateStatus()
{
    if (m_client->isConnected()) {
        m_statusLabel->setText(QString("Connected to %1:%2").arg(m_client->host()).arg(m_client->port()));
        m_statusLabel->setStyleSheet("QLabel { color: #00b4d8; font-size: 11px; }");
        m_connectBtn->setText("Disconnect");
        m_cmdEdit->setEnabled(true);
    } else {
        m_statusLabel->setText("Disconnected");
        m_statusLabel->setStyleSheet("QLabel { color: #808080; font-size: 11px; }");
        m_connectBtn->setText("Connect");
        m_cmdEdit->setEnabled(false);
    }
}

} // namespace AetherSDR
