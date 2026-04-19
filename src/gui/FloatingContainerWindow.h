#pragma once

#include <QWidget>

class QLabel;
class QTimer;
class QVBoxLayout;

namespace AetherSDR {

// FloatingContainerWindow — a lightweight top-level window that hosts a
// detached ContainerWidget.  Generalised from FloatingAppletWindow: the
// hosted widget can be a single applet (leaf container) or a group of
// applets (branch container).
//
// The window is non-modal, uses Qt::Tool so it stays with the main window,
// and has a thin custom title bar with a "Dock" button.  Closing the
// window (X button or Dock button) emits dockRequested() so the
// ContainerManager can re-dock the container cleanly.
//
// Geometry persistence is handled via a JSON blob managed by
// ContainerManager — this class provides save/restore helpers that the
// manager calls at the appropriate times.
class FloatingContainerWindow : public QWidget {
    Q_OBJECT

public:
    explicit FloatingContainerWindow(const QString& containerId,
                                     const QString& title,
                                     QWidget* content,
                                     QWidget* parent = nullptr);

    const QString& containerId() const { return m_containerId; }

    // Geometry persistence — called by ContainerManager during
    // serialisation / deserialisation.
    QRect savedGeometry() const;
    void  applySavedGeometry(const QRect& rect);

    // Save current on-screen geometry into the in-memory rect that
    // ContainerManager will later write to JSON.
    void captureGeometry();

    // Show + raise + deferred geometry restore (same 200 ms / 650 ms
    // guard pattern as FloatingAppletWindow).
    void showAndRestore(const QRect& geo);

    // Save geometry + hide.  Use instead of bare hide() so the position
    // is not lost when the window is re-shown later.
    void hideAndCapture();

signals:
    void dockRequested(const QString& containerId);

protected:
    void closeEvent(QCloseEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;
    void moveEvent(QMoveEvent* ev) override;

private:
    void restoreGeometryImpl(const QRect& geo);

    QString      m_containerId;
    QVBoxLayout* m_contentLayout{nullptr};
    QTimer*      m_saveTimer{nullptr};
    QRect        m_savedGeo;               // last captured geometry
    bool         m_restoringGeometry{false};
    bool         m_appShuttingDown{false};
};

} // namespace AetherSDR
