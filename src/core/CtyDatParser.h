#pragma once

#include <QString>
#include <QHash>
#include <QSet>

namespace AetherSDR {

struct DxccEntity {
    QString primaryPrefix;   // e.g. "G"
    QString name;            // e.g. "England"
    QString continent;       // e.g. "EU"
    int     cqZone{0};
    int     ituZone{0};
    double  latitude{0.0};   // degrees, positive = North
    double  longitude{0.0};  // degrees, positive = East (converted from cty.dat West-positive)
};

// ---------------------------------------------------------------------------
// CtyDatParser
//
// Parses the AD1C cty.dat file and resolves callsigns to DXCC entities via
// longest-prefix matching.  Load once at startup; queries are O(prefix_len).
// ---------------------------------------------------------------------------
class CtyDatParser {
public:
    // Returns true on success.
    bool loadFromFile(const QString& path);
    bool loadFromResource(const QString& resourcePath);   // e.g. ":/cty.dat"

    // Resolve callsign -> primary prefix of matched entity ("G", "VK", …)
    // Returns empty string if no match.
    QString resolvePrimaryPrefix(const QString& callsign) const;

    // Look up entity details by primary prefix.
    const DxccEntity* entityByPrefix(const QString& primaryPrefix) const;

    // Compute great-circle bearing (degrees, 0=N, 90=E) from (lat1,lon1) to (lat2,lon2).
    // All coordinates in degrees, positive = North/East.
    static int greatCircleBearing(double lat1, double lon1, double lat2, double lon2);

    int entityCount() const { return m_entityByPrefix.size(); }
    bool isLoaded()   const { return !m_entityByPrefix.isEmpty(); }

private:
    void parse(const QStringList& lines);

    // exact-match table:  "=VK9XX"  -> primaryPrefix
    QHash<QString, QString> m_exactMatch;
    // prefix table: "VK9X" -> primaryPrefix
    QHash<QString, QString> m_prefixTable;
    // entity details keyed by primaryPrefix
    QHash<QString, DxccEntity> m_entityByPrefix;
    int m_maxPrefixLen{0};
};

} // namespace AetherSDR
