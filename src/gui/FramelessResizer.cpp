#include "FramelessResizer.h"

#include <QChildEvent>
#include <QEvent>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

namespace AetherSDR {

void FramelessResizer::install(QWidget* window, int margin)
{
    auto* r = new FramelessResizer(window, margin);
    r->attachToWidget(window);
}

FramelessResizer::FramelessResizer(QWidget* window, int margin)
    : QObject(window), m_window(window), m_margin(margin)
{
}

void FramelessResizer::attachToWidget(QWidget* w)
{
    w->installEventFilter(this);
    w->setMouseTracking(true);
    for (QObject* child : w->children()) {
        if (QWidget* cw = qobject_cast<QWidget*>(child)) {
            attachToWidget(cw);
        }
    }
}

Qt::Edges FramelessResizer::edgesAt(const QPoint& p) const
{
    const QRect r = m_window->rect();
    Qt::Edges edges;
    if (p.x() <= m_margin)                 edges |= Qt::LeftEdge;
    if (p.x() >= r.width()  - m_margin)    edges |= Qt::RightEdge;
    if (p.y() <= m_margin)                 edges |= Qt::TopEdge;
    if (p.y() >= r.height() - m_margin)    edges |= Qt::BottomEdge;
    return edges;
}

void FramelessResizer::applyCursor(Qt::Edges edges)
{
    if      (edges == (Qt::TopEdge | Qt::LeftEdge))     m_window->setCursor(Qt::SizeFDiagCursor);
    else if (edges == (Qt::TopEdge | Qt::RightEdge))    m_window->setCursor(Qt::SizeBDiagCursor);
    else if (edges == (Qt::BottomEdge | Qt::LeftEdge))  m_window->setCursor(Qt::SizeBDiagCursor);
    else if (edges == (Qt::BottomEdge | Qt::RightEdge)) m_window->setCursor(Qt::SizeFDiagCursor);
    else if (edges & (Qt::LeftEdge | Qt::RightEdge))    m_window->setCursor(Qt::SizeHorCursor);
    else if (edges & (Qt::TopEdge  | Qt::BottomEdge))   m_window->setCursor(Qt::SizeVerCursor);
    else                                                 m_window->unsetCursor();
}

bool FramelessResizer::eventFilter(QObject* obj, QEvent* ev)
{
    // Hands off when the window has native decorations — the OS handles resize.
    if (!(m_window->windowFlags() & Qt::FramelessWindowHint))
        return false;

    switch (ev->type()) {

    case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->buttons() != Qt::NoButton) break;
        QWidget* w = qobject_cast<QWidget*>(obj);
        if (!w) break;
        const QPoint pos = (w == m_window) ? me->pos()
                                           : w->mapTo(m_window, me->pos());
        applyCursor(edgesAt(pos));
        break;
    }

    case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() != Qt::LeftButton) break;
        QWidget* w = qobject_cast<QWidget*>(obj);
        if (!w) break;
        const QPoint pos = (w == m_window) ? me->pos()
                                           : w->mapTo(m_window, me->pos());
        Qt::Edges edges = edgesAt(pos);
        if (edges && m_window->windowHandle()) {
            m_window->windowHandle()->startSystemResize(edges);
            return true;
        }
        break;
    }

    case QEvent::Leave:
        // Only reset when leaving the top-level window itself, not children.
        if (obj == m_window) {
            m_window->unsetCursor();
        }
        break;

    case QEvent::ChildAdded: {
        // Pick up widgets added after install (e.g. container reparented in).
        auto* ce = static_cast<QChildEvent*>(ev);
        if (QWidget* cw = qobject_cast<QWidget*>(ce->child())) {
            attachToWidget(cw);
        }
        break;
    }

    default:
        break;
    }
    return false;
}

} // namespace AetherSDR
