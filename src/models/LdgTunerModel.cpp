#include "LdgTunerModel.h"
#include "core/LdgTunerConnection.h"
#include "core/AppSettings.h"
#include "core/LogManager.h"

#include <QDebug>

namespace AetherSDR {

LdgTunerModel::LdgTunerModel(QObject* parent)
    : QObject(parent)
{
    m_carrierDelayTimer.setSingleShot(true);
    connect(&m_carrierDelayTimer, &QTimer::timeout,
            this, &LdgTunerModel::onCarrierDelayTimeout);
}

// ── Settings ───────────────────────────────────────────────────────────────

void LdgTunerModel::setEnabled(bool on)
{
    if (m_enabled == on) return;
    m_enabled = on;
    emit stateChanged();
}

void LdgTunerModel::setTuneMode(TuneMode mode)
{
    if (m_tuneMode == mode) return;
    m_tuneMode = mode;
    emit stateChanged();
}

void LdgTunerModel::setTunerPreset(TunerPreset preset)
{
    if (m_preset == preset) return;
    m_preset = preset;
    emit stateChanged();
}

void LdgTunerModel::setAutoStop(bool on)
{
    if (m_autoStop == on) return;
    m_autoStop = on;
    emit stateChanged();
}

void LdgTunerModel::setTuneDelayMs(int ms)
{
    ms = qBound(0, ms, 5000);
    if (m_tuneDelayMs == ms) return;
    m_tuneDelayMs = ms;
    emit stateChanged();
}

void LdgTunerModel::setPortName(const QString& port)
{
    if (m_portName == port) return;
    m_portName = port;
    emit stateChanged();
}

// ── Connection ─────────────────────────────────────────────────────────────

void LdgTunerModel::setConnection(LdgTunerConnection* conn)
{
    if (m_conn == conn) return;

    if (m_conn) {
        QObject::disconnect(m_conn, nullptr, this, nullptr);
    }

    m_conn = conn;

    if (m_conn) {
        connect(m_conn, &LdgTunerConnection::connected,
                this, &LdgTunerModel::onLdgConnected);
        connect(m_conn, &LdgTunerConnection::disconnected,
                this, &LdgTunerModel::onLdgDisconnected);
        connect(m_conn, &LdgTunerConnection::tuneFinished,
                this, &LdgTunerModel::onLdgTuneFinished);
        connect(m_conn, &LdgTunerConnection::errorOccurred,
                this, &LdgTunerModel::onLdgError);
    }
}

void LdgTunerModel::connectToTuner()
{
    if (!m_conn || m_portName.isEmpty()) {
        qCWarning(lcTuner) << "LdgTunerModel: cannot connect — no connection or port name";
        return;
    }
    m_conn->connectToTuner(m_portName);
}

void LdgTunerModel::disconnectFromTuner()
{
    if (m_conn)
        m_conn->disconnect();
}

// ── Tune sequencing ────────────────────────────────────────────────────────

bool LdgTunerModel::beginLdgTune()
{
    if (!m_enabled || !m_connected || !m_conn)
        return false;

    if (m_tuneMode == TuneMode::Disabled)
        return false;

    if (m_tuning) {
        qCDebug(lcTuner) << "LdgTunerModel: already tuning, ignoring";
        return false;
    }

    m_tuning = true;
    m_lastResult = TuneResult::None;
    emit tuningChanged(true);

    // Send the appropriate LDG tune command
    if (m_tuneMode == TuneMode::FullTune) {
        qCInfo(lcTuner) << "LdgTunerModel: starting LDG Full Tune";
        m_conn->sendFullTune();
    } else {
        qCInfo(lcTuner) << "LdgTunerModel: starting LDG Memory Tune";
        m_conn->sendMemoryTune();
    }

    // Start delay timer — when it fires, we signal RadioModel to start the carrier
    m_carrierDelayTimer.setInterval(m_tuneDelayMs);
    m_carrierDelayTimer.start();

    return true;
}

// ── Persistence ────────────────────────────────────────────────────────────

void LdgTunerModel::loadSettings()
{
    auto& s = AppSettings::instance();
    m_enabled     = s.value("LdgEnabled", "False").toString() == "True";
    m_portName    = s.value("LdgSerialPort", "").toString();
    m_autoStop    = s.value("LdgAutoStop", "True").toString() == "True";
    m_tuneDelayMs = s.value("LdgTuneDelayMs", "100").toInt();

    QString mode = s.value("LdgTuneMode", "Disabled").toString();
    if (mode == "FullTune")       m_tuneMode = TuneMode::FullTune;
    else if (mode == "MemoryTune") m_tuneMode = TuneMode::MemoryTune;
    else                           m_tuneMode = TuneMode::Disabled;

    QString preset = s.value("LdgTunerPreset", "Generic").toString();
    if (preset == "LDG1000ProII")      m_preset = TunerPreset::LDG1000ProII;
    else if (preset == "LDG600ProII")  m_preset = TunerPreset::LDG600ProII;
    else                               m_preset = TunerPreset::Generic;

    emit stateChanged();
}

void LdgTunerModel::saveSettings()
{
    auto& s = AppSettings::instance();
    s.setValue("LdgEnabled", m_enabled ? "True" : "False");
    s.setValue("LdgSerialPort", m_portName);
    s.setValue("LdgAutoStop", m_autoStop ? "True" : "False");
    s.setValue("LdgTuneDelayMs", QString::number(m_tuneDelayMs));

    QString mode;
    switch (m_tuneMode) {
    case TuneMode::FullTune:    mode = "FullTune"; break;
    case TuneMode::MemoryTune:  mode = "MemoryTune"; break;
    default:                    mode = "Disabled"; break;
    }
    s.setValue("LdgTuneMode", mode);

    QString preset;
    switch (m_preset) {
    case TunerPreset::LDG1000ProII: preset = "LDG1000ProII"; break;
    case TunerPreset::LDG600ProII:  preset = "LDG600ProII"; break;
    default:                        preset = "Generic"; break;
    }
    s.setValue("LdgTunerPreset", preset);
    s.save();
}

// ── Private slots ──────────────────────────────────────────────────────────

void LdgTunerModel::onLdgConnected()
{
    m_connected = true;
    qCInfo(lcTuner) << "LdgTunerModel: LDG tuner connected on" << m_portName;
    emit connectedChanged(true);
    emit stateChanged();
}

void LdgTunerModel::onLdgDisconnected()
{
    bool wasTuning = m_tuning;
    m_connected = false;
    m_tuning = false;
    m_carrierDelayTimer.stop();
    qCInfo(lcTuner) << "LdgTunerModel: LDG tuner disconnected";
    emit connectedChanged(false);
    if (wasTuning) {
        m_lastResult = TuneResult::Fail;
        emit tuneFinished(false);
    }
    emit stateChanged();
}

void LdgTunerModel::onLdgTuneFinished(bool success)
{
    if (!m_tuning) return;
    m_tuning = false;
    m_lastResult = success ? TuneResult::Success : TuneResult::Fail;
    qCInfo(lcTuner) << "LdgTunerModel: tune finished,"
                    << (success ? "success" : "fail");
    emit tuningChanged(false);
    emit tuneFinished(success);
    emit stateChanged();

    // Resume meter streaming after tune
    if (m_conn && m_connected)
        m_conn->sendStreamingMode();
}

void LdgTunerModel::onLdgError(const QString& msg)
{
    qCWarning(lcTuner) << "LdgTunerModel: LDG error:" << msg;
    emit connectionLost(msg);
}

void LdgTunerModel::onCarrierDelayTimeout()
{
    if (!m_tuning) return;
    qCDebug(lcTuner) << "LdgTunerModel: carrier delay elapsed, requesting radio tune carrier";
    emit startRadioTuneCarrier();
}

} // namespace AetherSDR
