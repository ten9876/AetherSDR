#include "TransmitModel.h"
#include "core/LogManager.h"
#include <QDebug>

namespace AetherSDR {

TransmitModel::TransmitModel(QObject* parent)
    : QObject(parent)
{}

void TransmitModel::resetState()
{
    m_apdEnabled = false;
    m_apdConfigurable = false;
    m_apdEqActive = false;
    m_rfPower = 100;
    m_tunePower = 10;
    m_tune = false;
    m_mox = false;
    m_transmitting = false;
    m_maxPowerLevel = 100;
    m_atuEnabled = false;
    m_atuStatus = ATUStatus::None;
    m_memoriesEnabled = false;
    m_usingMemory = false;
    m_showTxInWaterfall = false;

    emit apdStateChanged();
    emit moxChanged(false);
    emit tuneChanged(false);
    emit micStateChanged();
}

// ── Status parsing ──────────────────────────────────────────────────────────

void TransmitModel::applyTransmitStatus(const QMap<QString, QString>& kvs)
{
    bool changed = false;
    bool tuneChanged_ = false;

    if (kvs.contains("rfpower")) {
        int v = qBound(0, kvs["rfpower"].toInt(), 100);
        if (m_rfPower != v) { m_rfPower = v; changed = true; }
    }
    if (kvs.contains("tunepower")) {
        int v = qBound(0, kvs["tunepower"].toInt(), 100);
        if (m_tunePower != v) { m_tunePower = v; changed = true; }
    }
    if (kvs.contains("tune")) {
        bool v = kvs["tune"] == "1";
        if (m_tune != v) { m_tune = v; changed = true; tuneChanged_ = true; }
    }
    if (kvs.contains("mox")) {
        bool v = kvs["mox"] == "1";
        if (m_mox != v) { m_mox = v; changed = true; }
    }

    // ── Mic / monitor / processor keys ──────────────────────────────────────
    bool micChanged = false;

    if (kvs.contains("mic_selection")) {
        QString v = kvs["mic_selection"].toUpper();
        if (m_micSelection != v) { m_micSelection = v; micChanged = true; }
    }
    if (kvs.contains("mic_level")) {
        int v = qBound(0, kvs["mic_level"].toInt(), 100);
        if (m_micLevel != v) { m_micLevel = v; micChanged = true; }
    }
    if (kvs.contains("mic_acc")) {
        bool v = kvs["mic_acc"] == "1";
        if (m_micAcc != v) { m_micAcc = v; micChanged = true; }
    }
    if (kvs.contains("speech_processor_enable")) {
        bool v = kvs["speech_processor_enable"] == "1";
        if (m_speechProcEnable != v) { m_speechProcEnable = v; micChanged = true; }
    }
    if (kvs.contains("speech_processor_level")) {
        int v = qBound(0, kvs["speech_processor_level"].toInt(), 100);
        if (m_speechProcLevel != v) { m_speechProcLevel = v; micChanged = true; }
    }
    if (kvs.contains("compander")) {
        bool v = kvs["compander"] == "1";
        if (m_companderOn != v) { m_companderOn = v; micChanged = true; }
    }
    if (kvs.contains("compander_level")) {
        int v = qBound(0, kvs["compander_level"].toInt(), 100);
        if (m_companderLevel != v) { m_companderLevel = v; micChanged = true; }
    }
    if (kvs.contains("dax")) {
        bool v = kvs["dax"] == "1";
        if (m_daxOn != v) { m_daxOn = v; micChanged = true; }
    }
    if (kvs.contains("sb_monitor")) {
        bool v = kvs["sb_monitor"] == "1";
        if (m_sbMonitor != v) { m_sbMonitor = v; micChanged = true; }
    }
    if (kvs.contains("mon_gain_sb")) {
        int v = qBound(0, kvs["mon_gain_sb"].toInt(), 100);
        if (m_monGainSb != v) { m_monGainSb = v; micChanged = true; }
    }

    // ── VOX keys ───────────────────────────────────────────────────────────
    bool phoneChanged = false;

    if (kvs.contains("vox_enable")) {
        bool v = kvs["vox_enable"] == "1";
        if (m_voxEnable != v) { m_voxEnable = v; phoneChanged = true; }
    }
    if (kvs.contains("vox_level")) {
        int v = qBound(0, kvs["vox_level"].toInt(), 100);
        if (m_voxLevel != v) { m_voxLevel = v; phoneChanged = true; }
    }
    if (kvs.contains("vox_delay")) {
        int v = qBound(0, kvs["vox_delay"].toInt(), 100);
        if (m_voxDelay != v) { m_voxDelay = v; phoneChanged = true; }
    }
    if (kvs.contains("mic_boost")) {
        bool v = kvs["mic_boost"] == "1";
        if (m_micBoost != v) { m_micBoost = v; phoneChanged = true; }
    }
    if (kvs.contains("mic_bias")) {
        bool v = kvs["mic_bias"] == "1";
        if (m_micBias != v) { m_micBias = v; phoneChanged = true; }
    }
    if (kvs.contains("met_in_rx")) {
        bool v = kvs["met_in_rx"] == "1";
        if (m_metInRx != v) { m_metInRx = v; changed = true; }
    }
    if (kvs.contains("synccwx")) {
        bool v = kvs["synccwx"] == "1";
        if (m_syncCwx != v) { m_syncCwx = v; phoneChanged = true; }
    }
    if (kvs.contains("am_carrier_level")) {
        int v = qBound(0, kvs["am_carrier_level"].toInt(), 100);
        if (m_amCarrierLevel != v) { m_amCarrierLevel = v; phoneChanged = true; }
    }
    if (kvs.contains("dexp")) {
        bool v = kvs["dexp"] == "1";
        if (m_dexpOn != v) { m_dexpOn = v; phoneChanged = true; }
    }
    if (kvs.contains("noise_gate_level")) {
        int v = qBound(0, kvs["noise_gate_level"].toInt(), 100);
        if (m_dexpLevel != v) { m_dexpLevel = v; phoneChanged = true; }
    }
    if (kvs.contains("lo")) {
        int v = qBound(0, kvs["lo"].toInt(), 10000);
        if (m_txFilterLow != v) { m_txFilterLow = v; phoneChanged = true; }
    }
    if (kvs.contains("hi")) {
        int v = qBound(0, kvs["hi"].toInt(), 10000);
        if (m_txFilterHigh != v) { m_txFilterHigh = v; phoneChanged = true; }
    }

    // ── CW keys ──────────────────────────────────────────────────────────
    if (kvs.contains("speed")) {
        int v = qBound(5, kvs["speed"].toInt(), 100);
        if (m_cwSpeed != v) { m_cwSpeed = v; phoneChanged = true; }
    }
    if (kvs.contains("pitch")) {
        int v = qBound(100, kvs["pitch"].toInt(), 6000);
        if (m_cwPitch != v) { m_cwPitch = v; phoneChanged = true; }
    }
    if (kvs.contains("break_in")) {
        bool v = kvs["break_in"] == "1";
        if (m_cwBreakIn != v) { m_cwBreakIn = v; phoneChanged = true; }
    }
    if (kvs.contains("break_in_delay")) {
        int v = qBound(0, kvs["break_in_delay"].toInt(), 2000);
        if (m_cwDelay != v) { m_cwDelay = v; phoneChanged = true; }
    }
    if (kvs.contains("sidetone")) {
        bool v = kvs["sidetone"] == "1";
        if (m_cwSidetone != v) { m_cwSidetone = v; phoneChanged = true; }
    }
    if (kvs.contains("iambic")) {
        bool v = kvs["iambic"] == "1";
        if (m_cwIambic != v) { m_cwIambic = v; phoneChanged = true; }
    }
    if (kvs.contains("iambic_mode")) {
        int v = qBound(0, kvs["iambic_mode"].toInt(), 1);
        if (m_cwIambicMode != v) { m_cwIambicMode = v; phoneChanged = true; }
    }
    if (kvs.contains("swap_paddles")) {
        bool v = kvs["swap_paddles"] == "1";
        if (m_cwSwapPaddles != v) { m_cwSwapPaddles = v; phoneChanged = true; }
    }
    if (kvs.contains("cwl_enabled")) {
        bool v = kvs["cwl_enabled"] == "1";
        if (m_cwlEnabled != v) { m_cwlEnabled = v; phoneChanged = true; }
    }
    if (kvs.contains("mon_gain_cw")) {
        int v = qBound(0, kvs["mon_gain_cw"].toInt(), 100);
        if (m_monGainCw != v) { m_monGainCw = v; phoneChanged = true; }
    }

    if (kvs.contains("max_power_level")) {
        int v = kvs["max_power_level"].toInt();
        if (m_maxPowerLevel != v) { m_maxPowerLevel = v; changed = true; emit maxPowerLevelChanged(v); }
    }
    if (kvs.contains("tune_mode")) {
        QString v = kvs["tune_mode"];
        if (m_tuneMode != v) { m_tuneMode = v; changed = true; }
    }
    if (kvs.contains("show_tx_in_waterfall")) {
        bool v = kvs["show_tx_in_waterfall"] == "1";
        if (m_showTxInWaterfall != v) { m_showTxInWaterfall = v; changed = true; }
    }

    if (changed) emit stateChanged();
    if (tuneChanged_) emit tuneChanged(m_tune);
    if (micChanged) emit micStateChanged();
    if (phoneChanged) emit phoneStateChanged();
}

void TransmitModel::applyInterlockStatus(const QMap<QString, QString>& kvs)
{
    if (kvs.contains("acc_tx_delay"))      m_accTxDelay = kvs["acc_tx_delay"].toInt();
    if (kvs.contains("tx1_delay"))         m_tx1Delay = kvs["tx1_delay"].toInt();
    if (kvs.contains("tx2_delay"))         m_tx2Delay = kvs["tx2_delay"].toInt();
    if (kvs.contains("tx3_delay"))         m_tx3Delay = kvs["tx3_delay"].toInt();
    if (kvs.contains("tx_delay"))          m_txDelay = kvs["tx_delay"].toInt();
    if (kvs.contains("timeout"))           m_interlockTimeout = kvs["timeout"].toInt();
    if (kvs.contains("acc_txreq_polarity"))m_accTxReqPolarity = kvs["acc_txreq_polarity"].toInt();
    if (kvs.contains("rca_txreq_polarity"))m_rcaTxReqPolarity = kvs["rca_txreq_polarity"].toInt();
}

void TransmitModel::applyAtuStatus(const QMap<QString, QString>& kvs)
{
    bool changed = false;

    if (kvs.contains("status")) {
        auto s = parseAtuTuneStatus(kvs["status"]);
        if (m_atuStatus != s) { m_atuStatus = s; changed = true; }
    }
    if (kvs.contains("atu_enabled")) {
        bool v = kvs["atu_enabled"] == "1";
        if (m_atuEnabled != v) { m_atuEnabled = v; changed = true; }
    }
    if (kvs.contains("memories_enabled")) {
        bool v = kvs["memories_enabled"] == "1";
        if (m_memoriesEnabled != v) { m_memoriesEnabled = v; changed = true; }
    }
    if (kvs.contains("using_mem")) {
        bool v = kvs["using_mem"] == "1";
        if (m_usingMemory != v) { m_usingMemory = v; changed = true; }
    }

    if (changed) emit atuStateChanged();
}

void TransmitModel::applyApdStatus(const QMap<QString, QString>& kvs)
{
    bool changed = false;

    if (kvs.contains("enable")) {
        bool v = kvs["enable"] == "1";
        if (m_apdEnabled != v) { m_apdEnabled = v; changed = true; }
    }
    if (kvs.contains("configurable")) {
        bool v = kvs["configurable"] == "1";
        if (m_apdConfigurable != v) { m_apdConfigurable = v; changed = true; }
    }
    if (kvs.contains("equalizer_active")) {
        bool v = kvs["equalizer_active"] == "1";
        if (m_apdEqActive != v) { m_apdEqActive = v; changed = true; }
    }

    if (changed) emit apdStateChanged();
}

void TransmitModel::setApdEnabled(bool on)
{
    if (m_apdEnabled != on) {
        m_apdEnabled = on;
        emit apdStateChanged();
    }
    emit commandReady(QString("apd enable=%1").arg(on ? 1 : 0));
}

void TransmitModel::setProfileList(const QStringList& profiles)
{
    if (m_profileList != profiles) {
        m_profileList = profiles;
        emit profileListChanged();
    }
}

void TransmitModel::setActiveProfile(const QString& profile)
{
    if (m_activeProfile != profile) {
        m_activeProfile = profile;
        emit stateChanged();
    }
}

// ── Commands ────────────────────────────────────────────────────────────────

void TransmitModel::setRfPower(int power)
{
    power = qBound(0, power, 100);
    emit commandReady(QString("transmit set rfpower=%1").arg(power));
}

void TransmitModel::setTunePower(int power)
{
    power = qBound(0, power, 100);
    emit commandReady(QString("transmit set tunepower=%1").arg(power));
}

void TransmitModel::startTune()
{
    emit commandReady("transmit tune 1");
}

void TransmitModel::stopTune()
{
    emit commandReady("transmit tune 0");
}

void TransmitModel::setMox(bool on)
{
    // Optimistic MOX edge gating keeps UI/audio aligned with user intent.
    // Interlock status from the radio will still reconcile final state.
    if (m_transmitting != on) {
        m_transmitting = on;
        emit moxChanged(on);
    }
    emit commandReady(QString("xmit %1").arg(on ? 1 : 0));
}

void TransmitModel::setTransmitting(bool tx)
{
    if (tx == m_transmitting) return;
    m_transmitting = tx;
    emit moxChanged(tx);
}

void TransmitModel::atuStart()
{
    emit commandReady("atu start");
}

void TransmitModel::atuBypass()
{
    emit commandReady("atu bypass");
}

void TransmitModel::setAtuMemories(bool on)
{
    emit commandReady(QString("atu set memories_enabled=%1").arg(on ? 1 : 0));
}

void TransmitModel::loadProfile(const QString& name)
{
    emit commandReady(QString("profile tx load \"%1\"").arg(name));
}

// ── Mic profile setters (called from RadioModel) ────────────────────────────

void TransmitModel::setMicProfileList(const QStringList& profiles)
{
    if (m_micProfileList != profiles) {
        m_micProfileList = profiles;
        emit micProfileListChanged();
    }
}

void TransmitModel::setActiveMicProfile(const QString& profile)
{
    if (m_activeMicProfile != profile) {
        m_activeMicProfile = profile;
        emit micStateChanged();
    }
}

void TransmitModel::setMicInputList(const QStringList& inputs)
{
    if (m_micInputList != inputs) {
        m_micInputList = inputs;
        emit micInputListChanged();
    }
}

// ── Mic / monitor / processor commands ──────────────────────────────────────

void TransmitModel::setMicSelection(const QString& input)
{
    emit commandReady(QString("mic input %1").arg(input.toUpper()));
}

void TransmitModel::setMicLevel(int level)
{
    level = qBound(0, level, 100);
    emit commandReady(QString("transmit set miclevel=%1").arg(level));
}

void TransmitModel::setMicAcc(bool on)
{
    emit commandReady(QString("mic acc %1").arg(on ? 1 : 0));
}

void TransmitModel::setSpeechProcessorEnable(bool on)
{
    // Pcap confirmed: SmartSDR uses speech_processor_enable (not compander).
    // Optimistic update: radio does not echo speech_processor_enable in
    // incremental status — only in the initial full dump on connect.
    m_speechProcEnable = on;
    emit micStateChanged();
    emit commandReady(QString("transmit set speech_processor_enable=%1").arg(on ? 1 : 0));
}

void TransmitModel::setSpeechProcessorLevel(int level)
{
    // NOR=0, DX=1, DX+=2 (pcap confirmed: speech_processor_level, not compander_level).
    // Optimistic update: radio does not echo in incremental status.
    level = qBound(0, level, 2);
    m_speechProcLevel = level;
    emit micStateChanged();
    emit commandReady(QString("transmit set speech_processor_level=%1").arg(level));
}

void TransmitModel::setDax(bool on)
{
    emit commandReady(QString("transmit set dax=%1").arg(on ? 1 : 0));
}

void TransmitModel::setSbMonitor(bool on)
{
    emit commandReady(QString("transmit set mon=%1").arg(on ? 1 : 0));
}

void TransmitModel::setMonGainSb(int gain)
{
    gain = qBound(0, gain, 100);
    emit commandReady(QString("transmit set mon_gain_sb=%1").arg(gain));
}

void TransmitModel::loadMicProfile(const QString& name)
{
    emit commandReady(QString("profile mic load \"%1\"").arg(name));
}

// ── VOX commands ────────────────────────────────────────────────────────────

void TransmitModel::setVoxEnable(bool on)
{
    m_voxEnable = on;  // optimistic update — radio may not echo
    emit commandReady(QString("transmit set vox_enable=%1").arg(on ? 1 : 0));
}

void TransmitModel::setVoxLevel(int level)
{
    level = qBound(0, level, 100);
    emit commandReady(QString("transmit set vox_level=%1").arg(level));
}

void TransmitModel::setVoxDelay(int delay)
{
    delay = qBound(0, delay, 100);
    emit commandReady(QString("transmit set vox_delay=%1").arg(delay));
}

void TransmitModel::setMicBoost(bool on)
{
    m_micBoost = on;  // optimistic — radio sends no status echo (#1045)
    emit phoneStateChanged();
    emit commandReady(QString("mic boost %1").arg(on ? 1 : 0));
}

void TransmitModel::setMicBias(bool on)
{
    m_micBias = on;  // optimistic — radio sends no status echo (#1045)
    emit phoneStateChanged();
    emit commandReady(QString("mic bias %1").arg(on ? 1 : 0));
}

void TransmitModel::setAmCarrierLevel(int level)
{
    level = qBound(0, level, 100);
    emit commandReady(QString("transmit set am_carrier=%1").arg(level));
}

void TransmitModel::setDexp(bool on)
{
    // Optimistic update — radio may not echo dexp in incremental status
    m_dexpOn = on;
    emit phoneStateChanged();
    emit commandReady(QString("transmit set dexp=%1").arg(on ? 1 : 0));
}

void TransmitModel::setDexpLevel(int level)
{
    level = qBound(0, level, 100);
    // Optimistic update — radio may not echo noise_gate_level in incremental status
    m_dexpLevel = level;
    emit phoneStateChanged();
    emit commandReady(QString("transmit set noise_gate_level=%1").arg(level));
}

void TransmitModel::setTxFilterLow(int hz)
{
    hz = qBound(0, hz, 10000);
    emit commandReady(QString("transmit set filter_low=%1 filter_high=%2")
                      .arg(hz).arg(m_txFilterHigh));
}

void TransmitModel::setTxFilterHigh(int hz)
{
    hz = qBound(0, hz, 10000);
    emit commandReady(QString("transmit set filter_low=%1 filter_high=%2")
                      .arg(m_txFilterLow).arg(hz));
}

// ── CW commands ─────────────────────────────────────────────────────────────

void TransmitModel::setCwSpeed(int wpm)
{
    wpm = qBound(5, wpm, 100);
    emit commandReady(QString("cw wpm %1").arg(wpm));
}

void TransmitModel::setCwPitch(int hz)
{
    hz = qBound(100, hz, 6000);
    m_cwPitch = hz;  // update local cache so rapid steppers accumulate
    emit commandReady(QString("cw pitch %1").arg(hz));
}

void TransmitModel::setCwBreakIn(bool on)
{
    if (m_cwBreakIn != on) {
        m_cwBreakIn = on;
        emit phoneStateChanged();
    }
    emit commandReady(QString("cw break_in %1").arg(on ? 1 : 0));
}

void TransmitModel::setCwDelay(int ms)
{
    ms = qBound(0, ms, 2000);
    emit commandReady(QString("cw break_in_delay %1").arg(ms));
}

void TransmitModel::setCwSidetone(bool on)
{
    emit commandReady(QString("cw sidetone %1").arg(on ? 1 : 0));
}

void TransmitModel::setCwIambic(bool on)
{
    emit commandReady(QString("cw iambic %1").arg(on ? 1 : 0));
}

void TransmitModel::setCwIambicMode(int mode)
{
    mode = qBound(0, mode, 1);
    emit commandReady(QString("cw mode %1").arg(mode));
}

void TransmitModel::setCwSwapPaddles(bool on)
{
    emit commandReady(QString("cw swap %1").arg(on ? 1 : 0));
}

void TransmitModel::setCwlEnabled(bool on)
{
    emit commandReady(QString("cw cwl_enabled %1").arg(on ? 1 : 0));
}

void TransmitModel::setMonGainCw(int gain)
{
    gain = qBound(0, gain, 100);
    emit commandReady(QString("transmit set mon_gain_cw=%1").arg(gain));
}

// ── Helpers ─────────────────────────────────────────────────────────────────

ATUStatus TransmitModel::parseAtuTuneStatus(const QString& s)
{
    // Values from FlexLib Radio.cs ParseATUTuneStatus()
    if (s == "NONE")               return ATUStatus::None;
    if (s == "TUNE_NOT_STARTED")   return ATUStatus::NotStarted;
    if (s == "TUNE_IN_PROGRESS")   return ATUStatus::InProgress;
    if (s == "TUNE_BYPASS")        return ATUStatus::Bypass;
    if (s == "TUNE_SUCCESSFUL")    return ATUStatus::Successful;
    if (s == "TUNE_OK")            return ATUStatus::OK;
    if (s == "TUNE_FAIL_BYPASS")   return ATUStatus::FailBypass;
    if (s == "TUNE_FAIL")          return ATUStatus::Fail;
    if (s == "TUNE_ABORTED")       return ATUStatus::Aborted;
    if (s == "TUNE_MANUAL_BYPASS") return ATUStatus::ManualBypass;
    qCDebug(lcTransmit) << "TransmitModel: unknown ATU status:" << s;
    return ATUStatus::None;
}

} // namespace AetherSDR
