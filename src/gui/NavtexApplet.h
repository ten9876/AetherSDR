#pragma once

#include <QWidget>
#include <optional>

class QLabel;
class QLineEdit;
class QTextEdit;
class QPushButton;
class QSpinBox;

namespace AetherSDR {

class RadioModel;
class NavtexModel;

// NAVTEX Applet — compose form and status display for NAVTEX waveform (v4.2.18).
// Data wiring only; visual design/placement is maintainer-only per CLAUDE.md.
class NavtexApplet : public QWidget {
    Q_OBJECT

public:
    explicit NavtexApplet(QWidget* parent = nullptr);

    void setRadioModel(RadioModel* model);

private slots:
    void onSendClicked();
    void onStatusChanged();
    void onMessagesChanged();

private:
    void buildUI();

    RadioModel* m_radioModel{nullptr};
    NavtexModel* m_navtexModel{nullptr};

    // Compose form fields
    QLineEdit*   m_txIdentEdit{nullptr};
    QLineEdit*   m_subjIndEdit{nullptr};
    QTextEdit*   m_msgTextEdit{nullptr};
    QSpinBox*    m_serialSpin{nullptr};
    QPushButton* m_sendBtn{nullptr};

    // Status display
    QLabel*      m_statusLabel{nullptr};
    QLabel*      m_msgListLabel{nullptr};
};

} // namespace AetherSDR
