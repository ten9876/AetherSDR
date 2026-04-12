#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QHostAddress>
#include <QVector>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>

namespace AetherSDR {

class RadioConnection;
class OpusCodec;

// Receives all VITA-49 UDP datagrams from the radio on the single "client udpport"
// and routes them by PacketClassCode (bytes 14-15 of the VITA-49 class ID):
//   • PCC 0x03E3 → narrow audio, float32 stereo big-endian  → audioDataReady()
//   • PCC 0x0123 → narrow audio reduced-BW, int16 mono BE   → audioDataReady()
//   • PCC 0x8003 → panadapter FFT bins                      → spectrumReady()
//   • PCC 0x8004 → waterfall tiles (Width×Height uint16)    → waterfallRowReady()
//   • PCC 0x8002 → meter data (id/value pairs)             → meterDataReady()
//   • everything else → silently dropped
//
// All packets from the radio use ExtDataWithStream (VITA-49 type 3), not IFDataWithStream.
//
// Protocol:
//   1. Call start(conn) — binds port 4991 (LAN VITA port), falls back to OS-assigned.
//   2. Register the port with the radio via "client udpport <port>" (done by RadioModel).
//   3. The radio streams panadapter and audio to that port.

class PanadapterStream : public QObject {
    Q_OBJECT

public:
    static constexpr int VITA49_HEADER_BYTES = 28;

    explicit PanadapterStream(QObject* parent = nullptr);

    // Initialize socket and timer connections on the network thread.
    // Call after moveToThread() via QThread::started signal. (#561)
    Q_INVOKABLE void init();

    // Bind a local UDP port (OS-chosen) and register it with the radio.
    // conn must remain valid for the lifetime of this stream.
    // Q_INVOKABLE: must run on the network worker thread (#502)
    Q_INVOKABLE bool start(RadioConnection* conn);
    // Start for WAN: use explicit radio address and UDP port.
    Q_INVOKABLE bool startWan(const QHostAddress& radioAddr, quint16 radioUdpPort);

    // Begin WAN UDP registration: sends "client udp_register handle=0x<handle>"
    // via UDP every 50ms until VITA-49 packets arrive, then switches to
    // "client ping handle=0x<handle>" keepalive every 5 seconds.
    void startWanUdpRegister(quint32 clientHandle);
    void stop();

    QHostAddress localAddress() const { return m_localAddress; }
    quint16 localPort() const { return m_localPort; }
    bool    hasReceivedPackets() const { return m_hasReceivedPacket; }
    bool    isRunning() const;

    // Update the dBm range used to scale incoming FFT bins for a specific stream.
    void setDbmRange(quint32 streamId, float minDbm, float maxDbm);
    // Update the ypixels used to scale FFT bin values for a specific stream.
    // The radio encodes FFT bins as pixel Y positions (0 = top/max_dbm,
    // ypixels-1 = bottom/min_dbm), NOT as 0-65535 uint16 range.
    void setYPixels(quint32 streamId, int yPixels);

    // Register/unregister pan and waterfall stream IDs we own.
    // Only registered streams are processed; others are dropped.
    void registerPanStream(quint32 streamId);
    void registerWfStream(quint32 streamId);
    void unregisterPanStream(quint32 streamId);
    void unregisterWfStream(quint32 streamId);
    void clearRegisteredStreams();

    // DAX stream routing
    void registerDaxStream(quint32 streamId, int channel);
    void unregisterDaxStream(quint32 streamId);
    QList<quint32> daxStreamIds() const;

    // DAX IQ stream routing
    void registerIqStream(quint32 streamId, int channel);
    void unregisterIqStream(quint32 streamId);

    // Send a raw UDP datagram to the radio (used for DAX TX VITA-49 packets)
    void sendToRadio(const QByteArray& packet);

signals:
    void daxAudioReady(int channel, const QByteArray& pcm);
    void iqDataReady(int channel, const QByteArray& rawPayload, int sampleRate);
    void spectrumReady(quint32 streamId, const QVector<float>& binsDbm);
    // One row of waterfall data (intensity values, Width bins).
    void waterfallRowReady(quint32 streamId, const QVector<float>& binsDbm,
                           double lowFreqMhz, double highFreqMhz,
                           quint32 timecode);
    // Emitted once per waterfall tile with the radio's computed auto black level.
    void waterfallAutoBlackLevel(quint32 streamId, quint32 autoBlack);
    // Raw PCM payload (header stripped) from IF-Data (audio) VITA-49 packets.
    // Format: 16-bit signed, stereo, 24 kHz, little-endian.
    void audioDataReady(const QByteArray& pcm);
    // Meter data: parallel arrays of (meter_index, raw_int16_value).
    void meterDataReady(const QVector<quint16>& ids, const QVector<qint16>& vals);

private slots:
    void onDatagramReady();

private:
    void processDatagram(const QByteArray& data);
    void decodeFFT(const uchar* raw, int totalBytes, bool hasTrailer, quint32 streamId);
    void decodeWaterfallTile(const uchar* raw, int totalBytes, bool hasTrailer, quint32 streamId);
    void decodeNarrowAudio(const uchar* raw, int totalBytes, bool hasTrailer);
    void decodeReducedBwAudio(const uchar* raw, int totalBytes, bool hasTrailer);
    void decodeOpusAudio(const uchar* raw, int totalBytes, bool hasTrailer);
    void decodeMeterData(const uchar* raw, int totalBytes, bool hasTrailer);

    // PacketClassCodes (from FlexLib VitaFlex.cs)
    static constexpr quint16 PCC_IF_NARROW         = 0x03E3u; // float32 stereo, big-endian
    static constexpr quint16 PCC_IF_NARROW_REDUCED = 0x0123u; // int16 mono, big-endian
    static constexpr quint16 PCC_OPUS              = 0x8005u; // Opus compressed audio
    static constexpr quint16 PCC_FFT               = 0x8003u; // panadapter FFT bins
    static constexpr quint16 PCC_WATERFALL         = 0x8004u; // waterfall tiles
    static constexpr quint16 PCC_METER             = 0x8002u; // meter data

    // Frame assembly: a VITA-49 FFT frame may arrive in multiple UDP packets.
    // Each packet carries start_bin_index + num_bins so we can stitch them.
    struct FrameAssembler {
        quint32        frameIndex{0xFFFFFFFF};
        quint16        totalBins{0};
        quint16        binsReceived{0};
        QVector<quint16> buf;          // raw uint16 bins, host byte-order

        void reset(quint32 idx, quint16 total) {
            frameIndex   = idx;
            totalBins    = total;
            binsReceived = 0;
            buf.resize(total);
        }
        bool isComplete() const { return totalBins > 0 && binsReceived >= totalBins; }
    };

    // Waterfall frame assembly: tiles arrive in fragments across multiple packets.
    // Each packet carries firstBinIndex + width; totalBinsInFrame is constant.
    struct WaterfallFrame {
        quint32          timecode{0xFFFFFFFF};
        quint16          totalBins{0};
        quint16          binsReceived{0};
        double           lowFreqMhz{0};
        double           binBwMhz{0};
        quint32          autoBlack{0};
        QVector<float>   buf;   // intensity values (int16/128.0f)

        void reset(quint32 tc, quint16 total, double low, double bw, quint32 ab) {
            timecode     = tc;
            totalBins    = total;
            binsReceived = 0;
            lowFreqMhz   = low;
            binBwMhz     = bw;
            autoBlack    = ab;
            buf.resize(total);
            buf.fill(0.0f);
        }
        bool isComplete() const { return totalBins > 0 && binsReceived >= totalBins; }
    };

    QMap<quint32, WaterfallFrame> m_wfFrames;  // per-stream waterfall frame assembly

    // Per-stream packet sequence tracking (4-bit count in VITA-49 word0 bits 19:16)
    struct StreamStats {
        int  lastSeq{-1};
        int  errorCount{0};
        int  totalCount{0};
    };

public:
    // Per-category network statistics
    enum StreamCategory { CatAudio, CatFFT, CatWaterfall, CatMeter, CatDAX, CatCount };
    struct CategoryStats {
        qint64 bytes{0};
        int    packets{0};
        int    errors{0};
    };
    const CategoryStats& categoryStats(StreamCategory cat) const { return m_catStats[cat]; }
private:

    // Mutex guards stream ID sets, dBm ranges, yPixels, and DAX/IQ maps.
    // Written from main thread (RadioModel), read from network worker thread (#502).
    mutable QMutex  m_streamMutex;
    QSet<quint32>   m_knownPanStreams;     // registered pan stream IDs
    QSet<quint32>   m_knownWfStreams;     // registered wf stream IDs
    QUdpSocket      m_socket;
    quint16         m_localPort{0};
    QMap<quint32, QPair<float,float>> m_dbmRanges;  // streamId → (min, max)
    QMap<quint32, int> m_yPixels;  // streamId → ypixels for FFT bin scaling
    RadioConnection* m_conn{nullptr};
    QMap<quint32, FrameAssembler> m_frames;  // per-stream FFT frame assembly
    QMap<quint32, StreamStats> m_streamStats;  // keyed by stream ID
    CategoryStats m_catStats[CatCount]{};

public:
    // Packet error/total counts across all owned streams (for network quality monitor).
    int packetErrorCount() const;
    int packetTotalCount() const;
    qint64 totalRxBytes() const { return m_totalRxBytes; }
    qint64 totalTxBytes() const { return m_totalTxBytes; }

private:
    qint64 m_totalRxBytes{0};
    qint64 m_totalTxBytes{0};

    // Opus audio decoder (lazy-initialized on first Opus packet)
    OpusCodec* m_opusCodec{nullptr};

    // DAX stream routing: stream ID → DAX channel (1-4)
    QMap<quint32, int> m_daxStreamIds;
    // DAX IQ stream routing: stream ID → IQ channel (1-4)
    QMap<quint32, int> m_iqStreamIds;
    QHostAddress m_radioAddress;
    quint16      m_radioPort{0};
    QHostAddress m_localAddress;
    bool         m_hasReceivedPacket{false};

    // WAN UDP registration and keepalive
    QTimer   m_wanRegisterTimer;   // 50ms: "client udp_register" until first packet
    QTimer   m_wanPingTimer;       // 5s: "client ping" keepalive after registration
    QTimer   m_routedPrimeTimer;   // 250ms: non-WAN UDP prime retry until first packet
    bool     m_isWanMode{false};
    bool     m_wanRegistered{false};
    quint32  m_wanClientHandle{0};
    QElapsedTimer m_routedPrimeElapsed;
};

} // namespace AetherSDR
