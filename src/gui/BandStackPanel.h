#pragma once

#include "core/BandStackSettings.h"

#include <QWidget>
#include <QVector>
#include <QPushButton>

class QVBoxLayout;
class QScrollArea;

namespace AetherSDR {

class BandPlanManager;

// Vertical strip (80px) of frequency bookmarks alongside the panadapter.
// Each bookmark shows the frequency, color-coded by band plan segment.
// Click to recall, right-click to delete, "+" to add current position.
class BandStackPanel : public QWidget {
    Q_OBJECT

public:
    explicit BandStackPanel(QWidget* parent = nullptr);

    // Add a bookmark button with the given entry and color
    void addBookmark(const BandStackEntry& entry, const QColor& color);

    // Remove bookmark at index
    void removeBookmark(int index);

    // Load all bookmarks for a radio, looking up colors from band plan
    void loadBookmarks(const QString& radioSerial, const BandPlanManager* bpm);

    // Clear all bookmarks (on disconnect)
    void clear();

    // Look up band plan segment color for a frequency
    static QColor colorForFrequency(double freqMhz, const BandPlanManager* bpm);

signals:
    void addRequested();
    void recallRequested(const BandStackEntry& entry);
    void removeRequested(int index);

private:
    void rebuildButtons();

    struct Bookmark {
        BandStackEntry entry;
        QColor color;
        QPushButton* button{nullptr};
    };

    QVector<Bookmark> m_bookmarks;
    QVBoxLayout* m_buttonLayout{nullptr};
    QScrollArea* m_scrollArea{nullptr};
    QPushButton* m_addButton{nullptr};
};

} // namespace AetherSDR
