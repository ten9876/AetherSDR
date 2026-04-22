#include "BandSettings.h"
#include "core/AppSettings.h"

// Band persistence is deprecated (issue #9). Save/load are no-ops.
#include <cstring>

namespace AetherSDR {

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
    persistToAppSettings(bandName, snap);
}

BandSnapshot BandSettings::loadBandState(const QString& bandName) const
{
    if (m_bandStates.contains(bandName))
        return m_bandStates[bandName];

    // Try loading from AppSettings (survives restart).
    BandSnapshot persisted = loadFromAppSettings(bandName);
    if (persisted.isValid())
        return persisted;

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

void BandSettings::saveToFile() const
{
    // Deprecated — band persistence pending redesign (issue #9)
}

void BandSettings::loadFromFile()
{
    // Deprecated — band persistence pending redesign (issue #9)
}

// ── AppSettings-backed persistence (#1849) ───────────────────────────────────

static QString bandKey(const QString& band, const char* field)
{
    return QStringLiteral("Band/%1/%2").arg(band, QLatin1String(field));
}

void BandSettings::persistToAppSettings(const QString& band, const BandSnapshot& snap)
{
    auto& s = AppSettings::instance();
    s.setValue(bandKey(band, "LastFrequencyMHz"),  QString::number(snap.frequencyMhz, 'f', 6));
    s.setValue(bandKey(band, "Mode"),              snap.mode);
    s.setValue(bandKey(band, "RxAntenna"),         snap.rxAntenna);
    s.setValue(bandKey(band, "FilterLow"),         QString::number(snap.filterLow));
    s.setValue(bandKey(band, "FilterHigh"),        QString::number(snap.filterHigh));
    s.setValue(bandKey(band, "AgcMode"),           snap.agcMode);
    s.setValue(bandKey(band, "AgcThreshold"),      QString::number(snap.agcThreshold));
    s.setValue(bandKey(band, "RfGain"),            QString::number(snap.rfGain));
    s.setValue(bandKey(band, "WnbOn"),             snap.wnbOn ? QStringLiteral("True") : QStringLiteral("False"));
    s.setValue(bandKey(band, "WnbLevel"),          QString::number(snap.wnbLevel));
    s.setValue(bandKey(band, "MinDbm"),            QString::number(static_cast<double>(snap.minDbm), 'f', 2));
    s.setValue(bandKey(band, "MaxDbm"),            QString::number(static_cast<double>(snap.maxDbm), 'f', 2));
    s.setValue(bandKey(band, "SpectrumFrac"),      QString::number(static_cast<double>(snap.spectrumFrac), 'f', 4));
    s.save();
}

BandSnapshot BandSettings::loadFromAppSettings(const QString& band) const
{
    const auto& s = AppSettings::instance();
    if (!s.contains(bandKey(band, "LastFrequencyMHz")))
        return {};  // no persisted state — isValid() == false

    BandSnapshot snap;
    snap.frequencyMhz = s.value(bandKey(band, "LastFrequencyMHz")).toDouble();
    snap.mode         = s.value(bandKey(band, "Mode")).toString();
    snap.rxAntenna    = s.value(bandKey(band, "RxAntenna")).toString();
    snap.filterLow    = s.value(bandKey(band, "FilterLow")).toInt();
    snap.filterHigh   = s.value(bandKey(band, "FilterHigh")).toInt();
    snap.agcMode      = s.value(bandKey(band, "AgcMode")).toString();
    snap.agcThreshold = s.value(bandKey(band, "AgcThreshold")).toInt();
    snap.rfGain       = s.value(bandKey(band, "RfGain")).toInt();
    snap.wnbOn        = s.value(bandKey(band, "WnbOn")).toString() == QLatin1String("True");
    snap.wnbLevel     = s.value(bandKey(band, "WnbLevel"), 50).toInt();
    snap.minDbm       = s.value(bandKey(band, "MinDbm"), -130.0).toFloat();
    snap.maxDbm       = s.value(bandKey(band, "MaxDbm"), -40.0).toFloat();
    snap.spectrumFrac = s.value(bandKey(band, "SpectrumFrac"), 0.40).toFloat();
    return snap;
}

} // namespace AetherSDR
