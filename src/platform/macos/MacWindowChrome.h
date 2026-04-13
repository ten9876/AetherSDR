#pragma once

#include <QtGlobal>

class QWidget;

namespace AetherSDR {

struct MacChromeMetrics {
    int leadingInsetPx = 0;
    int titleBarHeightPx = 0;
    bool available = false;
};

#if defined(Q_OS_MAC)
bool applyMacWindowChrome(QWidget* topLevel);
MacChromeMetrics queryMacChromeMetrics(QWidget* topLevel);
#else
inline bool applyMacWindowChrome(QWidget*)
{
    return false;
}

inline MacChromeMetrics queryMacChromeMetrics(QWidget*)
{
    return {};
}
#endif

} // namespace AetherSDR
