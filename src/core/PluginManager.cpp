#include "PluginManager.h"
#include "plugins/IAmpPlugin.h"

#include <QDir>
#include <QPluginLoader>
#include <QStandardPaths>
#include <QDebug>

namespace AetherSDR {

PluginManager::PluginManager(QObject* parent) : QObject(parent) {}

PluginManager::~PluginManager()
{
    m_plugins.clear();
    // Unload in reverse order so dependencies are released last
    for (int i = m_loaders.size() - 1; i >= 0; --i) {
        auto* loader = qobject_cast<QPluginLoader*>(m_loaders[i]);
        if (loader) loader->unload();
        delete m_loaders[i];
    }
    m_loaders.clear();
}

void PluginManager::loadPlugins()
{
    // Clear any previously loaded plugins before re-scanning
    m_plugins.clear();
    for (int i = m_loaders.size() - 1; i >= 0; --i) {
        auto* loader = qobject_cast<QPluginLoader*>(m_loaders[i]);
        if (loader) loader->unload();
        delete m_loaders[i];
    }
    m_loaders.clear();

    // ~/.config/AetherSDR/plugins/amp/
    const QString configBase =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir pluginDir(configBase + "/plugins/amp");

    if (!pluginDir.exists()) {
        qDebug() << "PluginManager: plugin directory does not exist:"
                 << pluginDir.absolutePath()
                 << "— no amp plugins loaded";
        emit pluginsChanged();
        return;
    }

    const QStringList entries = pluginDir.entryList(QDir::Files);
    for (const QString& fileName : entries) {
        // Only attempt files that look like shared libraries
        if (!fileName.endsWith(".so", Qt::CaseInsensitive) &&
            !fileName.endsWith(".dll", Qt::CaseInsensitive) &&
            !fileName.endsWith(".dylib", Qt::CaseInsensitive)) {
            continue;
        }

        const QString path = pluginDir.absoluteFilePath(fileName);
        auto* loader = new QPluginLoader(path, this);

        QObject* instance = nullptr;
        try {
            instance = loader->instance();
        } catch (...) {
            qWarning() << "PluginManager: exception while loading plugin" << path
                       << "— skipped";
            loader->unload();
            delete loader;
            continue;
        }

        if (!instance) {
            qWarning() << "PluginManager: failed to load" << path
                       << "—" << loader->errorString();
            delete loader;
            continue;
        }

        auto* plugin = qobject_cast<IAmpPlugin*>(instance);
        if (!plugin) {
            qWarning() << "PluginManager:" << path
                       << "does not implement IAmpPlugin — skipped";
            loader->unload();
            delete loader;
            continue;
        }

        // Per-plugin writable config directory
        const QString pluginConfig =
            configBase + "/plugins/amp/config/" + plugin->pluginName();
        QDir().mkpath(pluginConfig);

        bool ok = false;
        try {
            ok = plugin->initialize(pluginConfig);
        } catch (...) {
            qWarning() << "PluginManager: plugin" << plugin->pluginName()
                       << "threw during initialize() — disabled";
            loader->unload();
            delete loader;
            continue;
        }

        if (!ok) {
            qWarning() << "PluginManager: plugin" << plugin->pluginName()
                       << "returned false from initialize() — disabled";
            loader->unload();
            delete loader;
            continue;
        }

        qDebug() << "PluginManager: loaded amp plugin" << plugin->pluginName()
                 << "v" + plugin->pluginVersion()
                 << "for" << plugin->ampModel();
        m_plugins.append(plugin);
        m_loaders.append(loader);
    }

    if (m_plugins.isEmpty())
        qDebug() << "PluginManager: no amp plugins found in" << pluginDir.absolutePath();
    else
        qDebug() << "PluginManager:" << m_plugins.size() << "amp plugin(s) loaded";

    emit pluginsChanged();
}

void PluginManager::dispatchFrequencyChanged(double hz, const QString& mode, int sliceIdx)
{
    for (auto* plugin : m_plugins) {
        try {
            plugin->onFrequencyChanged(hz, mode, sliceIdx);
        } catch (...) {
            qWarning() << "PluginManager: plugin threw in onFrequencyChanged — ignoring";
        }
    }
}

void PluginManager::dispatchPttChanged(bool transmitting)
{
    for (auto* plugin : m_plugins) {
        try {
            plugin->onPttChanged(transmitting);
        } catch (...) {
            qWarning() << "PluginManager: plugin threw in onPttChanged — ignoring";
        }
    }
}

} // namespace AetherSDR
