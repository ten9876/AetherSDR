#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QVector>
#include <QMap>

namespace AetherSDR {

class RadioConnection;

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

    // Bind a local UDP port (OS-chosen) and register it with the radio.
    // conn must remain valid for the lifetime of this stream.
    bool start(RadioConnection* conn);
    void stop();

    quint16 localPort() const { return m_localPort; }
    bool    isRunning() const;

    // Update the dBm range used to scale incoming FFT bins.
    // Called whenever the radio reports min_dbm / max_dbm for the panadapter.
    void setDbmRange(float minDbm, float maxDbm);

    // Send a VITA-49 packet to the radio via the registered UDP socket.
    // Must be called after start(). Returns bytes sent, or -1 on error.
    qint64 sendToRadio(const QByteArray& packet);

    // Set the stream IDs we own (filter out other clients' FFT/waterfall data).
    // panStreamId is the panadapter hex ID (e.g. 0x40000000).
    void setOwnedStreamIds(quint32 panStreamId, quint32 wfStreamId);

    // Register/unregister a VITA-49 stream ID as a DAX audio channel (1-4).
    // When a datagram's stream ID matches, audio is routed to daxAudioReady()
    // instead of audioDataReady().
    void registerDaxStream(quint32 streamId, int channel);
    void unregisterDaxStream(quint32 streamId);


signals:
    void spectrumReady(const QVector<float>& binsDbm);
    // One row of waterfall data (dBm values, Width bins).
    // lowFreqMhz / highFreqMhz describe the frequency span of this tile.
    // May be emitted multiple times per tile (once per Height row).
    void waterfallRowReady(const QVector<float>& binsDbm,
                           double lowFreqMhz, double highFreqMhz);
    // Emitted once per waterfall tile with the radio's computed auto black level.
    void waterfallAutoBlackLevel(quint32 autoBlack);
    // Raw PCM payload (header stripped) from IF-Data (audio) VITA-49 packets.
    // Format: 16-bit signed, stereo, 24 kHz, little-endian.
    void audioDataReady(const QByteArray& pcm);
    // Meter data: parallel arrays of (meter_index, raw_int16_value).
    void meterDataReady(const QVector<quint16>& ids, const QVector<qint16>& vals);
    // DAX audio for a specific channel (1-4). Same format as audioDataReady.
    void daxAudioReady(int channel, const QByteArray& pcm);

private slots:
    void onDatagramReady();

private:
    void processDatagram(const QByteArray& data);
    void decodeFFT(const uchar* raw, int totalBytes, bool hasTrailer);
    void decodeWaterfallTile(const uchar* raw, int totalBytes, bool hasTrailer);
    void decodeNarrowAudio(const uchar* raw, int totalBytes, bool hasTrailer);
    void decodeReducedBwAudio(const uchar* raw, int totalBytes, bool hasTrailer);
    void decodeMeterData(const uchar* raw, int totalBytes, bool hasTrailer);

    // PacketClassCodes (from FlexLib VitaFlex.cs)
    static constexpr quint16 PCC_IF_NARROW         = 0x03E3u; // float32 stereo, big-endian
    static constexpr quint16 PCC_IF_NARROW_REDUCED = 0x0123u; // int16 mono, big-endian
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

    WaterfallFrame m_wfFrame;

    // Per-stream packet sequence tracking (4-bit count in VITA-49 word0 bits 19:16)
    struct StreamStats {
        int  lastSeq{-1};
        int  errorCount{0};
        int  totalCount{0};
    };

    quint32         m_ownedPanStreamId{0};
    quint32         m_ownedWfStreamId{0};
    QUdpSocket      m_socket;
    quint16         m_localPort{0};
    float           m_minDbm{-130.0f};
    float           m_maxDbm{-20.0f};
    RadioConnection* m_conn{nullptr};
    FrameAssembler  m_frame;
    QMap<quint16, StreamStats> m_streamStats;  // keyed by PCC
    QMap<quint32, int> m_daxStreamIds;           // stream ID → DAX channel (1-4)

public:
    // Packet error/total counts across all streams (for network quality monitor).
    int packetErrorCount() const;
    int packetTotalCount() const;
    qint64 totalRxBytes() const { return m_totalRxBytes; }

private:
    qint64 m_totalRxBytes{0};
};

} // namespace AetherSDR
