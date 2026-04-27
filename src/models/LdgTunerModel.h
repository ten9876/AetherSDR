#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

namespace AetherSDR {

class LdgTunerConnection;

// State model for an LDG autotuner connected via USB-to-TTL serial adapter.
//
// Manages connection state, tune-button integration settings, and the
// tune sequencing flow: LDG tune command → configurable delay → radio
// tune carrier. Emits signals for the radio model to start/stop the
// carrier and for the UI to show status.
//
// Settings are persisted via AppSettings with "Ldg" prefix.
class LdgTunerModel : public QObject {
    Q_OBJECT

public:
    // Tune-button integration modes
    enum class TuneMode {
        Disabled,       // normal radio-only tune
        FullTune,       // LDG Full Tune (F) + radio tune carrier
        MemoryTune      // LDG Memory Tune (T) + radio tune carrier
    };
    Q_ENUM(TuneMode)

    // Tuner model presets (for future calibration)
    enum class TunerPreset {
        Generic,
        LDG1000ProII,
        LDG600ProII
    };
    Q_ENUM(TunerPreset)

    // Tune result
    enum class TuneResult {
        None,
        Success,
        Fail,
        Timeout
    };
    Q_ENUM(TuneResult)

    explicit LdgTunerModel(QObject* parent = nullptr);

    // ── Getters ────────────────────────────────────────────────────────────
    bool        isConnected()   const { return m_connected; }
    bool        isTuning()      const { return m_tuning; }
    bool        isEnabled()     const { return m_enabled; }
    TuneMode    tuneMode()      const { return m_tuneMode; }
    TunerPreset tunerPreset()   const { return m_preset; }
    TuneResult  lastResult()    const { return m_lastResult; }
    bool        autoStop()      const { return m_autoStop; }
    int         tuneDelayMs()   const { return m_tuneDelayMs; }
    QString     portName()      const { return m_portName; }

    // ── Settings ───────────────────────────────────────────────────────────
    void setEnabled(bool on);
    void setTuneMode(TuneMode mode);
    void setTunerPreset(TunerPreset preset);
    void setAutoStop(bool on);
    void setTuneDelayMs(int ms);
    void setPortName(const QString& port);

    // ── Connection ─────────────────────────────────────────────────────────
    void setConnection(LdgTunerConnection* conn);
    void connectToTuner();
    void disconnectFromTuner();

    // ── Tune sequencing ────────────────────────────────────────────────────
    // Called before normal startTune(). Returns true if LDG tune was
    // initiated and the caller should defer the radio tune carrier until
    // startRadioTuneCarrier() is emitted.
    bool beginLdgTune();

    // Load/save settings from AppSettings.
    void loadSettings();
    void saveSettings();

signals:
    void stateChanged();
    void connectedChanged(bool connected);
    void tuningChanged(bool tuning);

    // Emitted after the configurable delay following an LDG tune command.
    // RadioModel should start the radio tune carrier when this fires.
    void startRadioTuneCarrier();

    // Emitted when the LDG tune finishes (success, fail, or timeout).
    // If autoStop is enabled, RadioModel should stop the radio tune carrier.
    void tuneFinished(bool success);

    // Emitted when the LDG connection is lost unexpectedly.
    void connectionLost(const QString& reason);

private slots:
    void onLdgConnected();
    void onLdgDisconnected();
    void onLdgTuneFinished(bool success);
    void onLdgError(const QString& msg);
    void onCarrierDelayTimeout();

private:
    LdgTunerConnection* m_conn{nullptr};
    bool        m_connected{false};
    bool        m_tuning{false};
    bool        m_enabled{false};
    TuneMode    m_tuneMode{TuneMode::Disabled};
    TunerPreset m_preset{TunerPreset::Generic};
    TuneResult  m_lastResult{TuneResult::None};
    bool        m_autoStop{true};
    int         m_tuneDelayMs{100};    // delay between LDG command and radio carrier
    QString     m_portName;

    QTimer      m_carrierDelayTimer;   // fires after tuneDelayMs to start radio carrier
};

} // namespace AetherSDR
