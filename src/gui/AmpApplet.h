#pragma once

#include <QWidget>
#include <QPushButton>

class QLabel;
class QTabWidget;

namespace AetherSDR {

class HGauge;

class AmpApplet : public QWidget {
    Q_OBJECT
public:
    explicit AmpApplet(QWidget* parent = nullptr);

    void setFwdPower(float watts);
    void setSwr(float swr);
    void setTemp(float degC);
    void setDrainCurrent(float amps);
    void setMainsVoltage(int volts);
    void setState(const QString& state);
    void setMeff(const QString& meff);

    // Add a plugin tab (#1109). widget is reparented into the tab.
    // The plugin section becomes visible as soon as the first tab is added.
    void addPluginTab(const QString& tabLabel, QWidget* widget);

signals:
    void operateToggled(bool on);

private:
    void updatePowerLabel();

    HGauge*  m_fwdGauge{nullptr};
    HGauge*  m_swrGauge{nullptr};
    HGauge*  m_tempGauge{nullptr};
    QLabel*  m_powerLabel{nullptr};
    QLabel*  m_meffLabel{nullptr};
    QPushButton* m_operateBtn{nullptr};
    int      m_mainsVolts{0};
    float    m_drainAmps{0};

    // Plugin tab section — hidden until at least one plugin is loaded (#1109)
    QWidget*    m_pluginSection{nullptr};
    QTabWidget* m_pluginTabs{nullptr};
};

} // namespace AetherSDR
