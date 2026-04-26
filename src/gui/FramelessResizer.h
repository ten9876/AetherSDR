#pragma once

#include <QObject>
#include <Qt>

class QWidget;

namespace AetherSDR {

// Adds all-edge resize to a Qt::FramelessWindowHint top-level QWidget using
// QWindow::startSystemResize() (the same compositor-managed path already used
// for startSystemMove in TitleBar / ContainerWidget / PanadapterApplet).
//
// Install once after construction; the helper then monitors the window AND
// every child widget so resize intent is detected regardless of which child
// the cursor happens to be over at the moment.  New children added later are
// picked up via QEvent::ChildAdded.
//
// Usage:
//   FramelessResizer::install(this);          // from a QWidget constructor
//   FramelessResizer::install(win, 6);        // explicit margin
class FramelessResizer : public QObject {
    Q_OBJECT
public:
    static void install(QWidget* window, int margin = 6);

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    explicit FramelessResizer(QWidget* window, int margin);
    void attachToWidget(QWidget* w);
    Qt::Edges edgesAt(const QPoint& windowPos) const;
    void applyCursor(Qt::Edges edges);

    QWidget* m_window{nullptr};
    int      m_margin{6};
};

} // namespace AetherSDR
