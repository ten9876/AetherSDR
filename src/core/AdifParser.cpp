#include "AdifParser.h"

#include <QFile>
#include <QRegularExpression>
#include <QtAlgorithms>

namespace AetherSDR {

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

// Extract the value of a named ADIF field from a block of text.
// ADIF field format: <FIELDNAME:length>value  (case insensitive)
static QString extractField(const QString& block, const QString& fieldName)
{
    // e.g. <CALL:6>G3ABC  or <CALL:6:S>G3ABC
    static const QRegularExpression re(
        R"(<)" + QRegularExpression::escape(fieldName) + R"((?::\d+(?::[A-Z])?)?:(\d+)>)",
        QRegularExpression::CaseInsensitiveOption);
    auto m = re.match(block);
    if (!m.hasMatch()) return {};
    int len = m.captured(1).toInt();
    int start = m.capturedEnd(0);
    if (start + len > block.length()) return {};
    return block.mid(start, len);
}

QString AdifParser::normaliseMode(const QString& mode, const QString& submode)
{
    // Check submode first (handles MFSK/FT8, MFSK/FT4, etc.)
    const QString sub = submode.toUpper();
    if (!sub.isEmpty()) {
        if (sub == "FT8" || sub == "FT4" || sub == "JS8" ||
            sub == "JT65" || sub == "JT9" || sub == "WSPR" ||
            sub == "PSK31" || sub == "PSK63" || sub == "RTTY")
            return "DATA";
    }

    const QString m = mode.toUpper();
    if (m == "CW")   return "CW";
    if (m == "SSB" || m == "USB" || m == "LSB" || m == "AM" || m == "FM")
        return "PHONE";
    if (m == "FT8"  || m == "FT4"  || m == "RTTY" || m == "PSK31" ||
        m == "PSK63" || m == "WSPR" || m == "JT65" || m == "JT9"  ||
        m == "JS8"   || m == "MFSK" || m == "OLIVIA" || m == "CONTESTIA" ||
        m == "SSTV"  || m == "PACKET" || m == "HELL"  || m == "ATV")
        return "DATA";
    // Unknown — treat as DATA
    if (!m.isEmpty()) return "DATA";
    return "PHONE";  // default
}

QString AdifParser::freqToBand(double mhz)
{
    if (mhz >= 1.8   && mhz < 2.0)   return "160m";
    if (mhz >= 3.5   && mhz < 4.0)   return "80m";
    if (mhz >= 5.0   && mhz < 5.6)   return "60m";
    if (mhz >= 7.0   && mhz < 7.3)   return "40m";
    if (mhz >= 10.1  && mhz < 10.15) return "30m";
    if (mhz >= 14.0  && mhz < 14.35) return "20m";
    if (mhz >= 18.068&& mhz < 18.168)return "17m";
    if (mhz >= 21.0  && mhz < 21.45) return "15m";
    if (mhz >= 24.89 && mhz < 24.99) return "12m";
    if (mhz >= 28.0  && mhz < 29.7)  return "10m";
    if (mhz >= 50.0  && mhz < 54.0)  return "6m";
    if (mhz >= 70.0  && mhz < 70.5)  return "4m";
    if (mhz >= 144.0 && mhz < 148.0) return "2m";
    if (mhz >= 430.0 && mhz < 440.0) return "70cm";
    return {};
}

// ---------------------------------------------------------------------------
// Core parser
// ---------------------------------------------------------------------------

QVector<QsoRecord> AdifParser::parse(const QByteArray& data)
{
    QVector<QsoRecord> records;
    const QString text = QString::fromUtf8(data);

    // Skip ADIF header section (everything before <EOH>)
    int bodyStart = 0;
    int eoh = text.indexOf("<EOH>", 0, Qt::CaseInsensitive);
    if (eoh != -1)
        bodyStart = eoh + 5;

    // Split on <EOR> record separators
    static const QRegularExpression eorRe("<EOR>", QRegularExpression::CaseInsensitiveOption);
    int pos = bodyStart;
    QRegularExpressionMatchIterator it = eorRe.globalMatch(text, bodyStart);

    while (it.hasNext()) {
        auto eorMatch = it.next();
        const QString block = text.mid(pos, eorMatch.capturedStart() - pos);
        pos = eorMatch.capturedEnd();

        if (block.trimmed().isEmpty()) continue;

        QsoRecord rec;
        rec.callsign = extractField(block, "CALL").trimmed().toUpper();
        if (rec.callsign.isEmpty()) continue;

        // Band: prefer explicit <BAND> field, fall back to <FREQ> → freqToBand
        rec.band = extractField(block, "BAND").trimmed().toLower();
        if (rec.band.isEmpty()) {
            const QString freqStr = extractField(block, "FREQ").trimmed();
            if (!freqStr.isEmpty()) {
                bool ok = false;
                double mhz = freqStr.toDouble(&ok);
                if (ok) rec.band = freqToBand(mhz);
            }
        }

        // Mode
        const QString mode    = extractField(block, "MODE").trimmed();
        const QString submode = extractField(block, "SUBMODE").trimmed();
        rec.modeGroup = normaliseMode(mode, submode);

        records.append(rec);
    }

    return records;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

QVector<QsoRecord> AdifParser::parseFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return parse(f.readAll());
}

void AdifParser::parseFileAsync(const QString& path)
{
    emit finished(parseFile(path));
}

} // namespace AetherSDR
