#pragma once

#include <QObject>
#include <QString>
#include <QMap>

namespace AetherSDR {

// A "slice" in SmartSDR terminology is an independent receive channel.
// Each slice has its own frequency, mode, filter, and audio settings.
class SliceModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(int    sliceId    READ sliceId)
    Q_PROPERTY(double frequency  READ frequency  WRITE setFrequency  NOTIFY frequencyChanged)
    Q_PROPERTY(QString mode      READ mode       WRITE setMode       NOTIFY modeChanged)
    Q_PROPERTY(int filterLow     READ filterLow  NOTIFY filterChanged)
    Q_PROPERTY(int filterHigh    READ filterHigh NOTIFY filterChanged)
    Q_PROPERTY(bool active       READ isActive   NOTIFY activeChanged)
    Q_PROPERTY(bool txSlice      READ isTxSlice  NOTIFY txSliceChanged)

public:
    explicit SliceModel(int id, QObject* parent = nullptr);

    // Getters
    int     sliceId()    const { return m_id; }
    double  frequency()  const { return m_frequency; }   // MHz
    QString mode()       const { return m_mode; }
    int     filterLow()  const { return m_filterLow; }   // Hz offset
    int     filterHigh() const { return m_filterHigh; }
    bool    isActive()   const { return m_active; }
    bool    isTxSlice()  const { return m_txSlice; }
    float   rfGain()     const { return m_rfGain; }
    float   audioGain()  const { return m_audioGain; }
    int     audioPan()   const { return m_audioPan; }

    // Getters — RX DSP state
    QString rxAntenna()   const { return m_rxAntenna; }
    QString txAntenna()   const { return m_txAntenna; }
    bool    isLocked()    const { return m_locked; }
    bool    qskOn()       const { return m_qsk; }
    bool    nbOn()        const { return m_nb; }
    bool    nrOn()        const { return m_nr; }
    bool    anfOn()       const { return m_anf; }
    QString agcMode()      const { return m_agcMode; }
    int     agcThreshold() const { return m_agcThreshold; }
    bool    squelchOn()   const { return m_squelchOn; }
    int     squelchLevel()const { return m_squelchLevel; }
    bool    ritOn()       const { return m_ritOn; }
    int     ritFreq()     const { return m_ritFreq; }
    bool    xitOn()       const { return m_xitOn; }
    int     xitFreq()     const { return m_xitFreq; }

    // Setters (emit signals AND send radio commands)
    void setFrequency(double mhz);
    void setMode(const QString& mode);
    void setFilterWidth(int low, int high);
    void setAudioGain(float gain);
    void setRfGain(float gain);
    void setAudioPan(int pan);
    void setRxAntenna(const QString& ant);
    void setTxAntenna(const QString& ant);
    void setLocked(bool locked);
    void setQsk(bool on);
    void setNb(bool on);
    void setNr(bool on);
    void setAnf(bool on);
    void setAgcMode(const QString& mode);
    void setAgcThreshold(int value);
    void setSquelch(bool on, int level);
    void setRit(bool on, int hz);
    void setXit(bool on, int hz);

    // Apply a batch of KV pairs from a status message.
    void applyStatus(const QMap<QString, QString>& kvs);

    // Drain pending outgoing commands (called by RadioModel to send them)
    QStringList drainPendingCommands();

signals:
    void frequencyChanged(double mhz);
    void modeChanged(const QString& mode);
    void filterChanged(int low, int high);
    void activeChanged(bool active);
    void txSliceChanged(bool tx);
    void audioPanChanged(int pan);
    void rxAntennaChanged(const QString& ant);
    void txAntennaChanged(const QString& ant);
    void lockedChanged(bool locked);
    void qskChanged(bool on);
    void nbChanged(bool on);
    void nrChanged(bool on);
    void anfChanged(bool on);
    void agcModeChanged(const QString& mode);
    void agcThresholdChanged(int value);
    void squelchChanged(bool on, int level);
    void ritChanged(bool on, int hz);
    void xitChanged(bool on, int hz);
    void commandReady(const QString& cmd);  // ready to send to radio

private:
    int     m_id{0};
    double  m_frequency{0.0};
    QString m_mode{"USB"};
    int     m_filterLow{-1500};
    int     m_filterHigh{1500};
    bool    m_active{false};
    bool    m_txSlice{false};
    float   m_rfGain{0.0f};
    float   m_audioGain{50.0f};
    int     m_audioPan{50};

    // Slice control state
    QString m_rxAntenna{"ANT1"};
    QString m_txAntenna{"ANT1"};
    bool    m_locked{false};
    bool    m_qsk{false};
    bool    m_nb{false};
    bool    m_nr{false};
    bool    m_anf{false};
    QString m_agcMode{"med"};
    int     m_agcThreshold{65};
    bool    m_squelchOn{false};
    int     m_squelchLevel{20};
    bool    m_ritOn{false};
    int     m_ritFreq{0};
    bool    m_xitOn{false};
    int     m_xitFreq{0};

    void sendCommand(const QString& cmd);

    QStringList m_pendingCommands;
};

} // namespace AetherSDR
