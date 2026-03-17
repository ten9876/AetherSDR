#pragma once

#include "core/RadioConnection.h"
#include "core/WanConnection.h"
#include "core/PanadapterStream.h"
#include "SliceModel.h"
#include "MeterModel.h"
#include "TunerModel.h"
#include "TransmitModel.h"
#include "EqualizerModel.h"
#include "TnfModel.h"

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QSet>

namespace AetherSDR {

struct MemoryEntry {
    int     index{-1};
    QString group;
    QString owner;
    double  freq{0.0};
    QString name;
    QString mode;
    int     step{100};
    QString offsetDir;       // "simplex", "up", "down"
    double  repeaterOffset{0.0};
    QString toneMode;        // "off", "ctcss_tx", ...
    double  toneValue{0.0};
    bool    squelch{false};
    int     squelchLevel{0};
    int     rxFilterLow{0};
    int     rxFilterHigh{0};
    int     rttyMark{2125};
    int     rttyShift{170};
    int     diglOffset{2210};
    int     diguOffset{1500};
};

} // namespace AetherSDR
#include <QTimer>
#include <QElapsedTimer>

namespace AetherSDR {

// RadioModel is the central data model for a connected radio.
// It owns the RadioConnection, processes incoming status messages,
// and exposes the radio's current state to the GUI via Qt properties/signals.
class RadioModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString name        READ name        NOTIFY infoChanged)
    Q_PROPERTY(QString model       READ model       NOTIFY infoChanged)
    Q_PROPERTY(QString version     READ version     NOTIFY infoChanged)
    Q_PROPERTY(bool    connected   READ isConnected NOTIFY connectionStateChanged)
    Q_PROPERTY(float   paTemp      READ paTemp      NOTIFY metersChanged)
    Q_PROPERTY(float   txPower     READ txPower     NOTIFY metersChanged)

public:
    explicit RadioModel(QObject* parent = nullptr);

    // Access the underlying connection and panadapter stream
    RadioConnection*  connection()  { return &m_connection; }
    PanadapterStream* panStream()   { return &m_panStream; }
    MeterModel*       meterModel()  { return &m_meterModel; }
    TunerModel*       tunerModel()  { return &m_tunerModel; }
    TransmitModel*    transmitModel() { return &m_transmitModel; }
    EqualizerModel*   equalizerModel() { return &m_equalizerModel; }
    TnfModel*         tnfModel()       { return &m_tnfModel; }
    bool              hasAmplifier() const { return m_hasAmplifier; }

    // Getters
    QString name()    const { return m_name; }
    QString model()   const { return m_model; }
    QString version() const { return m_version; }
    bool isConnected() const;
    float paTemp()    const { return m_paTemp; }
    float txPower()   const { return m_txPower; }
    QStringList antennaList() const { return m_antList; }
    QString serial()       const;
    QString chassisSerial() const { return m_chassisSerial; }
    QString callsign()     const { return m_callsign; }
    QString nickname()     const { return m_nickname; }
    QString region()       const { return m_region; }
    QString radioOptions() const { return m_radioOptions; }
    QString ip()          const { return m_ip; }
    QString netmask()     const { return m_netmask; }
    QString gateway()     const { return m_gateway; }
    QString mac()         const { return m_mac; }
    bool    enforcePrivateIp() const { return m_enforcePrivateIp; }

    // GPS data
    QString gpsStatus()    const { return m_gpsStatus; }
    int     gpsTracked()   const { return m_gpsTracked; }
    int     gpsVisible()   const { return m_gpsVisible; }
    QString gpsGrid()      const { return m_gpsGrid; }
    QString gpsAltitude()  const { return m_gpsAltitude; }
    QString gpsLat()       const { return m_gpsLat; }
    QString gpsLon()       const { return m_gpsLon; }
    QString gpsTime()      const { return m_gpsTime; }
    QString gpsSpeed()     const { return m_gpsSpeed; }
    QString gpsFreqError() const { return m_gpsFreqError; }

    // Max slices reported by radio
    int maxSlices() const { return m_maxSlices; }

    // Oscillator / RX settings
    QString oscState()     const { return m_oscState; }
    QString oscSetting()   const { return m_oscSetting; }
    bool    oscLocked()    const { return m_oscLocked; }
    bool    extPresent()   const { return m_extPresent; }
    bool    gpsdoPresent() const { return m_gpsdoPresent; }
    bool    tcxoPresent()  const { return m_tcxoPresent; }
    bool    binauralRx()   const { return m_binauralRx; }
    bool    muteLocalWhenRemote() const { return m_muteLocalWhenRemote; }
    int     freqErrorPpb() const { return m_freqErrorPpb; }

    // Audio output
    int     lineoutGain()    const { return m_lineoutGain; }
    bool    lineoutMute()    const { return m_lineoutMute; }
    int     headphoneGain()  const { return m_headphoneGain; }
    bool    headphoneMute()  const { return m_headphoneMute; }
    bool    frontSpeakerMute() const { return m_frontSpeakerMute; }
    void setLineoutGain(int v);
    void setLineoutMute(bool m);
    void setHeadphoneGain(int v);
    void setHeadphoneMute(bool m);
    void setFrontSpeakerMute(bool m);
    int     filterSharpnessVoice()     const { return m_filterVoice; }
    bool    filterSharpnessVoiceAuto() const { return m_filterVoiceAuto; }
    int     filterSharpnessCw()        const { return m_filterCw; }
    bool    filterSharpnessCwAuto()    const { return m_filterCwAuto; }
    int     filterSharpnessDigital()   const { return m_filterDigital; }
    bool    filterSharpnessDigitalAuto() const { return m_filterDigitalAuto; }

    // Global profiles
    QStringList globalProfiles() const { return m_globalProfiles; }
    QString activeGlobalProfile() const { return m_activeGlobalProfile; }
    void loadGlobalProfile(const QString& name);
    void resetPanState();
    void createAudioStream();

    // Memory channel cache
    const QMap<int, MemoryEntry>& memories() const { return m_memories; }
    void handleMemoryStatus(int index, const QMap<QString, QString>& kvs);
    bool    lowLatencyDigital()        const { return m_lowLatencyDigital; }
    bool    hasStaticIp()     const { return m_hasStaticIp; }
    QString staticIp()        const { return m_staticIp; }
    QString staticNetmask()   const { return m_staticNetmask; }
    QString staticGateway()   const { return m_staticGateway; }
    bool    remoteOnEnabled() const { return m_remoteOnEnabled; }
    bool    multiFlexEnabled() const { return m_multiFlexEnabled; }
    void    setRemoteOnEnabled(bool on);
    void    setMultiFlexEnabled(bool on);
    double panCenterMhz()    const { return m_panCenterMhz; }
    double panBandwidthMhz() const { return m_panBandwidthMhz; }

    QList<SliceModel*> slices() const { return m_slices; }
    SliceModel* slice(int id) const;

    // High-level actions
    void connectToRadio(const RadioInfo& info);
    void connectViaWan(WanConnection* wan, const QString& publicIp, quint16 udpPort);
    void disconnectFromRadio();
    bool isWan() const { return m_wanConn != nullptr; }
    void setTransmit(bool tx);
    void addSlice();   // Create a new slice on the current panadapter
    void setPanBandwidth(double bandwidthMhz);
    void setPanCenter(double centerMhz);
    void setPanDbmRange(float minDbm, float maxDbm);
    void setPanWnb(bool on);
    void setPanWnbLevel(int level);
    void setPanRfGain(int gain);

    // Display controls — FFT (display pan set)
    void setPanAverage(int frames);
    void setPanFps(int fps);
    void setPanWeightedAverage(bool on);

    // Display controls — Waterfall (display panafall set)
    void setWaterfallColorGain(int gain);
    void setWaterfallBlackLevel(int level);
    void setWaterfallAutoBlack(bool on);
    void setWaterfallLineDuration(int ms);

    // Display controls — Noise floor
    void setPanNoiseFloorPosition(int pos);
    void setPanNoiseFloorEnable(bool on);

signals:
    void infoChanged();
    void connectionStateChanged(bool connected);
    void sliceAdded(SliceModel* slice);
    void sliceRemoved(int sliceId);
    void metersChanged();
    void connectionError(const QString& msg);
    // Emitted when a panadapter's center frequency or bandwidth changes.
    void panadapterInfoChanged(double centerMhz, double bandwidthMhz);
    // Emitted when the radio reports the panadapter's dBm display range.
    void panadapterLevelChanged(float minDbm, float maxDbm);
    // Emitted when the radio reports its antenna list (e.g. "ANT1,ANT2,RX_A,RX_B").
    void antListChanged(QStringList ants);
    // Emitted when a power amplifier (e.g. PGXL) is detected or lost.
    void amplifierChanged(bool present);
    void audioOutputChanged();
    // Emitted when GPS status changes (from "sub gps all").
    void gpsStatusChanged(const QString& status, int tracked, int visible,
                          const QString& grid, const QString& altitude,
                          const QString& lat, const QString& lon,
                          const QString& utcTime);
    // Emitted when network quality assessment changes.
    // quality: "Excellent", "Very Good", "Good", "Fair", "Poor"
    // pingMs: round-trip time in milliseconds
    void networkQualityChanged(const QString& quality, int pingMs);
    // Emitted when the radio assigns a TX audio stream ID.
    void txAudioStreamReady(quint32 streamId);
    // Emitted when global profile list or active profile changes.
    void globalProfilesChanged();
    // Generic status relay — for dialogs that need to listen for specific objects.
    void statusReceived(const QString& object, const QMap<QString, QString>& kvs);

public:
    // Send a raw command to the radio (for dialogs that need direct protocol access).
    void sendCommand(const QString& cmd);

private slots:
    void onStatusReceived(const QString& object, const QMap<QString, QString>& kvs);
    void onMessageReceived(const ParsedMessage& msg);
    void onConnected();
    void onDisconnected();
    void onConnectionError(const QString& msg);
    void onVersionReceived(const QString& version);

private:
    void handleRadioStatus(const QMap<QString, QString>& kvs);
    void handleSliceStatus(int id, const QMap<QString, QString>& kvs, bool removed);
    void handleMeterStatus(const QString& rawBody);
    void handlePanadapterStatus(const QMap<QString, QString>& kvs);
    void handleProfileStatus(const QString& object, const QMap<QString, QString>& kvs);
    void handleProfileStatusRaw(const QString& profileType, const QString& rawBody);

    void configurePan();
    void configureWaterfall();
    void registerAsGuiClient(const QString& clientId);

    // Route command to active connection (LAN or WAN)
    using ResponseCallback = RadioConnection::ResponseCallback;
    quint32 sendCmd(const QString& command, ResponseCallback cb = nullptr);
    quint32 clientHandle() const;
    void updateStreamFilters();
    void handleGpsStatus(const QString& rawBody);

    // Standalone mode: create a panadapter then attach a slice to it.
    void createDefaultSlice(const QString& freqMhz = "14.225000",
                            const QString& mode    = "USB",
                            const QString& antenna = "ANT1");

    RadioConnection  m_connection;
    PanadapterStream m_panStream;
    MeterModel       m_meterModel;
    TunerModel       m_tunerModel;
    TransmitModel    m_transmitModel;
    EqualizerModel   m_equalizerModel;
    TnfModel         m_tnfModel;

    QString     m_name;
    QString     m_model;
    int         m_maxSlices{4};
    QString     m_version;          // software version from discovery (e.g. "4.1.5")
    QString     m_protocolVersion;  // protocol version from V line (e.g. "1.4.0.0")
    float       m_paTemp{0.0f};
    float       m_txPower{0.0f};
    QString     m_chassisSerial;
    QString     m_callsign;
    QString     m_nickname;
    QString     m_region;
    QString     m_radioOptions;
    QString     m_ip;
    QString     m_netmask;
    QString     m_gateway;
    QString     m_mac;
    bool        m_hasStaticIp{false};
    QString     m_staticIp;
    QString     m_staticNetmask;
    QString     m_staticGateway;
    // Oscillator state
    QString     m_oscState;
    QString     m_oscSetting{"auto"};
    bool        m_oscLocked{false};
    bool        m_extPresent{false};
    bool        m_gpsdoPresent{false};
    bool        m_tcxoPresent{false};
    bool        m_binauralRx{false};
    bool        m_muteLocalWhenRemote{false};
    int         m_lineoutGain{50};
    bool        m_lineoutMute{false};
    int         m_headphoneGain{50};
    bool        m_headphoneMute{false};
    bool        m_frontSpeakerMute{false};
    int         m_freqErrorPpb{0};
    int         m_filterVoice{2};
    bool        m_filterVoiceAuto{false};
    int         m_filterCw{2};
    bool        m_filterCwAuto{true};
    int         m_filterDigital{2};
    bool        m_filterDigitalAuto{true};
    bool        m_lowLatencyDigital{true};
    bool        m_enforcePrivateIp{true};
    bool        m_remoteOnEnabled{false};
    bool        m_multiFlexEnabled{true};
    QStringList m_antList;

    double  m_panCenterMhz{14.225};
    double  m_panBandwidthMhz{0.200};
    QString m_panId;             // e.g. "0x40000000", empty until first status
    QString m_waterfallId;       // e.g. "0x42000000", from display waterfall status
    bool    m_panResized{false}; // true once we've sent the resize command
    bool    m_wfConfigured{false};

    bool    m_hasAmplifier{false};  // true if a power amp (PGXL) is detected

    // GPS state
    QString m_gpsStatus;           // "Locked", "Present", "Not Present"
    int     m_gpsTracked{0};
    int     m_gpsVisible{0};
    QString m_gpsGrid;
    QString m_gpsAltitude;
    QString m_gpsLat;
    QString m_gpsLon;
    QString m_gpsTime;
    QString m_gpsSpeed;
    QString m_gpsFreqError;

    // Per-band TX settings (from "transmit band" and "interlock band" status)
    struct TxBandInfo {
        int     bandId{0};
        QString bandName;
        int     rfPower{100};
        int     tunePower{10};
        bool    inhibit{false};
        bool    hwAlc{false};
        bool    accTxReq{false};
        bool    rcaTxReq{false};
        bool    accTx{false};
        bool    tx1{false};
        bool    tx2{false};
        bool    tx3{false};
    };
    QMap<int, TxBandInfo> m_txBandSettings;

public:
    const QMap<int, TxBandInfo>& txBandSettings() const { return m_txBandSettings; }

    struct XvtrInfo {
        int     index{0};
        QString name;
        double  rfFreq{0.0};
        double  ifFreq{0.0};
        double  loError{0.0};
        double  rxGain{0.0};
        double  maxPower{10.0};
        bool    rxOnly{false};
        bool    isValid{false};
    };
    const QMap<int, XvtrInfo>& xvtrList() const { return m_xvtrList; }

private:
    QMap<int, XvtrInfo> m_xvtrList;

private:
    QList<SliceModel*> m_slices;
    QMap<int, MemoryEntry> m_memories;
    QStringList m_globalProfiles;
    QString     m_activeGlobalProfile;
    QString     m_rxAudioStreamId;
    WanConnection* m_wanConn{nullptr};  // non-null when connected via SmartLink
    QString  m_wanPublicIp;
    quint16  m_wanUdpPort{4991};
    QSet<int>          m_ownedSliceIds;   // slice IDs that belong to our client

    RadioInfo m_lastInfo;               // stored for auto-reconnect
    bool      m_intentionalDisconnect{false};
    QTimer    m_reconnectTimer;

    // ── Network quality monitor (matches FlexLib MonitorNetworkQuality) ──
    void startNetworkMonitor();
    void stopNetworkMonitor();
    void evaluateNetworkQuality();

    enum class NetState { Off, Excellent, VeryGood, Good, Fair, Poor };
    static constexpr int LAN_PING_FAIR_MS = 50;
    static constexpr int LAN_PING_POOR_MS = 100;

    QTimer        m_pingTimer;           // 1-second interval
    QElapsedTimer m_pingStopwatch;       // measures RTT
    int           m_lastPingRtt{0};      // ms
    int           m_maxPingRtt{0};       // max RTT seen this session
    int           m_lastErrorCount{0};   // snapshot for delta
    NetState      m_netState{NetState::Off};
    NetState      m_nextState{NetState::Off};
    int           m_stateCountdown{0};

    // Network diagnostics — byte counters for rate calculation
    qint64        m_txBytes{0};          // total TCP bytes sent

public:
    // Network diagnostics getters
    int     lastPingRtt()      const { return m_lastPingRtt; }
    int     maxPingRtt()       const { return m_maxPingRtt; }
    QString networkQuality()   const;
    int     packetDropCount()  const;
    int     packetTotalCount() const;
    qint64  rxBytes()          const;
    qint64  txBytes()          const { return m_txBytes; }
    void    addTxBytes(qint64 n) { m_txBytes += n; }
};

} // namespace AetherSDR
