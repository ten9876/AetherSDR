#pragma once

// Shared combo box styling for consistent down-arrow appearance across all
// QComboBox instances in AetherSDR. Use applyComboStyle(combo) on any
// QComboBox to get the standard dark-themed look with painted down-arrow.

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QPainter>
#include "DesignTokens.h"

namespace AetherSDR {

// Generate a small down-arrow PNG (cached in temp dir, created once).
inline QString comboArrowPath()
{
    using namespace DesignTokens;
    static QString path;
    if (!path.isEmpty()) return path;
    path = QDir::temp().filePath("aethersdr_combo_arrow.png");
    if (QFile::exists(path)) return path;
    QPixmap pm(8, 6);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    // Arrow fill uses the secondary text color
    p.setBrush(QColor(kTextSecondary.data()));
    const QPointF tri[] = {{0, 0}, {8, 0}, {4, 6}};
    p.drawPolygon(tri, 3);
    p.end();
    pm.save(path, "PNG");
    return path;
}

// Standard combo box stylesheet matching the dark theme with painted arrow.
inline QString comboStyleSheet()
{
    using namespace DesignTokens;
    auto sheet = QStringLiteral("QComboBox { background: #1a2a3a; color: ") + kTextPrimary
        + QStringLiteral("; border: 1px solid ") + kBorderControl
        + QStringLiteral("; padding: 2px 2px 2px 4px; border-radius: 2px; }"
                         "QComboBox::drop-down { border: none; width: 14px; }"
                         "QComboBox::down-arrow { image: url(%1); width: 8px; height: 6px; }"
                         "QComboBox QAbstractItemView { background: #1a2a3a; color: ") + kTextPrimary
        + QStringLiteral("; selection-background-color: ") + kColorAccent + QStringLiteral("; }");
    return sheet.arg(comboArrowPath());
}

// Apply the standard style to a combo box.
inline void applyComboStyle(QComboBox* combo)
{
    combo->setStyleSheet(comboStyleSheet());
}

} // namespace AetherSDR
