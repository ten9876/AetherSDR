#pragma once

// Shared combo box styling for consistent down-arrow appearance across all
// QComboBox instances in AetherSDR. Use applyComboStyle(combo) on any
// QComboBox to get the standard themed look with painted down-arrow.

#include "ThemeColors.h"
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QPainter>

namespace AetherSDR {

// Generate a small down-arrow PNG for the given color (cached per color).
inline QString comboArrowPath(const QColor& color = QColor(0x8a, 0xa8, 0xc0))
{
    QString suffix = color.name().mid(1);  // e.g. "8aa8c0"
    QString path = QDir::temp().filePath(
        QStringLiteral("aethersdr_combo_arrow_%1.png").arg(suffix));
    if (QFile::exists(path)) return path;
    QPixmap pm(8, 6);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    const QPointF tri[] = {{0, 0}, {8, 0}, {4, 6}};
    p.drawPolygon(tri, 3);
    p.end();
    pm.save(path, "PNG");
    return path;
}

// Standard combo box stylesheet using the active theme palette.
inline QString comboStyleSheet()
{
    const auto& p = ThemeManager::instance().palette();
    return QString(
        "QComboBox { background: %1; color: %2; border: 1px solid %3;"
        " padding: 2px 2px 2px 4px; border-radius: 2px; }"
        "QComboBox::drop-down { border: none; width: 14px; }"
        "QComboBox::down-arrow { image: url(%4); width: 8px; height: 6px; }"
        "QComboBox QAbstractItemView { background: %1; color: %2;"
        " selection-background-color: %5; }")
        .arg(p.buttonBase.name(),
             p.textPrimary.name(),
             p.tabBorder.name(),
             comboArrowPath(p.titleBarText),
             p.accent.name());
}

// Apply the standard style to a combo box.
inline void applyComboStyle(QComboBox* combo)
{
    combo->setStyleSheet(comboStyleSheet());
}

} // namespace AetherSDR
