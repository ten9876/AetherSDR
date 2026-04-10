#pragma once

// AmpStubPlugin — minimal IAmpPlugin template for third-party developers (#1109).
//
// Build:
//   mkdir build && cd build
//   cmake .. -DAETHERSDR_INCLUDE=<path-to-AetherSDR/src>
//   make
//
// Install:
//   cp libampstub.so ~/.config/AetherSDR/plugins/amp/
//
// Then restart AetherSDR — the plugin appears in the AMP applet and in
// Settings → Radio Setup → Peripherals → Amplifier Plugins.

#include <QObject>
#include "../../../src/core/plugins/IAmpPlugin.h"

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>

class AmpStubPlugin : public QObject, public AetherSDR::IAmpPlugin {
    Q_OBJECT
    Q_INTERFACES(AetherSDR::IAmpPlugin)
    Q_PLUGIN_METADATA(IID "org.aethersdr.IAmpPlugin/1.0" FILE "ampstub.json")

public:
    explicit AmpStubPlugin(QObject* parent = nullptr) : QObject(parent) {}

    // IAmpPlugin interface
    bool initialize(const QString& configPath) override;
    void onFrequencyChanged(double hz, const QString& mode, int sliceIdx) override;
    void onPttChanged(bool transmitting) override;
    QWidget* createWidget(QWidget* parent) override;
    QString pluginName()    const override { return QStringLiteral("AmpStub"); }
    QString pluginVersion() const override { return QStringLiteral("1.0"); }
    QString ampModel()      const override { return QStringLiteral("Stub (template)"); }

private:
    QLabel* m_statusLabel{nullptr};
};
