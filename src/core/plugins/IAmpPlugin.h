#pragma once

// IAmpPlugin.h — stable ABI interface for third-party amplifier control plugins (#1109).
//
// Plugins are shared libraries (.so on Linux, .dll on Windows) placed in
//   ~/.config/AetherSDR/plugins/amp/
// Each must implement this interface and register via Q_PLUGIN_METADATA.
//
// Minimal plugin skeleton:
//
//   class MyAmpPlugin : public QObject, public AetherSDR::IAmpPlugin {
//       Q_OBJECT
//       Q_INTERFACES(AetherSDR::IAmpPlugin)
//       Q_PLUGIN_METADATA(IID "org.aethersdr.IAmpPlugin/1.0" FILE "myamp.json")
//   public:
//       bool initialize(const QString& configPath) override { return true; }
//       void onFrequencyChanged(double hz, const QString& mode, int sliceIdx) override {}
//       void onPttChanged(bool transmitting) override {}
//       QWidget* createWidget(QWidget* parent) override { return new QWidget(parent); }
//       QString pluginName()    const override { return "MyAmp"; }
//       QString pluginVersion() const override { return "1.0"; }
//       QString ampModel()      const override { return "My Amplifier Model"; }
//   };

#include <QtPlugin>
#include <QString>

class QWidget;

namespace AetherSDR {

// Optional status information that a plugin may surface to AetherSDR.
// Plugins can emit a statusUpdate(AmpStatus) signal (not part of the interface)
// to have faultString shown as a status-bar notification.
struct AmpStatus {
    float   forwardPowerW{0.0f};
    float   swrFloat{1.0f};
    QString faultString;
    QString bandName;
};

// Abstract plugin interface.  Derive from both QObject and IAmpPlugin:
//   class MyPlugin : public QObject, public IAmpPlugin { ... };
class IAmpPlugin {
public:
    virtual ~IAmpPlugin() = default;

    // Called once after the plugin is loaded.  configPath is a per-plugin
    // writable directory (~/.config/AetherSDR/plugins/amp/config/<pluginName>/).
    // Return false to abort loading (plugin will be unloaded).
    virtual bool initialize(const QString& configPath) = 0;

    // Dispatched on every slice frequency change, within 50 ms of the
    // radio status update.  hz is the absolute frequency in Hz.
    virtual void onFrequencyChanged(double hz, const QString& mode, int sliceIdx) = 0;

    // Dispatched when the TX state changes (MOX/PTT transitions).
    virtual void onPttChanged(bool transmitting) = 0;

    // Return a QWidget for the AmpApplet tab.  AetherSDR reparents the
    // widget — do not delete it; it is destroyed when the applet is closed.
    virtual QWidget* createWidget(QWidget* parent) = 0;

    // Human-readable metadata displayed in the Peripherals settings tab.
    virtual QString pluginName()    const = 0;
    virtual QString pluginVersion() const = 0;
    virtual QString ampModel()      const = 0;
};

} // namespace AetherSDR

Q_DECLARE_INTERFACE(AetherSDR::IAmpPlugin, "org.aethersdr.IAmpPlugin/1.0")
