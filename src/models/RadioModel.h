#pragma once

#include "core/RadioConnection.h"
#include "core/PanadapterStream.h"
#include "SliceModel.h"
#include "MeterModel.h"
#include "TunerModel.h"
#include "TransmitModel.h"
#include "EqualizerModel.h"

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QTimer>

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
    bool              hasAmplifier() const { return m_hasAmplifier; }

    // Getters
    QString name()    const { return m_name; }
    QString model()   const { return m_model; }
    QString version() const { return m_version; }
    bool isConnected() const;
    float paTemp()    const { return m_paTemp; }
    float txPower()   const { return m_txPower; }
    QStringList antennaList() const { return m_antList; }

    QList<SliceModel*> slices() const { return m_slices; }
    SliceModel* slice(int id) const;

    // High-level actions
    void connectToRadio(const RadioInfo& info);
    void disconnectFromRadio();
    void setTransmit(bool tx);

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

    QString     m_name;
    QString     m_model;
    QString     m_version;
    float       m_paTemp{0.0f};
    float       m_txPower{0.0f};
    QStringList m_antList;

    double  m_panCenterMhz{14.225};
    double  m_panBandwidthMhz{0.200};
    QString m_panId;             // e.g. "0x40000000", empty until first status
    QString m_waterfallId;       // e.g. "0x42000000", from display waterfall status
    bool    m_panResized{false}; // true once we've sent the resize command
    bool    m_wfConfigured{false};

    bool    m_hasAmplifier{false};  // true if a power amp (PGXL) is detected

    QList<SliceModel*> m_slices;

    RadioInfo m_lastInfo;               // stored for auto-reconnect
    bool      m_intentionalDisconnect{false};
    QTimer    m_reconnectTimer;
};

} // namespace AetherSDR
