#include "ShortcutManager.h"
#include "GlobalHotkey.h"
#include "AppSettings.h"
#include <QHash>
#include <QWidget>
#include <QDebug>

namespace AetherSDR {

ShortcutManager::ShortcutManager(QObject* parent)
    : QObject(parent)
    , m_globalHotkey(new GlobalHotkey(this))
{
}

void ShortcutManager::registerAction(const QString& id, const QString& displayName,
                                     const QString& category, const QKeySequence& defaultKey,
                                     std::function<void()> handler)
{
    // Prevent duplicate registration
    for (const auto& a : m_actions) {
        if (a.id == id) {
            qWarning() << "ShortcutManager: duplicate action registration:" << id;
            return;
        }
    }
    m_actions.append({id, displayName, category, defaultKey, defaultKey, std::move(handler),
                      false, false, nullptr});
}

void ShortcutManager::registerHoldAction(const QString& id, const QString& displayName,
                                         const QString& category, const QKeySequence& defaultKey,
                                         std::function<void()> pressHandler,
                                         std::function<void()> releaseHandler)
{
    for (const auto& a : m_actions) {
        if (a.id == id) {
            qWarning() << "ShortcutManager: duplicate action registration:" << id;
            return;
        }
    }
    m_actions.append({id, displayName, category, defaultKey, defaultKey,
                      std::move(pressHandler), false, true, std::move(releaseHandler)});
}

void ShortcutManager::setGlobalEnabled(const QString& actionId, bool enabled)
{
    auto* a = action(actionId);
    if (!a) return;
    a->globalEnabled = enabled;
    saveBindings();
    emit bindingsChanged();
}

bool ShortcutManager::isGlobalEnabled(const QString& actionId) const
{
    for (const auto& a : m_actions) {
        if (a.id == actionId) return a.globalEnabled;
    }
    return false;
}

void ShortcutManager::setBinding(const QString& actionId, const QKeySequence& key)
{
    auto* a = action(actionId);
    if (!a) return;

    // Clear any existing binding on this key (prevent conflicts)
    if (!key.isEmpty()) {
        for (auto& other : m_actions) {
            if (other.id != actionId && other.currentKey == key)
                other.currentKey = QKeySequence();
        }
    }

    a->currentKey = key;
    saveBindings();
    emit bindingsChanged();
}

void ShortcutManager::clearBinding(const QString& actionId)
{
    auto* a = action(actionId);
    if (!a) return;
    a->currentKey = QKeySequence();
    saveBindings();
    emit bindingsChanged();
}

void ShortcutManager::resetToDefaults()
{
    for (auto& a : m_actions)
        a.currentKey = a.defaultKey;
    saveBindings();
    emit bindingsChanged();
}

void ShortcutManager::loadBindings()
{
    auto& s = AppSettings::instance();
    for (auto& a : m_actions) {
        QString key = QString("Shortcut_%1").arg(a.id);
        QString val = s.value(key).toString();
        if (!val.isNull())
            a.currentKey = QKeySequence(val);
        // else keep default

        // Load global hotkey flag
        QString globalKey = QString("ShortcutGlobal_%1").arg(a.id);
        QString globalVal = s.value(globalKey).toString();
        if (!globalVal.isNull())
            a.globalEnabled = (globalVal == "True");
    }

    bool normalized = false;
    QHash<QString, int> ownerByKey;
    for (int i = 0; i < m_actions.size(); ++i) {
        auto& action = m_actions[i];
        if (action.currentKey.isEmpty()) continue;

        const QString keyText = action.currentKey.toString();
        auto it = ownerByKey.find(keyText);
        if (it == ownerByKey.end()) {
            ownerByKey.insert(keyText, i);
            continue;
        }

        auto& incumbent = m_actions[*it];
        const bool incumbentCustomized = incumbent.currentKey != incumbent.defaultKey;
        const bool actionCustomized = action.currentKey != action.defaultKey;

        if (actionCustomized && !incumbentCustomized) {
            qWarning() << "ShortcutManager: clearing duplicate default binding"
                       << incumbent.id << "for key" << keyText
                       << "in favor of customized binding" << action.id;
            incumbent.currentKey = QKeySequence();
            *it = i;
        } else {
            qWarning() << "ShortcutManager: clearing duplicate binding"
                       << action.id << "for key" << keyText
                       << "owned by" << incumbent.id;
            action.currentKey = QKeySequence();
        }
        normalized = true;
    }

    if (normalized)
        saveBindings();
}

void ShortcutManager::saveBindings()
{
    auto& s = AppSettings::instance();
    for (const auto& a : m_actions) {
        s.setValue(QString("Shortcut_%1").arg(a.id), a.currentKey.toString());
        s.setValue(QString("ShortcutGlobal_%1").arg(a.id),
                   a.globalEnabled ? "True" : "False");
    }
    s.save();
}

void ShortcutManager::rebuildShortcuts(QWidget* parent,
                                       std::function<bool()> guardFn)
{
    // Destroy existing shortcuts and global registrations
    qDeleteAll(m_shortcuts);
    m_shortcuts.clear();
    m_globalHotkey->unregisterAll();

    // Disconnect any previous global hotkey signals
    disconnect(m_globalHotkey, nullptr, this, nullptr);

    // Wire global hotkey signals to action dispatch
    connect(m_globalHotkey, &GlobalHotkey::activated, this, [this, guardFn](const QString& actionId) {
        auto* a = action(actionId);
        if (!a || !a->handler) return;
        if (guardFn && !guardFn()) return;
        a->handler();
    });
    connect(m_globalHotkey, &GlobalHotkey::released, this, [this](const QString& actionId) {
        auto* a = action(actionId);
        if (!a || !a->releaseHandler) return;
        a->releaseHandler();
    });

    for (const auto& a : m_actions) {
        if (a.currentKey.isEmpty()) continue;

        // Register global hotkey if enabled and supported
        if (a.globalEnabled && GlobalHotkey::isSupported()) {
            if (a.isHold)
                m_globalHotkey->registerHoldHotkey(a.id, a.currentKey);
            else
                m_globalHotkey->registerHotkey(a.id, a.currentKey);
        }

        // Always create the in-app QShortcut too (for when the window is focused)
        // Skip actions with no handler (hold actions handle press via eventFilter
        // or global hotkey)
        if (!a.handler) continue;

        auto* sc = new QShortcut(a.currentKey, parent);
        sc->setAutoRepeat(false);
        auto handler = a.handler;
        connect(sc, &QShortcut::activated, this, [guardFn, handler]() {
            if (guardFn && !guardFn()) return;
            handler();
        });
        m_shortcuts.append(sc);
    }
}

ShortcutManager::Action* ShortcutManager::action(const QString& id)
{
    for (auto& a : m_actions) {
        if (a.id == id) return &a;
    }
    return nullptr;
}

const ShortcutManager::Action* ShortcutManager::actionForKey(const QKeySequence& key) const
{
    if (key.isEmpty()) return nullptr;
    for (const auto& a : m_actions) {
        if (a.currentKey == key) return &a;
    }
    return nullptr;
}

QString ShortcutManager::conflictCheck(const QKeySequence& key,
                                       const QString& excludeId) const
{
    if (key.isEmpty()) return {};
    for (const auto& a : m_actions) {
        if (a.id != excludeId && a.currentKey == key)
            return a.displayName;
    }
    return {};
}

QStringList ShortcutManager::categories()
{
    return {"Frequency", "Band", "Mode", "TX", "Audio", "Slice",
            "Filter", "Tuning", "DSP", "AGC", "Display", "RIT/XIT"};
}

} // namespace AetherSDR
