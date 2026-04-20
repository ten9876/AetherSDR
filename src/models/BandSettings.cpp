#include "BandSettings.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

namespace AetherSDR {

static QString bandMemoryFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + "/AetherSDR/BandMemory.json";
}

BandSettings::BandSettings(QObject* parent)
    : QObject(parent)
{
}

QString BandSettings::bandForFrequency(double freqMhz)
{
    for (int i = 0; i < kBandCount; ++i) {
        if (freqMhz >= kBands[i].lowMhz && freqMhz <= kBands[i].highMhz)
            return QString::fromLatin1(kBands[i].name);
    }
    return QStringLiteral("GEN");
}

const BandDef& BandSettings::bandDef(const QString& name)
{
    if (name == "WWV") return kWwvBand;
    for (int i = 0; i < kBandCount; ++i) {
        if (name == QLatin1String(kBands[i].name))
            return kBands[i];
    }
    return kGenBand;
}

void BandSettings::saveBandState(const QString& bandName, const BandSnapshot& snap)
{
    m_bandStates[bandName] = snap;
}

BandSnapshot BandSettings::loadBandState(const QString& bandName) const
{
    if (m_bandStates.contains(bandName))
        return m_bandStates[bandName];

    // Return defaults from band definition
    const auto& def = bandDef(bandName);
    BandSnapshot snap;
    snap.frequencyMhz    = def.defaultFreqMhz;
    snap.mode            = QString::fromLatin1(def.defaultMode);
    snap.minDbm          = -130.0f;
    snap.maxDbm          = -40.0f;
    snap.spectrumFrac    = 0.40f;
    return snap;
}

bool BandSettings::hasSavedState(const QString& bandName) const
{
    return m_bandStates.contains(bandName);
}

// ── JSON serialization ──────────────────────────────────────────────────

QJsonObject BandSnapshot::toJson() const
{
    QJsonObject obj;
    obj["frequencyMhz"] = frequencyMhz;
    obj["mode"]         = mode;
    obj["rxAntenna"]    = rxAntenna;
    obj["filterLow"]    = filterLow;
    obj["filterHigh"]   = filterHigh;
    obj["agcMode"]      = agcMode;
    obj["agcThreshold"] = agcThreshold;
    obj["bandwidthMhz"] = bandwidthMhz;
    obj["centerMhz"]    = centerMhz;
    obj["rfGain"]       = rfGain;
    obj["wnbOn"]        = wnbOn;
    obj["wnbLevel"]     = wnbLevel;
    obj["minDbm"]       = static_cast<double>(minDbm);
    obj["maxDbm"]       = static_cast<double>(maxDbm);
    obj["spectrumFrac"] = static_cast<double>(spectrumFrac);
    return obj;
}

BandSnapshot BandSnapshot::fromJson(const QJsonObject& obj)
{
    BandSnapshot s;
    s.frequencyMhz = obj["frequencyMhz"].toDouble();
    s.mode         = obj["mode"].toString();
    s.rxAntenna    = obj["rxAntenna"].toString();
    s.filterLow    = obj["filterLow"].toInt();
    s.filterHigh   = obj["filterHigh"].toInt();
    s.agcMode      = obj["agcMode"].toString();
    s.agcThreshold = obj["agcThreshold"].toInt();
    s.bandwidthMhz = obj["bandwidthMhz"].toDouble();
    s.centerMhz    = obj["centerMhz"].toDouble();
    s.rfGain       = obj["rfGain"].toInt();
    s.wnbOn        = obj["wnbOn"].toBool();
    s.wnbLevel     = obj["wnbLevel"].toInt();
    s.minDbm       = static_cast<float>(obj["minDbm"].toDouble(-130.0));
    s.maxDbm       = static_cast<float>(obj["maxDbm"].toDouble(-40.0));
    s.spectrumFrac = static_cast<float>(obj["spectrumFrac"].toDouble(0.40));
    return s;
}

// ── Disk persistence ────────────────────────────────────────────────────

void BandSettings::saveToFile() const
{
    const QString path = bandMemoryFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QJsonObject root;
    for (auto it = m_bandStates.constBegin(); it != m_bandStates.constEnd(); ++it)
        root[it.key()] = it.value().toJson();

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "BandSettings: cannot write" << path << f.errorString();
        return;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void BandSettings::loadFromFile()
{
    const QString path = bandMemoryFilePath();
    QFile f(path);
    if (!f.exists()) return;
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "BandSettings: cannot read" << path << f.errorString();
        return;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qWarning() << "BandSettings: parse error in" << path << err.errorString();
        return;
    }

    const QJsonObject root = doc.object();
    for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
        BandSnapshot snap = BandSnapshot::fromJson(it.value().toObject());
        if (snap.isValid())
            m_bandStates[it.key()] = snap;
    }
    qDebug() << "BandSettings: loaded" << m_bandStates.size() << "band memories from disk";
}

} // namespace AetherSDR
