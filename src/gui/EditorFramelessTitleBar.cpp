#include "EditorFramelessTitleBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QWindow>

namespace AetherSDR {

EditorFramelessTitleBar::EditorFramelessTitleBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(20);
    setStyleSheet("background: #08121d;");

    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(8, 0, 4, 0);
    row->setSpacing(2);

    m_titleLbl = new QLabel(this);
    m_titleLbl->setStyleSheet(
        "QLabel { color: #d7e7f2; font-size: 11px; "
        "font-weight: bold; background: transparent; }");
    m_titleLbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    row->addWidget(m_titleLbl);
    row->addStretch();

    const QString lblStyle = QStringLiteral(
        "QLabel { color: #8aa8c0; font-size: 14px; padding: 0 8px; "
        "border-radius: 3px; }"
        "QLabel:hover { color: #ffffff; background: #203040; }");
    const QString closeStyle = QStringLiteral(
        "QLabel { color: #8aa8c0; font-size: 14px; padding: 0 8px; "
        "border-radius: 3px; }"
        "QLabel:hover { color: #ffffff; background: #cc2030; }");

    m_minLbl = new QLabel(QString::fromUtf8("\xe2\x80\x94"), this);  // —
    m_minLbl->setFixedHeight(20);
    m_minLbl->setAlignment(Qt::AlignCenter);
    m_minLbl->setCursor(Qt::PointingHandCursor);
    m_minLbl->setToolTip("Minimize");
    m_minLbl->setStyleSheet(lblStyle);
    m_minLbl->installEventFilter(this);
    row->addWidget(m_minLbl);

    m_maxLbl = new QLabel(QString::fromUtf8("\xe2\x96\xa1"), this);  // □
    m_maxLbl->setFixedHeight(20);
    m_maxLbl->setAlignment(Qt::AlignCenter);
    m_maxLbl->setCursor(Qt::PointingHandCursor);
    m_maxLbl->setToolTip("Maximize");
    m_maxLbl->setStyleSheet(lblStyle);
    m_maxLbl->installEventFilter(this);
    row->addWidget(m_maxLbl);

    m_closeLbl = new QLabel(QString::fromUtf8("\xe2\x9c\x95"), this); // ✕
    m_closeLbl->setFixedHeight(20);
    m_closeLbl->setAlignment(Qt::AlignCenter);
    m_closeLbl->setCursor(Qt::PointingHandCursor);
    m_closeLbl->setToolTip("Close");
    m_closeLbl->setStyleSheet(closeStyle);
    m_closeLbl->installEventFilter(this);
    row->addWidget(m_closeLbl);
}

void EditorFramelessTitleBar::setTitleText(const QString& text)
{
    if (m_titleLbl) m_titleLbl->setText(text);
}

void EditorFramelessTitleBar::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        if (auto* w = window()) {
            if (auto* h = w->windowHandle()) {
                h->startSystemMove();
                ev->accept();
                return;
            }
        }
    }
    QWidget::mousePressEvent(ev);
}

void EditorFramelessTitleBar::mouseDoubleClickEvent(QMouseEvent* ev)
{
    if (ev->button() == Qt::LeftButton) {
        if (auto* w = window()) {
            if (w->isMaximized()) w->showNormal();
            else                  w->showMaximized();
            ev->accept();
            return;
        }
    }
    QWidget::mouseDoubleClickEvent(ev);
}

bool EditorFramelessTitleBar::eventFilter(QObject* obj, QEvent* ev)
{
    if (ev->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton) {
            if (obj == m_minLbl) {
                if (auto* w = window()) w->showMinimized();
                return true;
            }
            if (obj == m_maxLbl) {
                if (auto* w = window()) {
                    if (w->isMaximized()) w->showNormal();
                    else                  w->showMaximized();
                }
                return true;
            }
            if (obj == m_closeLbl) {
                if (auto* w = window()) w->close();
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, ev);
}

} // namespace AetherSDR
