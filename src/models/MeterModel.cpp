#include "MeterModel.h"
#include "core/LogManager.h"
#include <QDebug>
#include <cmath>

namespace AetherSDR {

MeterModel::MeterModel(QObject* parent)
    : QObject(parent)
{}

void MeterModel::setTgxlHandle(quint32 handle)
{
    if (m_tgxlHandle == handle) return;
    m_tgxlHandle = handle;

    // Re-scan existing AMP meter definitions to reassign TGXL vs PGXL.
    // Meter definitions may arrive before the TGXL handle is known,
    // causing all AMP meters to be routed to the PGXL slot (#600).
    m_tgxlFwdIdx = -1;
    m_tgxlSwrIdx = -1;
    m_ampFwdPwrIdx = -1;
    m_ampSwrIdx = -1;
    m_ampTempIdx = -1;
    for (auto it = m_defs.constBegin(); it != m_defs.constEnd(); ++it) {
        const auto& def = *it;
        if (def.source == "AMP" && def.name == "FWD" && def.unit == "dBm") {
            if (handle != 0 && def.sourceIndex == static_cast<int>(handle))
                m_tgxlFwdIdx = def.index;
            else
                m_ampFwdPwrIdx = def.index;
        } else if (def.source == "AMP" && def.name == "RL") {
            if (handle != 0 && def.sourceIndex == static_cast<int>(handle))
                m_tgxlSwrIdx = def.index;
            else
                m_ampSwrIdx = def.index;
        } else if (def.source == "AMP" && def.name == "TEMP") {
            m_ampTempIdx = def.index;
        }
    }
}

void MeterModel::defineMeter(const MeterDef& def)
{
    m_defs[def.index] = def;

    // Cache indices for high-frequency lookups
    if (def.source == "SLC" && def.name == "LEVEL")
        m_sLevelIdxBySlice[def.sourceIndex] = def.index;
    else if (def.source == "SLC" && def.name == "ESC")
        m_escLevelIdxBySlice[def.sourceIndex] = def.index;
    else if (def.source.startsWith("TX") && def.name == "FWDPWR")
        m_fwdPwrIdx = def.index;
    else if (def.source.startsWith("TX") && def.name == "SWR")
        m_swrIdx = def.index;
    else if (def.name == "MICPEAK")
        m_micPeakIdx = def.index;
    else if (def.name == "COMPPEAK")
        m_compPeakIdx = def.index;
    else if (def.name == "AFTEREQ")
        m_afterEqIdx = def.index;
    else if (def.name == "MIC")
        m_micLevelIdx = def.index;
    else if (def.name == "COMP")
        m_compLevelIdx = def.index;
    else if (def.name == "HWALC")
        m_alcIdx = def.index;
    else if (def.source != "AMP" && def.name == "PATEMP")
        m_paTempIdx = def.index;
    else if (def.name == "+13.8A")
        m_supplyIdx = def.index;
    // Amplifier meters (source "AMP")
    // Multiple FWD/RL meters exist — one per amplifier handle.
    // TGXL meters go to TunerApplet (m_tgxlFwd/SwrIdx).
    // PGXL meters go to AmpApplet (m_ampFwdPwrIdx/SwrIdx/TempIdx).
    // Distinguish by matching def.sourceIndex against the known TGXL handle.
    else if (def.source == "AMP" && def.name == "FWD" && def.unit == "dBm") {
        if (m_tgxlHandle != 0 && def.sourceIndex == static_cast<int>(m_tgxlHandle))
            m_tgxlFwdIdx = def.index;
        else
            m_ampFwdPwrIdx = def.index;
    }
    else if (def.source == "AMP" && def.name == "RL") {
        if (m_tgxlHandle != 0 && def.sourceIndex == static_cast<int>(m_tgxlHandle))
            m_tgxlSwrIdx = def.index;
        else
            m_ampSwrIdx = def.index;
    }
    else if (def.source == "AMP" && def.name == "TEMP")
        m_ampTempIdx = def.index;

    qCDebug(lcMeters) << "MeterModel: defined meter" << def.index
             << def.source << def.sourceIndex << def.name
             << def.unit << "[" << def.low << "->" << def.high << "]";
}

void MeterModel::removeMeter(int index)
{
    m_defs.remove(index);
    m_values.remove(index);

    // Remove from per-slice LEVEL map
    for (auto it = m_sLevelIdxBySlice.begin(); it != m_sLevelIdxBySlice.end(); ) {
        if (it.value() == index) it = m_sLevelIdxBySlice.erase(it);
        else ++it;
    }
    for (auto it = m_escLevelIdxBySlice.begin(); it != m_escLevelIdxBySlice.end(); ) {
        if (it.value() == index) it = m_escLevelIdxBySlice.erase(it);
        else ++it;
    }
    if (index == m_fwdPwrIdx)   m_fwdPwrIdx = -1;
    if (index == m_swrIdx)      m_swrIdx = -1;
    if (index == m_micPeakIdx)   m_micPeakIdx = -1;
    if (index == m_compPeakIdx)  m_compPeakIdx = -1;
    if (index == m_afterEqIdx)   m_afterEqIdx = -1;
    if (index == m_micLevelIdx)  m_micLevelIdx = -1;
    if (index == m_compLevelIdx) m_compLevelIdx = -1;
    if (index == m_alcIdx)       m_alcIdx = -1;
    if (index == m_paTempIdx)    m_paTempIdx = -1;
    if (index == m_supplyIdx)    m_supplyIdx = -1;
    if (index == m_ampFwdPwrIdx) m_ampFwdPwrIdx = -1;
    if (index == m_ampSwrIdx)    m_ampSwrIdx = -1;
    if (index == m_ampTempIdx)   m_ampTempIdx = -1;
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
    // sLevelChanged is emitted per-slice inline in the loop below
    bool txChanged = false;
    bool micChanged = false;
    bool alcChanged = false;
    bool hwChanged = false;
    bool ampChanged = false;
    bool tgxlChanged = false;

    for (int i = 0; i < n; ++i) {
        const int idx = static_cast<int>(ids[i]);
        auto it = m_defs.constFind(idx);
        if (it == m_defs.constEnd()) continue;

        const float v = convertRaw(*it, vals[i]);
        m_values[idx] = v;

        // Check if this meter is a per-slice LEVEL meter
        bool isSliceLevel = false;
        for (auto sit = m_sLevelIdxBySlice.constBegin(); sit != m_sLevelIdxBySlice.constEnd(); ++sit) {
            if (sit.value() == idx) {
                emit sLevelChanged(sit.key(), v);
                isSliceLevel = true;
                break;
            }
        }
        // Check if this meter is a per-slice ESC meter
        if (!isSliceLevel) {
            for (auto sit = m_escLevelIdxBySlice.constBegin(); sit != m_escLevelIdxBySlice.constEnd(); ++sit) {
                if (sit.value() == idx) {
                    emit escLevelChanged(sit.key(), v);
                    isSliceLevel = true;
                    break;
                }
            }
        }
        if (isSliceLevel) {
            // no-op, already emitted
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
        } else if (idx == m_afterEqIdx) {
            // Smooth AFTEREQ to reduce async timing noise with COMPPEAK
            constexpr float kEqAlpha = 0.3f;
            m_afterEq = (m_afterEq < -140.0f) ? v : kEqAlpha * v + (1.0f - kEqAlpha) * m_afterEq;
        } else if (idx == m_compPeakIdx) {
            // Smooth COMPPEAK similarly
            constexpr float kCompAlpha = 0.3f;
            float smoothed = (m_compPeakRaw < -140.0f) ? v : kCompAlpha * v + (1.0f - kCompAlpha) * m_compPeakRaw;
            m_compPeakRaw = smoothed;
            // Gain reduction = input - output (both smoothed)
            // Only meaningful when both have signal
            if (m_afterEq > -140.0f && smoothed > -140.0f)
                m_compPeak = m_afterEq - smoothed;
            else
                m_compPeak = 0.0f;
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
        } else if (idx == m_tgxlFwdIdx) {
            m_tgxlFwdPwr = std::pow(10.0f, v / 10.0f) / 1000.0f;
            tgxlChanged = true;
        } else if (idx == m_tgxlSwrIdx) {
            float rho = std::pow(10.0f, -v / 20.0f);
            m_tgxlSwr = (rho < 0.999f) ? (1.0f + rho) / (1.0f - rho) : 99.9f;
            tgxlChanged = true;
        } else if (idx == m_ampFwdPwrIdx) {
            m_ampFwdPwr = std::pow(10.0f, v / 10.0f) / 1000.0f;  // dBm → watts
            ampChanged = true;
        } else if (idx == m_ampSwrIdx) {
            float rho = std::pow(10.0f, -v / 20.0f);
            m_ampSwr = (rho < 0.999f) ? (1.0f + rho) / (1.0f - rho) : 99.9f;
            ampChanged = true;
        } else if (idx == m_ampTempIdx) {
            m_ampTemp = v;
            ampChanged = true;
        }

        emit meterUpdated(idx, v);
    }

    // sLevelChanged is now emitted per-slice inline above
    if (txChanged)
        emit txMetersChanged(m_fwdPower, m_swr);
    if (micChanged)
        emit micMetersChanged(m_micLevel, m_compLevel, m_micPeak, m_compPeak);
    if (alcChanged)
        emit this->alcChanged(m_alc);
    if (hwChanged)
        emit hwTelemetryChanged(m_paTemp, m_supplyVolts);
    if (ampChanged)
        emit ampMetersChanged(m_ampFwdPwr, m_ampSwr, m_ampTemp);
    if (tgxlChanged)
        emit tgxlMetersChanged(m_tgxlFwdPwr, m_tgxlSwr);
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
