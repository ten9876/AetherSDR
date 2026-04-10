#include "BandStackPanel.h"
#include "core/BandStackSettings.h"
#include "models/BandPlanManager.h"

#include <QVBoxLayout>
#include <QScrollArea>
#include <QMenu>

namespace AetherSDR {

BandStackPanel::BandStackPanel(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(80);
    setStyleSheet("background: #0a0a14;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // Scrollable area for bookmark buttons
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollBar:vertical { background: #0a0a14; width: 6px; }"
        "QScrollBar::handle:vertical { background: #304050; border-radius: 3px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");

    auto* scrollContent = new QWidget;
    m_buttonLayout = new QVBoxLayout(scrollContent);
    m_buttonLayout->setContentsMargins(0, 0, 0, 0);
    m_buttonLayout->setSpacing(3);
    m_buttonLayout->addStretch(1);  // push buttons to top

    m_scrollArea->setWidget(scrollContent);
    root->addWidget(m_scrollArea, 1);

    // "+" button at bottom
    m_addButton = new QPushButton("+", this);
    m_addButton->setFixedSize(72, 28);
    m_addButton->setStyleSheet(
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #8aa8c0; font-size: 16px; font-weight: bold; }"
        "QPushButton:hover { background: #203040; color: #c8d8e8; }");
    m_addButton->setToolTip("Save current frequency as a bookmark");
    connect(m_addButton, &QPushButton::clicked, this, &BandStackPanel::addRequested);
    root->addWidget(m_addButton, 0, Qt::AlignHCenter);
}

QColor BandStackPanel::colorForFrequency(double freqMhz, const BandPlanManager* bpm)
{
    if (!bpm) return QColor(0x30, 0x40, 0x50);  // dark grey default
    for (const auto& seg : bpm->segments()) {
        if (freqMhz >= seg.lowMhz && freqMhz <= seg.highMhz) {
            return seg.color;
        }
    }
    return QColor(0x30, 0x40, 0x50);
}

void BandStackPanel::addBookmark(const BandStackEntry& entry, const QColor& color)
{
    Bookmark bm;
    bm.entry = entry;
    bm.color = color;

    // Create button
    auto* btn = new QPushButton(this);
    btn->setFixedSize(72, 32);
    btn->setText(QString::number(entry.frequencyMhz, 'f', 3));
    btn->setToolTip(QString("%1 MHz  %2  %3")
        .arg(entry.frequencyMhz, 0, 'f', 6)
        .arg(entry.mode)
        .arg(entry.rxAntenna));

    // Darken the color slightly for the background, use as-is for border
    QColor bg = color.darker(130);
    btn->setStyleSheet(
        QString("QPushButton { background: %1; border: 1px solid %2; "
                "border-radius: 3px; color: #ffffff; font-size: 11px; font-weight: bold; }"
                "QPushButton:hover { background: %3; }")
            .arg(bg.name(), color.name(), color.name()));

    btn->setContextMenuPolicy(Qt::CustomContextMenu);

    int index = m_bookmarks.size();

    // Left click: recall
    connect(btn, &QPushButton::clicked, this, [this, index]() {
        if (index < m_bookmarks.size()) {
            emit recallRequested(m_bookmarks[index].entry);
        }
    });

    // Right click: context menu with Remove
    connect(btn, &QPushButton::customContextMenuRequested, this, [this, btn, index]() {
        QMenu menu;
        menu.setStyleSheet(
            "QMenu { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050; }"
            "QMenu::item:selected { background: #304050; }");
        auto* removeAction = menu.addAction("Remove");
        if (menu.exec(btn->mapToGlobal(QPoint(btn->width(), 0))) == removeAction) {
            emit removeRequested(index);
        }
    });

    bm.button = btn;
    m_bookmarks.append(bm);

    // Insert before the stretch
    int insertPos = m_buttonLayout->count() - 1;  // before stretch
    m_buttonLayout->insertWidget(insertPos, btn, 0, Qt::AlignHCenter);
}

void BandStackPanel::removeBookmark(int index)
{
    if (index < 0 || index >= m_bookmarks.size()) return;

    // Remove the button
    auto* btn = m_bookmarks[index].button;
    m_buttonLayout->removeWidget(btn);
    delete btn;
    m_bookmarks.removeAt(index);

    // Rebuild connections since indices shifted
    rebuildButtons();
}

void BandStackPanel::rebuildButtons()
{
    // Reconnect all buttons with correct indices after a removal
    for (int i = 0; i < m_bookmarks.size(); ++i) {
        auto* btn = m_bookmarks[i].button;
        btn->disconnect();

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            if (i < m_bookmarks.size()) {
                emit recallRequested(m_bookmarks[i].entry);
            }
        });

        connect(btn, &QPushButton::customContextMenuRequested, this, [this, btn, i]() {
            QMenu menu;
            menu.setStyleSheet(
                "QMenu { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050; }"
                "QMenu::item:selected { background: #304050; }");
            auto* removeAction = menu.addAction("Remove");
            if (menu.exec(btn->mapToGlobal(QPoint(btn->width(), 0))) == removeAction) {
                emit removeRequested(i);
            }
        });
    }
}

void BandStackPanel::loadBookmarks(const QString& radioSerial, const BandPlanManager* bpm)
{
    clear();
    const auto entries = BandStackSettings::instance().entries(radioSerial);
    for (const BandStackEntry& entry : entries) {
        QColor color = colorForFrequency(entry.frequencyMhz, bpm);
        addBookmark(entry, color);
    }
}

void BandStackPanel::clear()
{
    for (auto& bm : m_bookmarks) {
        m_buttonLayout->removeWidget(bm.button);
        delete bm.button;
    }
    m_bookmarks.clear();
}

} // namespace AetherSDR
