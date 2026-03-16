#pragma once

#include "core/RadioDiscovery.h"

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>

namespace AetherSDR {

// Panel that shows discovered radios and a Connect/Disconnect button.
class ConnectionPanel : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionPanel(QWidget* parent = nullptr);

    void setConnected(bool connected);
    void setStatusText(const QString& text);
    void setCollapsed(bool collapsed);
    bool isCollapsed() const { return m_collapsed; }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

public slots:
    void onRadioDiscovered(const RadioInfo& radio);
    void onRadioUpdated(const RadioInfo& radio);
    void onRadioLost(const QString& serial);

signals:
    void connectRequested(const RadioInfo& radio);
    void disconnectRequested();
    void collapsedChanged(bool collapsed);

private slots:
    void onConnectClicked();
    void onListSelectionChanged();

private:

    QListWidget* m_radioList;
    QPushButton* m_connectBtn;
    QPushButton* m_collapseBtn;
    QLabel*      m_statusLabel;
    QLabel*      m_indicatorLabel;
    QWidget*     m_radioGroup;       // "Discovered Radios" group box

    QList<RadioInfo> m_radios;   // mirror of what's in the list
    bool m_connected{false};
    bool m_collapsed{false};
    int  m_expandedWidth{260};
};

} // namespace AetherSDR
