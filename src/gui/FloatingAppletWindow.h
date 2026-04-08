#pragma once

#include <QWidget>

class QLabel;
class QTimer;
class QVBoxLayout;

namespace AetherSDR {

// FloatingAppletWindow — a lightweight top-level window that hosts a single
// detached applet widget. The window is non-modal and has a thin custom
// title bar with a "↩ Dock" button. Closing the window (X or Dock button)
// emits dockRequested() so AppletPanel can re-dock the applet cleanly.
class FloatingAppletWindow : public QWidget {
    Q_OBJECT

public:
    explicit FloatingAppletWindow(const QString& appletId,
                                  const QString& title,
                                  QWidget* applet,
                                  QWidget* parent = nullptr);

    // Restore saved geometry from AppSettings (call after construction)
    void restoreGeometry();

    // Save current geometry to AppSettings (called before docking)
    void saveGeometry();

    // Save geometry + flush + hide.  Use instead of bare hide() so the
    // window position is not lost when the window is re-shown later.
    void hideAndSave();

    // Show + raise + schedule restoreGeometry() after 200 ms so the window
    // manager has time to map the window before we attempt to position it.
    void showAndRestore();

    const QString& appletId() const { return m_appletId; }

signals:
    void dockRequested(const QString& appletId);

protected:
    void closeEvent(QCloseEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;
    void moveEvent(QMoveEvent* ev) override;

private:
    QString      m_appletId;
    QVBoxLayout* m_contentLayout{nullptr};
    QTimer*      m_saveTimer{nullptr};        // debounce geometry saves on resize/move
    bool         m_restoringGeometry{false};  // suppresses debounce during restoreGeometry/showAndRestore
    bool         m_appShuttingDown{false};    // set on aboutToQuit — prevents dockRequested on close
};

} // namespace AetherSDR
