#include "SliceModel.h"
#include <QDebug>

namespace AetherSDR {

SliceModel::SliceModel(int id, QObject* parent)
    : QObject(parent), m_id(id)
{}

// ─── Setters ──────────────────────────────────────────────────────────────────

// Helper: emit commandReady to send the command immediately (when connected),
// or queue it for when the connection becomes available.
void SliceModel::sendCommand(const QString& cmd)
{
    emit commandReady(cmd);
}

void SliceModel::setFrequency(double mhz)
{
    if (m_locked) return;           // software lock — block tune while locked
    if (qFuzzyCompare(m_frequency, mhz)) return;
    m_frequency = mhz;
    sendCommand(QString("slice tune %1 %2").arg(m_id).arg(mhz, 0, 'f', 6));
    emit frequencyChanged(mhz);
}

void SliceModel::setMode(const QString& mode)
{
    if (m_mode == mode) return;
    m_mode = mode;
    sendCommand(QString("slice set %1 mode=%2").arg(m_id).arg(mode));
    emit modeChanged(mode);
}

void SliceModel::setFilterWidth(int low, int high)
{
    m_filterLow  = low;
    m_filterHigh = high;
    // FlexAPI: "filt <id> <low_hz> <high_hz>"
    sendCommand(QString("filt %1 %2 %3").arg(m_id).arg(low).arg(high));
    emit filterChanged(low, high);
}

void SliceModel::setRxAntenna(const QString& ant)
{
    if (m_rxAntenna == ant) return;
    m_rxAntenna = ant;
    sendCommand(QString("slice set %1 rxant=%2").arg(m_id).arg(ant));
    emit rxAntennaChanged(ant);
}

void SliceModel::setTxAntenna(const QString& ant)
{
    if (m_txAntenna == ant) return;
    m_txAntenna = ant;
    sendCommand(QString("slice set %1 txant=%2").arg(m_id).arg(ant));
    emit txAntennaChanged(ant);
}

void SliceModel::setLocked(bool locked)
{
    m_locked = locked;
    // FlexAPI: "slice lock <id>" / "slice unlock <id>"
    sendCommand(locked ? QString("slice lock %1").arg(m_id)
                       : QString("slice unlock %1").arg(m_id));
    emit lockedChanged(locked);
}

void SliceModel::setQsk(bool on)
{
    m_qsk = on;
    sendCommand(QString("transmit set qsk_enabled=%1").arg(on ? 1 : 0));
    emit qskChanged(on);
}

void SliceModel::setNb(bool on)
{
    m_nb = on;
    sendCommand(QString("slice set %1 nb=%2").arg(m_id).arg(on ? 1 : 0));
    emit nbChanged(on);
}

void SliceModel::setNr(bool on)
{
    m_nr = on;
    sendCommand(QString("slice set %1 nr=%2").arg(m_id).arg(on ? 1 : 0));
    emit nrChanged(on);
}

void SliceModel::setAnf(bool on)
{
    m_anf = on;
    sendCommand(QString("slice set %1 anf=%2").arg(m_id).arg(on ? 1 : 0));
    emit anfChanged(on);
}

void SliceModel::setAgcMode(const QString& mode)
{
    if (m_agcMode == mode) return;
    m_agcMode = mode;
    sendCommand(QString("slice set %1 agc_mode=%2").arg(m_id).arg(mode));
    emit agcModeChanged(mode);
}

void SliceModel::setAgcThreshold(int value)
{
    value = qBound(0, value, 100);
    if (m_agcThreshold == value) return;
    m_agcThreshold = value;
    sendCommand(QString("slice set %1 agc_threshold=%2").arg(m_id).arg(value));
    emit agcThresholdChanged(value);
}

void SliceModel::setSquelch(bool on, int level)
{
    m_squelchOn    = on;
    m_squelchLevel = level;
    sendCommand(QString("slice set %1 squelch=%2 squelch_level=%3")
                    .arg(m_id).arg(on ? 1 : 0).arg(level));
    emit squelchChanged(on, level);
}

void SliceModel::setRit(bool on, int hz)
{
    m_ritOn   = on;
    m_ritFreq = hz;
    sendCommand(QString("slice set %1 rit_on=%2 rit_freq=%3")
                    .arg(m_id).arg(on ? 1 : 0).arg(hz));
    emit ritChanged(on, hz);
}

void SliceModel::setXit(bool on, int hz)
{
    m_xitOn   = on;
    m_xitFreq = hz;
    sendCommand(QString("slice set %1 xit_on=%2 xit_freq=%3")
                    .arg(m_id).arg(on ? 1 : 0).arg(hz));
    emit xitChanged(on, hz);
}

void SliceModel::setAudioGain(float gain)
{
    m_audioGain = qBound(0.0f, gain, 100.0f);
    sendCommand(QString("audio gain 0x%1 slice %2 %3")
                    .arg(0, 8, 16, QChar('0'))
                    .arg(m_id)
                    .arg(static_cast<int>(m_audioGain)));
}

void SliceModel::setRfGain(float gain)
{
    m_rfGain = gain;
    sendCommand(QString("slice set %1 rf_gain=%2").arg(m_id).arg(static_cast<int>(gain)));
}

void SliceModel::setAudioPan(int pan)
{
    pan = qBound(0, pan, 100);
    if (m_audioPan == pan) return;
    m_audioPan = pan;
    sendCommand(QString("slice set %1 audio_pan=%2").arg(m_id).arg(pan));
    emit audioPanChanged(pan);
}

// ─── Status updates from radio ────────────────────────────────────────────────

void SliceModel::applyStatus(const QMap<QString, QString>& kvs)
{
    bool freqChanged   = false;
    bool modeChanged_  = false;
    bool filterChanged_= false;

    // The radio sends the frequency as "RF_frequency" in status messages.
    if (kvs.contains("RF_frequency")) {
        const double f = kvs["RF_frequency"].toDouble();
        if (!qFuzzyCompare(m_frequency, f)) {
            m_frequency = f;
            freqChanged = true;
        }
    }
    if (kvs.contains("mode")) {
        const QString m = kvs["mode"];
        if (m_mode != m) {
            m_mode = m;
            modeChanged_ = true;
        }
    }
    if (kvs.contains("filter_lo") || kvs.contains("filter_hi")) {
        m_filterLow  = kvs.value("filter_lo",  QString::number(m_filterLow)).toInt();
        m_filterHigh = kvs.value("filter_hi", QString::number(m_filterHigh)).toInt();
        filterChanged_ = true;
    }
    if (kvs.contains("active"))
        m_active = kvs["active"] == "1";
    if (kvs.contains("tx"))
        m_txSlice = kvs["tx"] == "1";
    if (kvs.contains("rf_gain"))
        m_rfGain = kvs["rf_gain"].toFloat();
    if (kvs.contains("audio_gain"))
        m_audioGain = kvs["audio_gain"].toFloat();
    if (kvs.contains("audio_pan")) {
        m_audioPan = kvs["audio_pan"].toInt();
        emit audioPanChanged(m_audioPan);
    }

    // Slice control state
    if (kvs.contains("rxant")) {
        m_rxAntenna = kvs["rxant"];
        emit rxAntennaChanged(m_rxAntenna);
    }
    if (kvs.contains("txant")) {
        m_txAntenna = kvs["txant"];
        emit txAntennaChanged(m_txAntenna);
    }
    // Status key is "lock" (not "locked") per FlexAPI
    if (kvs.contains("lock")) {
        m_locked = kvs["lock"] == "1";
        emit lockedChanged(m_locked);
    }
    if (kvs.contains("qsk")) {
        m_qsk = kvs["qsk"] == "1";
        emit qskChanged(m_qsk);
    }
    if (kvs.contains("nb")) {
        m_nb = kvs["nb"] == "1";
        emit nbChanged(m_nb);
    }
    if (kvs.contains("nr")) {
        m_nr = kvs["nr"] == "1";
        emit nrChanged(m_nr);
    }
    if (kvs.contains("anf")) {
        m_anf = kvs["anf"] == "1";
        emit anfChanged(m_anf);
    }
    if (kvs.contains("agc_mode")) {
        m_agcMode = kvs["agc_mode"];
        emit agcModeChanged(m_agcMode);
    }
    if (kvs.contains("agc_threshold")) {
        m_agcThreshold = kvs["agc_threshold"].toInt();
        emit agcThresholdChanged(m_agcThreshold);
    }
    if (kvs.contains("squelch") || kvs.contains("squelch_level")) {
        if (kvs.contains("squelch"))       m_squelchOn    = kvs["squelch"] == "1";
        if (kvs.contains("squelch_level")) m_squelchLevel = kvs["squelch_level"].toInt();
        emit squelchChanged(m_squelchOn, m_squelchLevel);
    }
    if (kvs.contains("rit_on") || kvs.contains("rit_freq")) {
        if (kvs.contains("rit_on"))   m_ritOn   = kvs["rit_on"] == "1";
        if (kvs.contains("rit_freq")) m_ritFreq = kvs["rit_freq"].toInt();
        emit ritChanged(m_ritOn, m_ritFreq);
    }
    if (kvs.contains("xit_on") || kvs.contains("xit_freq")) {
        if (kvs.contains("xit_on"))   m_xitOn   = kvs["xit_on"] == "1";
        if (kvs.contains("xit_freq")) m_xitFreq = kvs["xit_freq"].toInt();
        emit xitChanged(m_xitOn, m_xitFreq);
    }

    if (freqChanged)    emit frequencyChanged(m_frequency);
    if (modeChanged_)   emit modeChanged(m_mode);
    if (filterChanged_) emit filterChanged(m_filterLow, m_filterHigh);
}

QStringList SliceModel::drainPendingCommands()
{
    QStringList cmds;
    cmds.swap(m_pendingCommands);
    return cmds;
}

} // namespace AetherSDR
