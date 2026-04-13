#include "platform/macos/MacWindowChrome.h"

#ifdef Q_OS_MAC

#include <AppKit/AppKit.h>

#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cmath>

namespace AetherSDR {
namespace {

NSWindow* nativeWindowForWidget(QWidget* topLevel)
{
    if (!topLevel)
        return nil;

    QWidget* window = topLevel->window();
    if (!window)
        return nil;

    // Qt top-level widgets are backed by an NSView on macOS; keep using the
    // native titled window so AppKit still owns resize, fullscreen, and the
    // traffic-light controls instead of emulating them with a frameless Qt window.
    auto nativeViewId = window->winId();
    if (!nativeViewId)
        return nil;

    auto* nsView = reinterpret_cast<NSView*>(nativeViewId);
    if (!nsView)
        return nil;

    return nsView.window;
}

MacChromeMetrics metricsForWindow(NSWindow* nsWindow)
{
    MacChromeMetrics metrics;
    if (!nsWindow)
        return metrics;

    NSView* contentView = nsWindow.contentView;
    CGFloat clusterRight = 0.0;
    bool foundButtons = false;

    for (NSWindowButton buttonType : {
             NSWindowCloseButton,
             NSWindowMiniaturizeButton,
             NSWindowZoomButton,
         }) {
        NSButton* button = [nsWindow standardWindowButton:buttonType];
        if (!button)
            continue;

        NSRect buttonFrame = button.frame;
        if (button.superview && contentView && button.superview != contentView)
            buttonFrame = [button.superview convertRect:buttonFrame toView:contentView];

        clusterRight = std::max(clusterRight, NSMaxX(buttonFrame));
        foundButtons = true;
    }

    if (foundButtons)
        metrics.leadingInsetPx = static_cast<int>(std::ceil(clusterRight + 10.0));

    CGFloat titleBarHeight = 0.0;
    if (contentView) {
        const NSRect contentBounds = contentView.bounds;
        const NSRect layoutRect = nsWindow.contentLayoutRect;
        titleBarHeight = std::max<CGFloat>(0.0, NSMaxY(contentBounds) - NSMaxY(layoutRect));
        if (titleBarHeight <= 0.0)
            titleBarHeight = std::max<CGFloat>(0.0, NSHeight(contentBounds) - NSHeight(layoutRect));
    }

    if (titleBarHeight <= 0.0) {
        const NSRect frameRect = nsWindow.frame;
        const NSRect contentRect = [nsWindow contentRectForFrameRect:frameRect];
        titleBarHeight = std::max<CGFloat>(0.0, NSHeight(frameRect) - NSHeight(contentRect));
    }

    if (titleBarHeight > 0.0)
        metrics.titleBarHeightPx = static_cast<int>(std::ceil(titleBarHeight));

    metrics.available = foundButtons || metrics.titleBarHeightPx > 0;
    return metrics;
}

} // namespace

bool applyMacWindowChrome(QWidget* topLevel)
{
    NSWindow* nsWindow = nativeWindowForWidget(topLevel);
    if (!nsWindow)
        return false;

    nsWindow.titlebarAppearsTransparent = YES;
    nsWindow.titleVisibility = NSWindowTitleHidden;
    nsWindow.styleMask |= NSWindowStyleMaskFullSizeContentView;
    return true;
}

MacChromeMetrics queryMacChromeMetrics(QWidget* topLevel)
{
    return metricsForWindow(nativeWindowForWidget(topLevel));
}

bool toggleMacWindowZoom(QWidget* topLevel)
{
    NSWindow* nsWindow = nativeWindowForWidget(topLevel);
    if (!nsWindow)
        return false;

    // Use AppKit's zoom behavior so double-clicking the custom titlebar strip
    // matches the native titled window action without entering fullscreen.
    [nsWindow zoom:nil];
    return true;
}

} // namespace AetherSDR

#endif
