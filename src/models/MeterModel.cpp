#include "MeterModel.h"
#include <QDebug>
#include <cmath>

namespace AetherSDR {

MeterModel::MeterModel(QObject* parent)
    : QObject(parent)
{}

void MeterModel::defineMeter(const MeterDef& def)
{
    m_defs[def.index] = def;

    // Cache indices for high-frequency lookups
    if (def.source == "SLC" && def.name == "LEVEL")
        m_sLevelIdx = def.index;
    else if (def.name == "FWDPWR")
        m_fwdPwrIdx = def.index;
    else if (def.name == "SWR")
        m_swrIdx = def.index;
    else if (def.name == "MICPEAK")
        m_micPeakIdx = def.index;
    else if (def.name == "COMPPEAK")
        m_compPeakIdx = def.index;
    else if (def.name == "MIC")
        m_micLevelIdx = def.index;
    else if (def.name == "COMP")
        m_compLevelIdx = def.index;
    else if (def.name == "HWALC")
        m_alcIdx = def.index;
    else if (def.name == "PATEMP")
        m_paTempIdx = def.index;
    else if (def.name == "+13.8A")
        m_supplyIdx = def.index;

    qDebug() << "MeterModel: defined meter" << def.index
             << def.source << def.sourceIndex << def.name
             << def.unit << "[" << def.low << "->" << def.high << "]";
}

void MeterModel::removeMeter(int index)
{
    m_defs.remove(index);
    m_values.remove(index);

    if (index == m_sLevelIdx)   m_sLevelIdx = -1;
    if (index == m_fwdPwrIdx)   m_fwdPwrIdx = -1;
    if (index == m_swrIdx)      m_swrIdx = -1;
    if (index == m_micPeakIdx)   m_micPeakIdx = -1;
    if (index == m_compPeakIdx)  m_compPeakIdx = -1;
    if (index == m_micLevelIdx)  m_micLevelIdx = -1;
    if (index == m_compLevelIdx) m_compLevelIdx = -1;
    if (index == m_alcIdx)       m_alcIdx = -1;
    if (index == m_paTempIdx)    m_paTempIdx = -1;
    if (index == m_supplyIdx)    m_supplyIdx = -1;
}

float MeterModel::convertRaw(const MeterDef& def, qint16 raw) const
{
    // Conversion factors from FlexLib Meter.cs UpdateValue()
    // FlexLib uses volt_denom=1024 for fw < 1.11.0.0, 256 for newer.
    // FLEX-8600 fw v1.4.0.0 is a newer product and uses 256.
    if (def.unit == "dBm" || def.unit == "dB" || def.unit == "dBFS" || def.unit == "SWR")
        return static_cast<float>(raw) / 128.0f;
    if (def.unit == "Volts" || def.unit == "Amps")
        return static_cast<float>(raw) / 256.0f;
    if (def.unit == "degF" || def.unit == "degC")
        return static_cast<float>(raw) / 64.0f;
    return static_cast<float>(raw);
}

void MeterModel::updateValues(const QVector<quint16>& ids, const QVector<qint16>& vals)
{
    const int n = qMin(ids.size(), vals.size());
    bool sChanged = false;
    bool txChanged = false;
    bool micChanged = false;
    bool alcChanged = false;
    bool hwChanged = false;

    for (int i = 0; i < n; ++i) {
        const int idx = static_cast<int>(ids[i]);
        auto it = m_defs.constFind(idx);
        if (it == m_defs.constEnd()) continue;

        const float v = convertRaw(*it, vals[i]);
        m_values[idx] = v;

        if (idx == m_sLevelIdx) {
            m_sLevel = v;
            sChanged = true;
        } else if (idx == m_fwdPwrIdx) {
            // FWDPWR meter reports in dBm — convert to watts for display.
            // watts = 10^(dBm/10) / 1000
            // e.g. 50 dBm = 100 W, 47 dBm ≈ 50 W, 40 dBm = 10 W
            m_fwdPower = std::pow(10.0f, v / 10.0f) / 1000.0f;
            txChanged = true;
        } else if (idx == m_swrIdx) {
            m_swr = v;
            txChanged = true;
        } else if (idx == m_micPeakIdx) {
            m_micPeak = v;
            micChanged = true;
        } else if (idx == m_compPeakIdx) {
            m_compPeak = v;
            micChanged = true;
        } else if (idx == m_micLevelIdx) {
            m_micLevel = v;
            micChanged = true;
        } else if (idx == m_compLevelIdx) {
            m_compLevel = v;
            micChanged = true;
        } else if (idx == m_alcIdx) {
            m_alc = v;
            alcChanged = true;
        } else if (idx == m_paTempIdx) {
            m_paTemp = v;
            hwChanged = true;
        } else if (idx == m_supplyIdx) {
            m_supplyVolts = v;  // "+13.8A" = supply voltage at point A (before fuse)
            hwChanged = true;
        }

        emit meterUpdated(idx, v);
    }

    if (sChanged)
        emit sLevelChanged(m_sLevel);
    if (txChanged)
        emit txMetersChanged(m_fwdPower, m_swr);
    if (micChanged)
        emit micMetersChanged(m_micLevel, m_compLevel, m_micPeak, m_compPeak);
    if (alcChanged)
        emit this->alcChanged(m_alc);
    if (hwChanged)
        emit hwTelemetryChanged(m_paTemp, m_supplyVolts);
}

const MeterDef* MeterModel::meterDef(int index) const
{
    auto it = m_defs.constFind(index);
    return (it != m_defs.constEnd()) ? &(*it) : nullptr;
}

int MeterModel::findMeter(const QString& source, const QString& name, int sourceIndex) const
{
    for (auto it = m_defs.constBegin(); it != m_defs.constEnd(); ++it) {
        if (it->source == source && it->name == name) {
            if (sourceIndex < 0 || it->sourceIndex == sourceIndex)
                return it->index;
        }
    }
    return -1;
}

float MeterModel::value(int index) const
{
    return m_values.value(index, 0.0f);
}

} // namespace AetherSDR
