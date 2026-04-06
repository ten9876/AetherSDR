#include "GlobalHotkey.h"
#include <QDebug>

#if defined(Q_OS_WIN)
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <windows.h>
#elif defined(Q_OS_MAC)
#include <Carbon/Carbon.h>
#include <QMap>
#elif defined(Q_OS_LINUX)
#include <QGuiApplication>
// X11 global hotkeys — only available under X11, not Wayland
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QNativeInterface>
#endif
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <xcb/xcb.h>
#endif

namespace AetherSDR {

// ─── Platform helpers: Qt key → native key/modifier mapping ────────────────

#if defined(Q_OS_WIN)

static UINT qtKeyToVk(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return 'A' + (key - Qt::Key_A);
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return '0' + (key - Qt::Key_0);
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        return VK_F1 + (key - Qt::Key_F1);
    switch (key) {
    case Qt::Key_Space:   return VK_SPACE;
    case Qt::Key_Return:  return VK_RETURN;
    case Qt::Key_Enter:   return VK_RETURN;
    case Qt::Key_Escape:  return VK_ESCAPE;
    case Qt::Key_Tab:     return VK_TAB;
    case Qt::Key_Up:      return VK_UP;
    case Qt::Key_Down:    return VK_DOWN;
    case Qt::Key_Left:    return VK_LEFT;
    case Qt::Key_Right:   return VK_RIGHT;
    case Qt::Key_Home:    return VK_HOME;
    case Qt::Key_End:     return VK_END;
    case Qt::Key_PageUp:  return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Delete:  return VK_DELETE;
    case Qt::Key_Insert:  return VK_INSERT;
    case Qt::Key_Minus:   return VK_OEM_MINUS;
    case Qt::Key_Plus:
    case Qt::Key_Equal:   return VK_OEM_PLUS;
    case Qt::Key_BracketLeft:  return VK_OEM_4;
    case Qt::Key_BracketRight: return VK_OEM_6;
    case Qt::Key_Comma:   return VK_OEM_COMMA;
    case Qt::Key_Period:  return VK_OEM_PERIOD;
    default:              return 0;
    }
}

static UINT qtModsToWin(Qt::KeyboardModifiers mods)
{
    UINT m = MOD_NOREPEAT;
    if (mods & Qt::ControlModifier) m |= MOD_CONTROL;
    if (mods & Qt::AltModifier)     m |= MOD_ALT;
    if (mods & Qt::ShiftModifier)   m |= MOD_SHIFT;
    if (mods & Qt::MetaModifier)    m |= MOD_WIN;
    return m;
}

// Static pointer for the low-level keyboard hook callback
static GlobalHotkey::Impl* s_winImpl = nullptr;

class GlobalHotkey::Impl : public QAbstractNativeEventFilter {
public:
    GlobalHotkey* owner;
    HHOOK m_keyboardHook{nullptr};
    struct HoldKeyInfo { UINT vk; bool pressed; };
    QMap<int, HoldKeyInfo> m_holdKeys;  // nativeId → VK + state

    explicit Impl(GlobalHotkey* o) : owner(o) {
        s_winImpl = this;
        QCoreApplication::instance()->installNativeEventFilter(this);
    }
    ~Impl() override {
        if (m_keyboardHook) {
            UnhookWindowsHookEx(m_keyboardHook);
            m_keyboardHook = nullptr;
        }
        s_winImpl = nullptr;
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }

    static LRESULT CALLBACK llKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
        if (nCode >= 0 && s_winImpl) {
            auto* kbs = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
            for (auto it = s_winImpl->m_holdKeys.begin();
                 it != s_winImpl->m_holdKeys.end(); ++it) {
                if (kbs->vkCode == it->vk) {
                    if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                        if (it->pressed) {
                            it->pressed = false;
                            // Find action ID
                            for (auto rit = s_winImpl->owner->m_registrations.cbegin();
                                 rit != s_winImpl->owner->m_registrations.cend(); ++rit) {
                                if (rit->nativeId == it.key() && rit->isHold) {
                                    emit s_winImpl->owner->released(rit.key());
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        return CallNextHookEx(nullptr, nCode, wParam, lParam);
    }

    void ensureHook() {
        if (!m_keyboardHook && !m_holdKeys.isEmpty()) {
            m_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, llKeyboardProc,
                                                GetModuleHandleW(nullptr), 0);
        }
    }

    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override
    {
        Q_UNUSED(result);
        if (eventType != "windows_generic_MSG") return false;
        auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_HOTKEY) {
            int id = static_cast<int>(msg->wParam);
            for (auto it = owner->m_registrations.cbegin();
                 it != owner->m_registrations.cend(); ++it) {
                if (it->nativeId == id) {
                    emit owner->activated(it.key());
                    // Track press state for hold keys
                    auto hIt = m_holdKeys.find(id);
                    if (hIt != m_holdKeys.end())
                        hIt->pressed = true;
                    return true;
                }
            }
        }
        return false;
    }

    bool registerKey(int id, const QKeySequence& key) {
        if (key.isEmpty()) return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        auto combined = key[0].toCombined();
#else
        int combined = key[0];
#endif
        int qtKey = combined & ~Qt::KeyboardModifierMask;
        auto mods = Qt::KeyboardModifiers(combined & Qt::KeyboardModifierMask);
        UINT vk = qtKeyToVk(qtKey);
        if (vk == 0) return false;
        UINT winMods = qtModsToWin(mods);
        return RegisterHotKey(nullptr, id, winMods, vk) != 0;
    }

    bool registerHoldKey(int id, const QKeySequence& key) {
        if (!registerKey(id, key)) return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        auto combined = key[0].toCombined();
#else
        int combined = key[0];
#endif
        int qtKey = combined & ~Qt::KeyboardModifierMask;
        UINT vk = qtKeyToVk(qtKey);
        m_holdKeys.insert(id, { vk, false });
        ensureHook();
        return true;
    }

    void unregisterKey(int id) {
        UnregisterHotKey(nullptr, id);
        m_holdKeys.remove(id);
        if (m_holdKeys.isEmpty() && m_keyboardHook) {
            UnhookWindowsHookEx(m_keyboardHook);
            m_keyboardHook = nullptr;
        }
    }
};

bool GlobalHotkey::isSupported() { return true; }

#elif defined(Q_OS_MAC)

// macOS Carbon EventHotKey
static OSType kHotkeySignature = 'ASDR';

static UInt32 qtKeyToMacVk(int key)
{
    // Common virtual keycodes for macOS
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        // macOS keycodes are not sequential like ASCII, use a lookup
        static const UInt32 map[] = {
            kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E,
            kVK_ANSI_F, kVK_ANSI_G, kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J,
            kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N, kVK_ANSI_O,
            kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T,
            kVK_ANSI_U, kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y,
            kVK_ANSI_Z
        };
        return map[key - Qt::Key_A];
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        static const UInt32 map[] = {
            kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
            kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9
        };
        return map[key - Qt::Key_0];
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        static const UInt32 map[] = {
            kVK_F1, kVK_F2, kVK_F3, kVK_F4, kVK_F5, kVK_F6,
            kVK_F7, kVK_F8, kVK_F9, kVK_F10, kVK_F11, kVK_F12
        };
        return map[key - Qt::Key_F1];
    }
    switch (key) {
    case Qt::Key_Space:    return kVK_Space;
    case Qt::Key_Return:   return kVK_Return;
    case Qt::Key_Tab:      return kVK_Tab;
    case Qt::Key_Escape:   return kVK_Escape;
    case Qt::Key_Up:       return kVK_UpArrow;
    case Qt::Key_Down:     return kVK_DownArrow;
    case Qt::Key_Left:     return kVK_LeftArrow;
    case Qt::Key_Right:    return kVK_RightArrow;
    case Qt::Key_Minus:    return kVK_ANSI_Minus;
    case Qt::Key_Plus:
    case Qt::Key_Equal:    return kVK_ANSI_Equal;
    case Qt::Key_BracketLeft:  return kVK_ANSI_LeftBracket;
    case Qt::Key_BracketRight: return kVK_ANSI_RightBracket;
    case Qt::Key_Comma:    return kVK_ANSI_Comma;
    case Qt::Key_Period:   return kVK_ANSI_Period;
    default:               return 0xFFFF;
    }
}

static UInt32 qtModsToMac(Qt::KeyboardModifiers mods)
{
    UInt32 m = 0;
    if (mods & Qt::ControlModifier) m |= cmdKey;     // Qt::Control = Cmd on Mac
    if (mods & Qt::AltModifier)     m |= optionKey;
    if (mods & Qt::ShiftModifier)   m |= shiftKey;
    if (mods & Qt::MetaModifier)    m |= controlKey;  // Qt::Meta = Ctrl on Mac
    return m;
}

static EventHandlerRef s_macHandler = nullptr;
static GlobalHotkey* s_macOwner = nullptr;

class GlobalHotkey::Impl {
public:
    GlobalHotkey* owner;
    QMap<int, EventHotKeyRef> refs;

    static OSStatus macHotkeyCallback(EventHandlerCallRef, EventRef event, void*)
    {
        EventHotKeyID hkId;
        GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID,
                          nullptr, sizeof(hkId), nullptr, &hkId);
        if (s_macOwner) {
            int nativeId = static_cast<int>(hkId.id);
            for (auto it = s_macOwner->m_registrations.cbegin();
                 it != s_macOwner->m_registrations.cend(); ++it) {
                if (it->nativeId == nativeId) {
                    emit s_macOwner->activated(it.key());
                    break;
                }
            }
        }
        return noErr;
    }

    explicit Impl(GlobalHotkey* o) : owner(o) {
        s_macOwner = o;
        if (!s_macHandler) {
            EventTypeSpec spec = { kEventClassKeyboard, kEventHotKeyPressed };
            InstallApplicationEventHandler(&macHotkeyCallback, 1, &spec,
                                           nullptr, &s_macHandler);
        }
    }
    ~Impl() {
        for (auto ref : refs)
            UnregisterEventHotKey(ref);
        if (s_macOwner == owner)
            s_macOwner = nullptr;
    }

    bool registerKey(int id, const QKeySequence& key) {
        if (key.isEmpty()) return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        auto combined = key[0].toCombined();
#else
        int combined = key[0];
#endif
        int qtKey = combined & ~Qt::KeyboardModifierMask;
        auto mods = Qt::KeyboardModifiers(combined & Qt::KeyboardModifierMask);
        UInt32 vk = qtKeyToMacVk(qtKey);
        if (vk == 0xFFFF) return false;
        UInt32 macMods = qtModsToMac(mods);

        EventHotKeyID hkId = { kHotkeySignature, static_cast<UInt32>(id) };
        EventHotKeyRef ref;
        OSStatus err = RegisterEventHotKey(vk, macMods, hkId,
                                           GetApplicationEventTarget(),
                                           0, &ref);
        if (err != noErr) return false;
        refs.insert(id, ref);
        return true;
    }

    void unregisterKey(int id) {
        auto it = refs.find(id);
        if (it != refs.end()) {
            UnregisterEventHotKey(*it);
            refs.erase(it);
        }
    }
};

bool GlobalHotkey::isSupported() { return true; }

#elif defined(Q_OS_LINUX)

// Linux X11 implementation — only works under X11, not Wayland
static Display* getX11Display()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    auto* x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    if (x11App)
        return x11App->display();
#endif
    return nullptr;
}

static KeyCode qtKeyToX11(Display* dpy, int key)
{
    KeySym sym = NoSymbol;
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        sym = XK_a + (key - Qt::Key_A);
    else if (key >= Qt::Key_0 && key <= Qt::Key_9)
        sym = XK_0 + (key - Qt::Key_0);
    else if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        sym = XK_F1 + (key - Qt::Key_F1);
    else {
        switch (key) {
        case Qt::Key_Space:   sym = XK_space; break;
        case Qt::Key_Return:  sym = XK_Return; break;
        case Qt::Key_Tab:     sym = XK_Tab; break;
        case Qt::Key_Escape:  sym = XK_Escape; break;
        case Qt::Key_Up:      sym = XK_Up; break;
        case Qt::Key_Down:    sym = XK_Down; break;
        case Qt::Key_Left:    sym = XK_Left; break;
        case Qt::Key_Right:   sym = XK_Right; break;
        case Qt::Key_Minus:   sym = XK_minus; break;
        case Qt::Key_Plus:    sym = XK_plus; break;
        case Qt::Key_Equal:   sym = XK_equal; break;
        case Qt::Key_Comma:   sym = XK_comma; break;
        case Qt::Key_Period:  sym = XK_period; break;
        case Qt::Key_BracketLeft:  sym = XK_bracketleft; break;
        case Qt::Key_BracketRight: sym = XK_bracketright; break;
        default: return 0;
        }
    }
    return XKeysymToKeycode(dpy, sym);
}

static unsigned int qtModsToX11(Qt::KeyboardModifiers mods)
{
    unsigned int m = 0;
    if (mods & Qt::ControlModifier) m |= ControlMask;
    if (mods & Qt::AltModifier)     m |= Mod1Mask;
    if (mods & Qt::ShiftModifier)   m |= ShiftMask;
    if (mods & Qt::MetaModifier)    m |= Mod4Mask;
    return m;
}

class GlobalHotkey::Impl : public QAbstractNativeEventFilter {
public:
    GlobalHotkey* owner;
    Display* dpy;
    struct GrabInfo { KeyCode code; unsigned int mods; };
    QMap<int, GrabInfo> grabs;

    explicit Impl(GlobalHotkey* o) : owner(o), dpy(getX11Display()) {
        if (dpy)
            QCoreApplication::instance()->installNativeEventFilter(this);
    }
    ~Impl() override {
        if (dpy)
            QCoreApplication::instance()->removeNativeEventFilter(this);
    }

    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override
    {
        Q_UNUSED(result);
        if (eventType != "xcb_generic_event_t" || !message) return false;
        auto* ev = static_cast<xcb_generic_event_t*>(message);
        uint8_t type = ev->response_type & ~0x80;
        if (type == XCB_KEY_PRESS) {
            auto* kp = reinterpret_cast<xcb_key_press_event_t*>(ev);
            // Strip NumLock/CapsLock/ScrollLock from state
            unsigned int cleanState = kp->state & (ControlMask | Mod1Mask | ShiftMask | Mod4Mask);
            for (auto it = grabs.cbegin(); it != grabs.cend(); ++it) {
                if (kp->detail == it->code && cleanState == it->mods) {
                    // Find action ID from native ID
                    for (auto rit = owner->m_registrations.cbegin();
                         rit != owner->m_registrations.cend(); ++rit) {
                        if (rit->nativeId == it.key()) {
                            emit owner->activated(rit.key());
                            return true;
                        }
                    }
                }
            }
        } else if (type == XCB_KEY_RELEASE) {
            auto* kr = reinterpret_cast<xcb_key_release_event_t*>(ev);
            unsigned int cleanState = kr->state & (ControlMask | Mod1Mask | ShiftMask | Mod4Mask);
            for (auto it = grabs.cbegin(); it != grabs.cend(); ++it) {
                if (kr->detail == it->code && cleanState == it->mods) {
                    for (auto rit = owner->m_registrations.cbegin();
                         rit != owner->m_registrations.cend(); ++rit) {
                        if (rit->nativeId == it.key() && rit->isHold) {
                            emit owner->released(rit.key());
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    }

    bool registerKey(int id, const QKeySequence& key) {
        if (!dpy || key.isEmpty()) return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        auto combined = key[0].toCombined();
#else
        int combined = key[0];
#endif
        int qtKey = combined & ~Qt::KeyboardModifierMask;
        auto mods = Qt::KeyboardModifiers(combined & Qt::KeyboardModifierMask);
        KeyCode code = qtKeyToX11(dpy, qtKey);
        if (code == 0) return false;
        unsigned int x11Mods = qtModsToX11(mods);

        Window root = DefaultRootWindow(dpy);
        // Grab with and without NumLock/CapsLock/ScrollLock
        unsigned int modMasks[] = { 0, Mod2Mask, LockMask, Mod2Mask | LockMask };
        for (unsigned int extra : modMasks) {
            XGrabKey(dpy, code, x11Mods | extra, root, True,
                     GrabModeAsync, GrabModeAsync);
        }
        XFlush(dpy);
        grabs.insert(id, { code, x11Mods });
        return true;
    }

    void unregisterKey(int id) {
        if (!dpy) return;
        auto it = grabs.find(id);
        if (it == grabs.end()) return;
        Window root = DefaultRootWindow(dpy);
        unsigned int modMasks[] = { 0, Mod2Mask, LockMask, Mod2Mask | LockMask };
        for (unsigned int extra : modMasks) {
            XUngrabKey(dpy, it->code, it->mods | extra, root);
        }
        XFlush(dpy);
        grabs.erase(it);
    }
};

bool GlobalHotkey::isSupported()
{
    return getX11Display() != nullptr;
}

#else

// Unsupported platform stub
class GlobalHotkey::Impl {
public:
    GlobalHotkey* owner;
    explicit Impl(GlobalHotkey* o) : owner(o) {}
    bool registerKey(int, const QKeySequence&) { return false; }
    void unregisterKey(int) {}
};

bool GlobalHotkey::isSupported() { return false; }

#endif

// ─── GlobalHotkey public interface ─────────────────────────────────────────

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent)
    , m_impl(new Impl(this))
{
}

GlobalHotkey::~GlobalHotkey()
{
    unregisterAll();
    delete m_impl;
}

bool GlobalHotkey::registerHotkey(const QString& actionId, const QKeySequence& key)
{
    if (m_registrations.contains(actionId))
        unregisterHotkey(actionId);

    int id = m_nextId++;
    if (!m_impl->registerKey(id, key)) {
        qWarning() << "GlobalHotkey: failed to register" << actionId
                    << "for key" << key.toString();
        return false;
    }
    m_registrations.insert(actionId, { key, id, false });
    qDebug() << "GlobalHotkey: registered" << actionId << "=" << key.toString();
    return true;
}

bool GlobalHotkey::registerHoldHotkey(const QString& actionId, const QKeySequence& key)
{
    if (m_registrations.contains(actionId))
        unregisterHotkey(actionId);

    int id = m_nextId++;
#if defined(Q_OS_WIN)
    // Windows needs special hold key handling with low-level hook for release
    if (!m_impl->registerHoldKey(id, key)) {
#else
    if (!m_impl->registerKey(id, key)) {
#endif
        qWarning() << "GlobalHotkey: failed to register hold hotkey" << actionId
                    << "for key" << key.toString();
        return false;
    }
    m_registrations.insert(actionId, { key, id, true });
    qDebug() << "GlobalHotkey: registered hold hotkey" << actionId << "=" << key.toString();
    return true;
}

void GlobalHotkey::unregisterHotkey(const QString& actionId)
{
    auto it = m_registrations.find(actionId);
    if (it == m_registrations.end()) return;
    m_impl->unregisterKey(it->nativeId);
    m_registrations.erase(it);
}

void GlobalHotkey::unregisterAll()
{
    for (auto it = m_registrations.cbegin(); it != m_registrations.cend(); ++it)
        m_impl->unregisterKey(it->nativeId);
    m_registrations.clear();
}

} // namespace AetherSDR
