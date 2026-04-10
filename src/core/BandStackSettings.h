#pragma once

#include <QString>
#include <QVector>
#include <QMap>

namespace AetherSDR {

struct BandStackEntry {
    double frequencyMhz{0.0};
    QString mode;
    int filterLow{0};
    int filterHigh{0};
    QString rxAntenna;
    QString txAntenna;
    QString agcMode;
    int agcThreshold{0};
    int audioGain{50};
    bool nbOn{false};
    int nbLevel{50};
    bool nrOn{false};
    int nrLevel{50};
    bool wnbOn{false};
    int wnbLevel{50};
};

// Persistence for user frequency bookmarks, stored per-radio in
// ~/.config/AetherSDR/BandStack.settings (XML, atomic save).
class BandStackSettings {
public:
    static BandStackSettings& instance();

    void load();
    void save();

    QVector<BandStackEntry> entries(const QString& radioSerial) const;
    void addEntry(const QString& radioSerial, const BandStackEntry& entry);
    void removeEntry(const QString& radioSerial, int index);

private:
    BandStackSettings();
    BandStackSettings(const BandStackSettings&) = delete;
    BandStackSettings& operator=(const BandStackSettings&) = delete;

    static QString sanitizeSerial(const QString& serial);

    QMap<QString, QVector<BandStackEntry>> m_entries;
    QString m_filePath;
};

} // namespace AetherSDR
