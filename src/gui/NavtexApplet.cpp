#include "NavtexApplet.h"
#include "models/RadioModel.h"
#include "models/NavtexModel.h"

#include <QLabel>
#include <QLineEdit>
#include <QTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>

namespace AetherSDR {

NavtexApplet::NavtexApplet(QWidget* parent)
    : QWidget(parent)
{
    buildUI();
}

void NavtexApplet::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Status display
    m_statusLabel = new QLabel("Status: Inactive");
    layout->addWidget(m_statusLabel);

    // Compose form
    auto* formGroup = new QGroupBox("Compose");
    auto* form = new QFormLayout(formGroup);

    m_txIdentEdit = new QLineEdit;
    m_txIdentEdit->setMaxLength(1);
    m_txIdentEdit->setPlaceholderText("A-Z");
    form->addRow("TX Ident:", m_txIdentEdit);

    m_subjIndEdit = new QLineEdit;
    m_subjIndEdit->setMaxLength(1);
    m_subjIndEdit->setPlaceholderText("A-Z");
    form->addRow("Subject:", m_subjIndEdit);

    m_serialSpin = new QSpinBox;
    m_serialSpin->setRange(0, 999);
    m_serialSpin->setSpecialValueText("Auto");
    m_serialSpin->setValue(0);
    form->addRow("Serial #:", m_serialSpin);

    m_msgTextEdit = new QTextEdit;
    m_msgTextEdit->setMaximumHeight(80);
    m_msgTextEdit->setPlaceholderText("Message text...");
    form->addRow("Message:", m_msgTextEdit);

    m_sendBtn = new QPushButton("Send");
    m_sendBtn->setEnabled(false);
    form->addRow(m_sendBtn);

    layout->addWidget(formGroup);

    // Message list (simple label for now — visual design is maintainer-only)
    m_msgListLabel = new QLabel("No messages.");
    m_msgListLabel->setWordWrap(true);
    layout->addWidget(m_msgListLabel);

    layout->addStretch();

    connect(m_sendBtn, &QPushButton::clicked, this, &NavtexApplet::onSendClicked);
}

void NavtexApplet::setRadioModel(RadioModel* model)
{
    m_radioModel = model;
    if (!model) {
        m_navtexModel = nullptr;
        m_sendBtn->setEnabled(false);
        return;
    }

    m_navtexModel = &model->navtexModel();

    connect(m_navtexModel, &NavtexModel::statusChanged,
            this, &NavtexApplet::onStatusChanged);
    connect(m_navtexModel, &NavtexModel::messagesChanged,
            this, &NavtexApplet::onMessagesChanged);

    // Enable send button when NAVTEX is active
    onStatusChanged();
}

void NavtexApplet::onSendClicked()
{
    if (!m_navtexModel) return;

    QString txIdentStr = m_txIdentEdit->text().trimmed().toUpper();
    QString subjIndStr = m_subjIndEdit->text().trimmed().toUpper();
    QString msgText = m_msgTextEdit->toPlainText();

    if (txIdentStr.isEmpty() || subjIndStr.isEmpty() || msgText.isEmpty()) {
        return;
    }

    QChar txIdent = txIdentStr.at(0);
    QChar subjInd = subjIndStr.at(0);

    std::optional<uint> serial = std::nullopt;
    if (m_serialSpin->value() > 0) {
        serial = static_cast<uint>(m_serialSpin->value());
    }

    m_navtexModel->sendMessage(txIdent, subjInd, msgText, serial);

    // Clear form after send
    m_msgTextEdit->clear();
    m_serialSpin->setValue(0);
}

void NavtexApplet::onStatusChanged()
{
    if (!m_navtexModel) return;

    NavtexStatus st = m_navtexModel->status();
    QString text;
    switch (st) {
    case NavtexStatus::Inactive:     text = "Inactive"; break;
    case NavtexStatus::Active:       text = "Active"; break;
    case NavtexStatus::Transmitting: text = "Transmitting"; break;
    case NavtexStatus::QueueFull:    text = "Queue Full"; break;
    case NavtexStatus::Unlicensed:   text = "Unlicensed"; break;
    case NavtexStatus::Error:        text = "Error"; break;
    }
    m_statusLabel->setText("Status: " + text);

    // Only allow send when active
    bool canSend = (st == NavtexStatus::Active || st == NavtexStatus::Transmitting);
    m_sendBtn->setEnabled(canSend);
}

void NavtexApplet::onMessagesChanged()
{
    if (!m_navtexModel) return;

    const auto& msgs = m_navtexModel->messages();
    if (msgs.isEmpty()) {
        m_msgListLabel->setText("No messages.");
        return;
    }

    // Simple text summary of recent messages
    QStringList lines;
    int start = qMax(0, msgs.size() - 5);  // Show last 5
    for (int i = start; i < msgs.size(); ++i) {
        const auto& msg = msgs[i];
        QString statusStr;
        switch (msg.status) {
        case NavtexMsgStatus::Pending: statusStr = "Pending"; break;
        case NavtexMsgStatus::Queued:  statusStr = "Queued"; break;
        case NavtexMsgStatus::Sent:    statusStr = "Sent"; break;
        case NavtexMsgStatus::Error:   statusStr = "Error"; break;
        }
        lines << QString("#%1 [%2] %3").arg(msg.idx).arg(statusStr).arg(msg.dateTime);
    }
    m_msgListLabel->setText(lines.join("\n"));
}

} // namespace AetherSDR
