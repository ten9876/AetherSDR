#include "FilterEditorPopup.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QApplication>
#include <QScreen>

namespace AetherSDR {

static const QString kPopupStyle = QStringLiteral(
    "QWidget#FilterEditorPopup { background: rgba(15, 15, 26, 240);"
    "  border: 1px solid #304050; border-radius: 4px; }"
    "QLabel { color: #8090a0; font-size: 11px; border: none; }"
    "QSpinBox { background: #0a0a18; color: #c8d8e8; border: 1px solid #304050;"
    "  border-radius: 3px; padding: 2px 4px; font-size: 11px; min-width: 60px; }"
    "QSpinBox::up-button, QSpinBox::down-button { width: 14px; }"
    "QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050;"
    "  border-radius: 3px; padding: 3px 10px; font-size: 11px; }"
    "QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px solid #0090e0; }");

FilterEditorPopup::FilterEditorPopup(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setObjectName("FilterEditorPopup");
    setStyleSheet(kPopupStyle);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(6);

    auto* title = new QLabel("Filter Edges (Hz)");
    title->setStyleSheet("QLabel { color: #00c8ff; font-size: 11px; font-weight: bold; }");
    root->addWidget(title);

    // Lo row
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel("Lo:");
        lbl->setFixedWidth(24);
        row->addWidget(lbl);
        m_loSpin = new QSpinBox;
        m_loSpin->setRange(-20000, 20000);
        m_loSpin->setSuffix(" Hz");
        m_loSpin->setAccelerated(true);
        row->addWidget(m_loSpin);
        root->addLayout(row);
    }

    // Hi row
    {
        auto* row = new QHBoxLayout;
        auto* lbl = new QLabel("Hi:");
        lbl->setFixedWidth(24);
        row->addWidget(lbl);
        m_hiSpin = new QSpinBox;
        m_hiSpin->setRange(-20000, 20000);
        m_hiSpin->setSuffix(" Hz");
        m_hiSpin->setAccelerated(true);
        row->addWidget(m_hiSpin);
        root->addLayout(row);
    }

    // Apply button
    auto* applyBtn = new QPushButton("Apply");
    connect(applyBtn, &QPushButton::clicked, this, [this]() {
        emit filterChanged(m_loSpin->value(), m_hiSpin->value());
        close();
    });
    root->addWidget(applyBtn);
}

void FilterEditorPopup::setFilter(int lo, int hi)
{
    m_loSpin->setValue(lo);
    m_hiSpin->setValue(hi);
}

void FilterEditorPopup::showAt(const QPoint& globalPos)
{
    adjustSize();
    QPoint pos = globalPos;
    QRect screen = QApplication::primaryScreen()->availableGeometry();
    if (pos.x() + width() > screen.right()) {
        pos.setX(screen.right() - width());
    }
    if (pos.y() + height() > screen.bottom()) {
        pos.setY(globalPos.y() - height());
    }
    move(pos);
    show();
    m_loSpin->setFocus();
    m_loSpin->selectAll();
}

} // namespace AetherSDR
