#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QTimer>
#include <QList>
#include <QMap>
#include <QString>
#include <QHostAddress>

namespace AetherSDR {

// Represents a discovered FlexRadio on the network.
struct RadioInfo {
    QString name;           // e.g. "FLEX-6600"
    QString model;
    QString serial;
    QString version;
    QString nickname;
    QString callsign;
    QHostAddress address;
    quint16 port{4992};
    QString status;         // "Available" | "In_Use" | etc.
    int maxLicensedVersion{0};
    bool inUse{false};
    bool isRouted{false};

    // Connected GUI client info (from discovery broadcast)
    QStringList guiClientStations;
    QStringList guiClientHandles;
    QStringList guiClientPrograms;

    QString displayName() const {
        QString suffix;
        if (!guiClientStations.isEmpty()) {
            const QString& station = guiClientStations.first();
            suffix = QString("Multi-Flex: %1").arg(station.isEmpty() ? "unknown" : station);
        } else {
            suffix = isRouted ? "routed" : "Local";
        }
        if (nickname.isEmpty() && callsign.isEmpty())
            return QString("%1 @ %2\nAvailable (%3)")
                .arg(model, address.toString(), suffix);
        return QString("%1  %2  %3\nAvailable (%4)")
            .arg(model, nickname, callsign, suffix);
    }
};

// Listens for SmartSDR discovery broadcasts on UDP port 4992
// and emits radioDiscovered / radioLost signals as radios appear/disappear.
class RadioDiscovery : public QObject {
    Q_OBJECT

public:
    static constexpr quint16 DISCOVERY_PORT  = 4992;
    static constexpr int STALE_TIMEOUT_MS   = 5000;  // radio considered gone after 5s
    static constexpr int BIND_RETRY_MS      = 2000;  // retry interval when bind fails
    static constexpr int MAX_BIND_RETRIES   = 15;    // give up after 30s (15 × 2s)
    static constexpr int REBIND_INTERVAL_MS = 5000;  // re-bind interval until first packet received

    explicit RadioDiscovery(QObject* parent = nullptr);
    ~RadioDiscovery() override;

    void startListening();
    void stopListening();

    QList<RadioInfo> discoveredRadios() const { return m_radios; }

signals:
    void radioDiscovered(const RadioInfo& radio);
    void radioUpdated(const RadioInfo& radio);
    void radioLost(const QString& serial);

private slots:
    void onReadyRead();
    void onStaleCheck();
    void onBindRetry();

private:
    RadioInfo parseDiscoveryPacket(const QByteArray& data) const;
    void upsertRadio(const RadioInfo& info);

    QUdpSocket        m_socket;
    QTimer            m_staleTimer;
    QTimer            m_bindRetryTimer;  // retries bind if first attempt fails (e.g. macOS net consent)
    QTimer            m_rebindTimer;     // periodic re-bind until first packet (handles interface changes)
    int               m_bindRetryCount{0};
    bool              m_receivedAny{false};
    QList<RadioInfo>  m_radios;

    // Track last-seen time per serial for staleness detection
    QMap<QString, qint64> m_lastSeen;
};

} // namespace AetherSDR
