#pragma once

#include <QObject>
#include <QKeySequence>
#include <QHash>
#include <QString>

namespace AetherSDR {

/// Cross-platform global hotkey registration.
///
/// On Windows: uses RegisterHotKey() / UnregisterHotKey() + native event filter.
/// On Linux/X11: uses XGrabKey() via Xlib.
/// On macOS: uses Carbon RegisterEventHotKey().
///
/// When the OS does not support global hotkeys (e.g. Wayland without portal),
/// registration silently fails and isSupported() returns false.
class GlobalHotkey : public QObject {
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;

    /// Whether the current platform supports global hotkeys.
    static bool isSupported();

    /// Register a global hotkey for the given action ID.
    /// Returns true on success. Emits activated(actionId) when pressed.
    bool registerHotkey(const QString& actionId, const QKeySequence& key);

    /// Unregister a previously registered hotkey.
    void unregisterHotkey(const QString& actionId);

    /// Unregister all hotkeys.
    void unregisterAll();

    /// Register a key-release callback (needed for PTT Hold).
    /// Only emits released(actionId) — press is still via activated().
    bool registerHoldHotkey(const QString& actionId, const QKeySequence& key);

signals:
    void activated(const QString& actionId);
    void released(const QString& actionId);

private:
    struct Registration {
        QKeySequence key;
        int nativeId;     // platform-specific ID
        bool isHold;      // true if this also tracks key release
    };
    QHash<QString, Registration> m_registrations;
    int m_nextId{1};

    // Platform-specific implementation
    class Impl;
    Impl* m_impl;
};

} // namespace AetherSDR
