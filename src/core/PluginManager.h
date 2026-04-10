#pragma once

#include <QObject>
#include <QVector>
#include <QString>

namespace AetherSDR {

class IAmpPlugin;

// Scans ~/.config/AetherSDR/plugins/amp/ at startup and loads .so/.dll files
// that implement IAmpPlugin via QPluginLoader.
//
// Plugin crashes in onFrequencyChanged / onPttChanged are caught by try/catch
// and result in a warning log — the bad plugin is NOT automatically disabled
// (it may recover on the next call) but AetherSDR itself is not affected.
class PluginManager : public QObject {
    Q_OBJECT
public:
    explicit PluginManager(QObject* parent = nullptr);
    ~PluginManager() override;

    // Scan the plugin directory and load all valid amp plugins.
    // Safe to call multiple times (re-scans and reloads).
    // Emits pluginsChanged() on completion.
    void loadPlugins();

    const QVector<IAmpPlugin*>& ampPlugins() const { return m_plugins; }
    bool hasPlugins() const { return !m_plugins.isEmpty(); }

    // Dispatch radio state changes to all loaded plugins.
    // Called from the main thread — plugins must not block.
    void dispatchFrequencyChanged(double hz, const QString& mode, int sliceIdx);
    void dispatchPttChanged(bool transmitting);

signals:
    // Emitted after loadPlugins() finishes (even when no plugins are found).
    void pluginsChanged();

private:
    QVector<IAmpPlugin*> m_plugins;
    QVector<QObject*>    m_loaders;  // QPluginLoader* kept alive to own the .so handles
};

} // namespace AetherSDR
