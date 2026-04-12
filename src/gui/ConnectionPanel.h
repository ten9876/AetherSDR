#pragma once

#include "core/RadioDiscovery.h"
#include "core/SmartLinkClient.h"

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QTcpSocket>

namespace AetherSDR {

// Floating panel that shows discovered radios, SmartLink, and manual connection.
// Displayed as a popup anchored to the station label in the status bar.
class ConnectionPanel : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionPanel(QWidget* parent = nullptr);

    void setConnected(bool connected);
    void setStatusText(const QString& text);
    void probeRadio(const QString& ip);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* e) override;

public slots:
    void onRadioDiscovered(const RadioInfo& radio);
    void onRadioUpdated(const RadioInfo& radio);
    void onRadioLost(const QString& serial);

    // SmartLink
    void setSmartLinkClient(SmartLinkClient* client);

signals:
    void connectRequested(const RadioInfo& radio);
    void wanConnectRequested(const WanRadioInfo& radio);
    void disconnectRequested();
    void routedRadioFound(const RadioInfo& radio);
    void smartLinkLoginRequested(const QString& email, const QString& password);

private slots:
    void onConnectClicked();
    void onListSelectionChanged();
    void onManualIpChanged(const QString& ip);

private:
    void refreshManualSourceOptions(const RadioBindSettings* selected = nullptr);
    void applySavedSourceSelection(const QString& ip);
    RadioBindSettings currentManualBindSettings(bool* staleSelection = nullptr) const;
    void saveManualProfile(const QString& targetIp,
                           const RadioBindSettings& settings,
                           const QHostAddress& lastSuccessfulLocalIp);

    QListWidget* m_radioList;
    QPushButton* m_connectBtn;
    QCheckBox*   m_lowBwCheck;
    QLabel*      m_statusLabel;
    QWidget*     m_radioGroup;       // "Discovered Radios" group box

    QList<RadioInfo> m_radios;   // mirror of what's in the list
    bool m_connected{false};

    // SmartLink UI
    SmartLinkClient* m_smartLink{nullptr};
    QWidget*     m_smartLinkGroup{nullptr};
    QWidget*     m_loginForm{nullptr};
    QLineEdit*   m_emailEdit{nullptr};
    QLineEdit*   m_passwordEdit{nullptr};
    QPushButton* m_loginBtn{nullptr};
    QLabel*      m_slUserLabel{nullptr};
    QList<WanRadioInfo> m_wanRadios;

    // Manual (routed) connection
    QWidget*     m_manualGroup{nullptr};
    QLineEdit*   m_manualIpEdit{nullptr};
    QComboBox*   m_manualSourceCombo{nullptr};
    QLabel*      m_manualSourceWarningLabel{nullptr};
    QPushButton* m_manualProbeBtn{nullptr};
    QString      m_manualProfileIp;
};

} // namespace AetherSDR
