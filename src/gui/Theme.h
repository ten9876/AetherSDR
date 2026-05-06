#pragma once

#include <QString>
#include "DesignTokens.h"

namespace AetherSDR {

// Shared dark-theme stylesheet applied to MainWindow and every
// top-level floating window so that pop-out panels inherit the
// complete theme instead of falling back to the system palette.
inline QString darkThemeStylesheet()
{
    using namespace DesignTokens;
    const QString hw  = QString::number(kCtrlHandleW);
    const QString hm  = QString::number(-3);
    const QString hr  = QString::number(kCtrlHandleW / 2);
    const QString rad = QString::number(kCtrlRadius);

    return
        QStringLiteral("QWidget { background-color: ")  + kSurfaceBase
        + QStringLiteral("; color: ")                    + kTextPrimary
        + QStringLiteral("; font-family: \"Inter\", \"Segoe UI\", sans-serif; font-size: 13px; }")

        + QStringLiteral("QGroupBox { border: 1px solid ") + kBorderControl
        + QStringLiteral("; border-radius: ")              + rad
        + QStringLiteral("px; margin-top: 8px; padding-top: 8px; }")

        + QStringLiteral("QGroupBox::title { subcontrol-origin: margin; left: 8px; color: ")
        + kTextAccent + QStringLiteral("; }")

        + QStringLiteral("QPushButton { background-color: #1a2a3a; border: 1px solid ")
        + kBorderControl + QStringLiteral("; border-radius: ") + rad
        + QStringLiteral("px; padding: 4px 10px; color: ") + kTextPrimary + QStringLiteral("; }")

        + QStringLiteral("QPushButton:hover { background-color: ")  + kSurfaceOverlay
        + QStringLiteral("; }")

        + QStringLiteral("QPushButton:pressed { background-color: ") + kColorAccent
        + QStringLiteral("; color: #000; }")

        + QStringLiteral("QComboBox { background-color: #1a2a3a; border: 1px solid ")
        + kBorderControl + QStringLiteral("; border-radius: ") + rad
        + QStringLiteral("px; padding: 3px 6px; }")

        + QStringLiteral("QComboBox::drop-down { border: none; }")

        + QStringLiteral("QListWidget { background-color: #111120; border: 1px solid ")
        + kBorderControl + QStringLiteral("; alternate-background-color: #161626; }")

        + QStringLiteral("QListWidget::item:selected { background-color: ") + kColorAccent
        + QStringLiteral("; color: #000; }")

        + QStringLiteral("QSlider::groove:horizontal { height: 4px; background: ")
        + kBorderControl + QStringLiteral("; border-radius: 2px; }")

        + QStringLiteral("QSlider::handle:horizontal { width: ") + hw
        + QStringLiteral("px; height: ") + hw
        + QStringLiteral("px; margin: ") + hm
        + QStringLiteral("px 0; background: ") + kColorAccent
        + QStringLiteral("; border-radius: ") + hr
        + QStringLiteral("px; }")

        + QStringLiteral("QMenuBar { background-color: ") + kSurfaceSunken
        + QStringLiteral("; }")

        + QStringLiteral("QMenuBar::item:selected { background-color: #1a2a3a; }")

        + QStringLiteral("QMenu { background-color: #111120; border: 1px solid ")
        + kBorderControl + QStringLiteral("; }")

        + QStringLiteral("QMenu::item:selected { background-color: ") + kColorAccent
        + QStringLiteral("; color: #000; }")

        + QStringLiteral("QStatusBar { background-color: ") + kSurfaceSunken
        + QStringLiteral("; border-top: 1px solid ") + kBorderControl
        + QStringLiteral("; }")

        + QStringLiteral("QProgressBar { background-color: #111120; border: 1px solid ")
        + kBorderControl + QStringLiteral("; border-radius: 3px; }")

        + QStringLiteral("QSplitter::handle { background-color: ")
        + kBorderControl + QStringLiteral("; width: 2px; }");
}

} // namespace AetherSDR
