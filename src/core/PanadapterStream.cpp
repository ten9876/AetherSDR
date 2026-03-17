#include "PanadapterStream.h"
#include "RadioConnection.h"

#include <QNetworkDatagram>
#include <QHostAddress>
#include <QtEndian>
#include <QSet>
#include <QDebug>
#include <cstring>

namespace AetherSDR {

// ─── VITA-49 header layout (28 bytes, big-endian) ─────────────────────────────
// Word 0 (bytes  0- 3): Packet header (type=3 ExtData, flags, count, size)
// Word 1 (bytes  4- 7): Stream ID
// Word 2 (bytes  8-11): Class ID OUI
// Word 3 (bytes 12-15): Class ID — InformationClassCode[15:0] | PacketClassCode[15:0]
// Word 4 (bytes 16-19): Integer timestamp
// Word 5 (bytes 20-23): Fractional timestamp (upper)
// Word 6 (bytes 24-27): Fractional timestamp (lower)
// Byte 28+            : Payload
//
// All FLEX radio streams use ExtDataWithStream (type 3), including audio.
// Audio is identified by PacketClassCode (lower 16 bits of word 3):
//   0x03E3 — SL_VITA_IF_NARROW_CLASS        — float32 stereo, big-endian
//   0x0123 — SL_VITA_IF_NARROW_REDUCED_BW   — int16 mono, big-endian
//   0x8005 — SL_VITA_OPUS_CLASS             — Opus compressed (not yet handled)
//
// Panadapter FFT: PCC = 0x8003 (SL_VITA_FFT_CLASS)
// Waterfall tile: PCC = 0x8004 (SL_VITA_WATERFALL_CLASS)

PanadapterStream::PanadapterStream(QObject* parent)
    : QObject(parent)
{
    connect(&m_socket, &QUdpSocket::readyRead,
            this, &PanadapterStream::onDatagramReady);
}

bool PanadapterStream::isRunning() const
{
    return m_socket.state() == QAbstractSocket::BoundState;
}

bool PanadapterStream::start(RadioConnection* conn)
{
    if (isRunning()) return true;

    static constexpr quint16 LAN_VITA_PORT = 4991;
    bool bound = m_socket.bind(QHostAddress::AnyIPv4, LAN_VITA_PORT,
                               QAbstractSocket::ReuseAddressHint);
    if (bound)
        qDebug() << "PanadapterStream: bound to LAN VITA-49 port" << LAN_VITA_PORT;
    else {
        qDebug() << "PanadapterStream: port" << LAN_VITA_PORT
                 << "unavailable, using OS-assigned port";
        bound = m_socket.bind(QHostAddress::AnyIPv4, 0);
    }
    if (!bound) {
        qWarning() << "PanadapterStream: failed to bind UDP socket:"
                   << m_socket.errorString();
        return false;
    }

    m_localPort = m_socket.localPort();
    qDebug() << "PanadapterStream: bound to UDP port" << m_localPort;

    // Send a one-byte UDP registration datagram to the radio's VITA-49 port.
    // The radio learns our IP:port from the source address of this datagram.
    // This is required on firmware v1.4.0.0 where the TCP "client udpport"
    // command may return 0x50001000 ("command not supported").
    const QHostAddress radioAddr = conn->radioAddress();
    if (!radioAddr.isNull()) {
        const QByteArray reg(1, '\x00');
        const qint64 sent = m_socket.writeDatagram(reg, radioAddr, 4992);
        if (sent == 1)
            qDebug() << "PanadapterStream: sent UDP registration to"
                     << radioAddr.toString() << ":4992";
        else
            qWarning() << "PanadapterStream: UDP registration send failed:"
                       << m_socket.errorString();
    } else {
        qWarning() << "PanadapterStream: radio address unknown — skipping UDP registration";
    }

    // Store radio address for sendToRadio (DAX TX path)
    m_radioAddress = radioAddr;
    m_radioPort = 4991;

    m_conn = conn;
    return true;
}

bool PanadapterStream::startWan(const QHostAddress& radioAddr, quint16 radioUdpPort)
{
    if (isRunning()) return true;

    // For WAN: bind to any port, send registration to radio's public UDP port
    bool bound = m_socket.bind(QHostAddress::AnyIPv4, 0);
    if (!bound) {
        qWarning() << "PanadapterStream: WAN — failed to bind UDP socket:"
                   << m_socket.errorString();
        return false;
    }

    m_localPort = m_socket.localPort();
    qDebug() << "PanadapterStream: WAN — bound to UDP port" << m_localPort;

    // Send registration to radio's public UDP port
    const QByteArray reg(1, '\x00');
    const qint64 sent = m_socket.writeDatagram(reg, radioAddr, radioUdpPort);
    if (sent == 1)
        qDebug() << "PanadapterStream: WAN — sent UDP registration to"
                 << radioAddr.toString() << ":" << radioUdpPort;
    else
        qWarning() << "PanadapterStream: WAN — UDP registration send failed:"
                   << m_socket.errorString();

    m_conn = nullptr;  // no RadioConnection in WAN mode
    return true;
}

void PanadapterStream::stop()
{
    m_socket.close();
    m_localPort = 0;
}

// ─── Datagram reception ───────────────────────────────────────────────────────

void PanadapterStream::setOwnedStreamIds(quint32 panStreamId, quint32 wfStreamId)
{
    m_ownedPanStreamId = panStreamId;
    m_ownedWfStreamId  = wfStreamId;
    qDebug() << "PanadapterStream: filtering for pan=0x" + QString::number(panStreamId, 16)
             << "wf=0x" + QString::number(wfStreamId, 16);
}

void PanadapterStream::setDbmRange(float minDbm, float maxDbm)
{
    m_minDbm = minDbm;
    m_maxDbm = maxDbm;
    qDebug() << "PanadapterStream: dBm range set to" << minDbm << "->" << maxDbm;
}

void PanadapterStream::onDatagramReady()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram dg = m_socket.receiveDatagram();
        if (!dg.isNull()) {
            m_totalRxBytes += dg.data().size();
            processDatagram(dg.data());
        }
    }
}

void PanadapterStream::processDatagram(const QByteArray& data)
{
    if (data.size() < VITA49_HEADER_BYTES) return;

    const auto* raw = reinterpret_cast<const uchar*>(data.constData());

    const quint32 word0    = qFromBigEndian<quint32>(raw);
    const quint32 streamId = qFromBigEndian<quint32>(raw + 4);
    const bool    hasTrailer = (word0 & 0x04000000u) != 0;

    // PacketClassCode is in the lower 16 bits of word 3 (bytes 12-15).
    const quint16 pcc = static_cast<quint16>(qFromBigEndian<quint32>(raw + 12) & 0xFFFFu);

    // Log the first occurrence of each unique stream ID.
    static QSet<quint32> seenIds;
    if (!seenIds.contains(streamId)) {
        seenIds.insert(streamId);
        qDebug() << "PanadapterStream: new stream" << data.size()
                 << "bytes, word0=0x" + QString::number(word0, 16)
                 << "streamId=0x" + QString::number(streamId, 16)
                 << "pcc=0x" + QString::number(pcc, 16)
                 << "trailer=" << hasTrailer;
    }

    // Track packet sequence for network quality monitoring.
    // VITA-49 packet count is a 4-bit field (bits 19:16 of word0).
    const int seq = (word0 >> 16) & 0x0F;
    auto& stats = m_streamStats[pcc];
    stats.totalCount++;
    if (stats.lastSeq >= 0) {
        const int expected = (stats.lastSeq + 1) & 0x0F;
        if (seq != expected)
            stats.errorCount++;
    }
    stats.lastSeq = seq;

    // Check if this stream ID is a DAX stream — route separately
    if (m_daxStreamIds.contains(streamId)) {
        int channel = m_daxStreamIds[streamId];
        QByteArray pcm;
        if (pcc == PCC_IF_NARROW) {
            const int payloadStart = VITA49_HEADER_BYTES;
            const int payloadBytes = data.size() - payloadStart - (hasTrailer ? 4 : 0);
            if (payloadBytes < 4) return;
            const int numFloats = payloadBytes / 4;
            const uchar* src = raw + payloadStart;
            pcm.resize(numFloats * 2);
            auto* dst = reinterpret_cast<qint16*>(pcm.data());
            for (int i = 0; i < numFloats; ++i) {
                const quint32 u = qFromBigEndian<quint32>(src + i * 4);
                float f;
                std::memcpy(&f, &u, 4);
                dst[i] = static_cast<qint16>(qBound(-1.0f, f, 1.0f) * 32767.0f);
            }
        } else if (pcc == PCC_IF_NARROW_REDUCED) {
            const int payloadStart = VITA49_HEADER_BYTES;
            const int payloadBytes = data.size() - payloadStart - (hasTrailer ? 4 : 0);
            if (payloadBytes < 2) return;
            const int monoSamples = payloadBytes / 2;
            const uchar* src = raw + payloadStart;
            pcm.resize(monoSamples * 4);
            auto* dst = reinterpret_cast<qint16*>(pcm.data());
            for (int i = 0; i < monoSamples; ++i) {
                const qint16 s = qFromBigEndian<qint16>(src + i * 2);
                dst[i * 2]     = s;
                dst[i * 2 + 1] = s;
            }
        } else {
            return;
        }
        emit daxAudioReady(channel, pcm);
        return;
    }

    // Route by PacketClassCode
    switch (pcc) {
    case PCC_IF_NARROW:
        decodeNarrowAudio(raw, data.size(), hasTrailer);
        return;
    case PCC_IF_NARROW_REDUCED:
        decodeReducedBwAudio(raw, data.size(), hasTrailer);
        return;
    case PCC_FFT:
        // Filter: only process FFT from our panadapter
        if (m_ownedPanStreamId != 0 && streamId != m_ownedPanStreamId)
            return;
        decodeFFT(raw, data.size(), hasTrailer);
        return;
    case PCC_WATERFALL:
        // Filter: only process waterfall from our display
        if (m_ownedWfStreamId != 0 && streamId != m_ownedWfStreamId)
            return;
        decodeWaterfallTile(raw, data.size(), hasTrailer);
        return;
    case PCC_METER:
        decodeMeterData(raw, data.size(), hasTrailer);
        return;
    default:
        break;
    }
}

// ─── FFT decode ──────────────────────────────────────────────────────────────

void PanadapterStream::decodeFFT(const uchar* raw, int totalBytes, bool hasTrailer)
{
    // FFT sub-header (bytes 28–39):
    //   uint16 start_bin_index
    //   uint16 num_bins
    //   uint16 bin_size          (bytes per bin, always 2)
    //   uint16 total_bins_in_frame
    //   uint32 frame_index
    static constexpr int FFT_SUBHEADER_BYTES = 12;
    if (totalBytes < VITA49_HEADER_BYTES + FFT_SUBHEADER_BYTES) return;

    const uchar* sub = raw + VITA49_HEADER_BYTES;
    const quint16 startBin   = qFromBigEndian<quint16>(sub + 0);
    const quint16 numBins    = qFromBigEndian<quint16>(sub + 2);
    const quint16 binSize    = qFromBigEndian<quint16>(sub + 4);
    const quint16 totalBins  = qFromBigEndian<quint16>(sub + 6);
    const quint32 frameIndex = qFromBigEndian<quint32>(sub + 8);

    if (numBins == 0 || binSize == 0 || totalBins == 0) return;

    const int binDataOffset = VITA49_HEADER_BYTES + FFT_SUBHEADER_BYTES;
    int binDataBytes = numBins * binSize;
    const int available = totalBytes - binDataOffset - (hasTrailer ? 4 : 0);
    if (available < binDataBytes) {
        binDataBytes = available;
        if (binDataBytes <= 0) return;
    }

    const uchar* binData = raw + binDataOffset;

    if (frameIndex != m_frame.frameIndex)
        m_frame.reset(frameIndex, totalBins);

    if (startBin + numBins > static_cast<quint16>(m_frame.buf.size()))
        return;

    for (quint16 i = 0; i < numBins; ++i)
        m_frame.buf[startBin + i] = qFromBigEndian<quint16>(binData + i * 2);

    m_frame.binsReceived += numBins;

    if (!m_frame.isComplete()) return;

    // Convert to dBm and emit
    const float range = m_maxDbm - m_minDbm;
    const int   count = m_frame.buf.size();
    QVector<float> bins(count);

    // The radio sends pre-scaled Y pixel coordinates:
    //   raw 0 = max_dbm (strongest), raw (ypixels-1) = min_dbm (weakest).
    // We configured ypixels=700, so the Y range is 0–699.
    constexpr float kYPixels = 700.0f;
    for (int i = 0; i < count; ++i)
        bins[i] = m_maxDbm - (static_cast<float>(m_frame.buf[i]) / (kYPixels - 1.0f)) * range;

    emit spectrumReady(bins);
}

// ─── Waterfall tile decode ───────────────────────────────────────────────────
//
// Tile sub-header (36 bytes, big-endian, at byte 28):
//   int64  FrameLowFreq      (Hz × 1e6 — i.e. VitaFrequency)
//   int64  BinBandwidth      (Hz × 1e6)
//   uint32 LineDurationMS
//   uint16 Width             (bins per row)
//   uint16 Height            (rows in this tile)
//   uint32 Timecode          (frame index for reassembly)
//   uint32 AutoBlackLevel
//   uint16 TotalBinsInFrame  (total bins if fragmented across packets)
//   uint16 FirstBinIndex
//
// Payload: Width × Height uint16 values (big-endian).
// Conversion: treat as signed int16, divide by 128.  Typical noise floor
// ~96-106, signal peaks ~110-115.  Colour-mapped in SpectrumWidget.

void PanadapterStream::decodeWaterfallTile(const uchar* raw, int totalBytes, bool hasTrailer)
{
    static constexpr int TILE_SUBHEADER_BYTES = 36;
    if (totalBytes < VITA49_HEADER_BYTES + TILE_SUBHEADER_BYTES) return;

    const uchar* sub = raw + VITA49_HEADER_BYTES;

    // Extract frequency range from tile sub-header.
    // FrameLowFreq and BinBandwidth are int64 "VitaFrequency" (Hz × 2^20).
    const qint64 frameLowRaw  = qFromBigEndian<qint64>(sub + 0);
    const qint64 binBwRaw     = qFromBigEndian<qint64>(sub + 8);
    const quint16 tileWidth       = qFromBigEndian<quint16>(sub + 20);
    const quint16 tileHeight      = qFromBigEndian<quint16>(sub + 22);
    const quint32 timecode        = qFromBigEndian<quint32>(sub + 24);
    const quint32 autoBlack       = qFromBigEndian<quint32>(sub + 28);
    const quint16 totalBinsInFrame = qFromBigEndian<quint16>(sub + 32);
    const quint16 firstBinIndex   = qFromBigEndian<quint16>(sub + 34);

    if (tileWidth == 0 || tileHeight == 0) return;

    // FrameLowFreq and BinBandwidth may be VitaFrequency (Hz × 2^20) or plain Hz.
    // Try VitaFrequency first; if the result is unreasonable, try plain Hz.
    double lowFreqMhz  = static_cast<double>(frameLowRaw) / (1048576.0 * 1e6);
    double binBwMhz    = static_cast<double>(binBwRaw) / (1048576.0 * 1e6);
    if (lowFreqMhz < 0.001 || lowFreqMhz > 1000.0) {
        // Fallback: treat as plain Hz
        lowFreqMhz = static_cast<double>(frameLowRaw) / 1e6;
        binBwMhz   = static_cast<double>(binBwRaw) / 1e6;
    }
    const double highFreqMhz = lowFreqMhz + binBwMhz * tileWidth;

    const int payloadOffset = VITA49_HEADER_BYTES + TILE_SUBHEADER_BYTES;
    const int payloadBytes  = totalBytes - payloadOffset - (hasTrailer ? 4 : 0);
    if (payloadBytes < tileWidth * 2) return;  // need at least one row of bins

    static bool loggedOnce = false;
    if (!loggedOnce) {
        qDebug() << "WaterfallTile: width=" << tileWidth << "height=" << tileHeight
                 << "totalBinsInFrame=" << totalBinsInFrame
                 << "firstBinIndex=" << firstBinIndex
                 << "timecode=" << timecode
                 << "lowFreqMhz=" << lowFreqMhz
                 << "binBwMhz=" << binBwMhz
                 << "highFreqMhz=" << highFreqMhz
                 << "fullFrameMhz=" << (lowFreqMhz + binBwMhz * totalBinsInFrame)
                 << "autoBlack=" << autoBlack;
        loggedOnce = true;
    }

    // ── Waterfall frame assembly ─────────────────────────────────────────
    // Start a new frame if timecode changed
    if (timecode != m_wfFrame.timecode)
        m_wfFrame.reset(timecode, totalBinsInFrame, lowFreqMhz, binBwMhz, autoBlack);

    // Copy this fragment's bins into the assembly buffer.
    // Only process the first row (height is typically 1).
    const uchar* tilePayload = raw + payloadOffset;
    const int binsToRead = qMin(static_cast<int>(tileWidth),
                                static_cast<int>(totalBinsInFrame) - static_cast<int>(firstBinIndex));
    if (binsToRead <= 0) return;

    for (int i = 0; i < binsToRead; ++i) {
        const auto raw16 = static_cast<qint16>(qFromBigEndian<quint16>(tilePayload + i * 2));
        m_wfFrame.buf[firstBinIndex + i] = static_cast<float>(raw16) / 128.0f;
    }
    m_wfFrame.binsReceived += binsToRead;

    // Only emit when the full frame is assembled
    if (!m_wfFrame.isComplete()) return;

    const double frameHighMhz = m_wfFrame.lowFreqMhz + m_wfFrame.binBwMhz * m_wfFrame.totalBins;
    emit waterfallAutoBlackLevel(m_wfFrame.autoBlack);
    emit waterfallRowReady(m_wfFrame.buf, m_wfFrame.lowFreqMhz, frameHighMhz);
}

// ─── Audio decode ─────────────────────────────────────────────────────────────

void PanadapterStream::decodeNarrowAudio(const uchar* raw, int totalBytes, bool hasTrailer)
{
    // One-time: log the RX audio VITA-49 header for comparison with our TX packets
    static bool rxHeaderLogged = false;
    if (!rxHeaderLogged && totalBytes >= 28) {
        rxHeaderLogged = true;
        const quint32* w = reinterpret_cast<const quint32*>(raw);
        qDebug() << "VITA-49 RX audio header (host-order):"
                 << QString("w0=%1 w1=%2 w2=%3 w3=%4 w4=%5 w5=%6 w6=%7")
                    .arg(qFromBigEndian(w[0]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[1]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[2]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[3]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[4]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[5]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[6]), 8, 16, QChar('0'))
                 << "totalBytes=" << totalBytes;
    }

    // Payload: big-endian float32 stereo interleaved (L, R, L, R, ...).
    // Convert to little-endian int16 stereo for QAudioSink (Int16, 24 kHz).
    const int payloadStart = VITA49_HEADER_BYTES;
    const int payloadBytes = totalBytes - payloadStart - (hasTrailer ? 4 : 0);
    if (payloadBytes < 4) return;

    const int numFloats = payloadBytes / 4;
    const uchar* src = raw + payloadStart;

    QByteArray pcm(numFloats * 2, Qt::Uninitialized);
    auto* dst = reinterpret_cast<qint16*>(pcm.data());

    for (int i = 0; i < numFloats; ++i) {
        // Read big-endian uint32, reinterpret as float (byte-swap = big→native float).
        const quint32 u = qFromBigEndian<quint32>(src + i * 4);
        float f;
        std::memcpy(&f, &u, 4);
        dst[i] = static_cast<qint16>(qBound(-1.0f, f, 1.0f) * 32767.0f);
    }

    emit audioDataReady(pcm);
}

void PanadapterStream::decodeReducedBwAudio(const uchar* raw, int totalBytes, bool hasTrailer)
{
    // One-time: log the reduced-BW RX audio VITA-49 header
    static bool rxReducedLogged = false;
    if (!rxReducedLogged && totalBytes >= 28) {
        rxReducedLogged = true;
        const quint32* w = reinterpret_cast<const quint32*>(raw);
        qDebug() << "VITA-49 RX reduced-BW audio header (host-order):"
                 << QString("w0=%1 w1=%2 w2=%3 w3=%4 w4=%5 w5=%6 w6=%7")
                    .arg(qFromBigEndian(w[0]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[1]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[2]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[3]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[4]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[5]), 8, 16, QChar('0'))
                    .arg(qFromBigEndian(w[6]), 8, 16, QChar('0'))
                 << "totalBytes=" << totalBytes;
    }

    // Payload: big-endian int16 mono. Duplicate to stereo for QAudioSink.
    const int payloadStart = VITA49_HEADER_BYTES;
    const int payloadBytes = totalBytes - payloadStart - (hasTrailer ? 4 : 0);
    if (payloadBytes < 2) return;

    const int monoSamples = payloadBytes / 2;
    const uchar* src = raw + payloadStart;

    QByteArray pcm(monoSamples * 4, Qt::Uninitialized);  // stereo int16
    auto* dst = reinterpret_cast<qint16*>(pcm.data());

    for (int i = 0; i < monoSamples; ++i) {
        const qint16 s = qFromBigEndian<qint16>(src + i * 2);
        dst[i * 2]     = s;  // L
        dst[i * 2 + 1] = s;  // R
    }

    emit audioDataReady(pcm);
}

// ─── Meter data decode ───────────────────────────────────────────────────────
//
// VITA-49 meter packet (PCC 0x8002): payload is N × 4-byte pairs:
//   uint16 meter_id  (big-endian)
//   int16  raw_value (big-endian)
//
// Raw values are converted by MeterModel based on the meter's unit type.
// Reference: FlexLib VitaMeterPacket.cs

void PanadapterStream::decodeMeterData(const uchar* raw, int totalBytes, bool hasTrailer)
{
    const int payloadStart = VITA49_HEADER_BYTES;
    const int payloadBytes = totalBytes - payloadStart - (hasTrailer ? 4 : 0);
    if (payloadBytes < 4) return;

    const int numMeters = payloadBytes / 4;
    const uchar* payload = raw + payloadStart;

    QVector<quint16> ids(numMeters);
    QVector<qint16>  vals(numMeters);

    for (int i = 0; i < numMeters; ++i) {
        ids[i]  = qFromBigEndian<quint16>(payload + i * 4);
        vals[i] = qFromBigEndian<qint16>(payload + i * 4 + 2);
    }

    emit meterDataReady(ids, vals);
}

int PanadapterStream::packetErrorCount() const
{
    int total = 0;
    for (auto it = m_streamStats.constBegin(); it != m_streamStats.constEnd(); ++it)
        total += it->errorCount;
    return total;
}

int PanadapterStream::packetTotalCount() const
{
    int total = 0;
    for (auto it = m_streamStats.constBegin(); it != m_streamStats.constEnd(); ++it)
        total += it->totalCount;
    return total;
}

void PanadapterStream::registerDaxStream(quint32 streamId, int channel)
{
    m_daxStreamIds[streamId] = channel;
    qDebug() << "PanadapterStream: registered DAX stream" << Qt::hex << streamId << "-> channel" << channel;
}

void PanadapterStream::unregisterDaxStream(quint32 streamId)
{
    m_daxStreamIds.remove(streamId);
    qDebug() << "PanadapterStream: unregistered DAX stream" << Qt::hex << streamId;
}

void PanadapterStream::sendToRadio(const QByteArray& packet)
{
    if (m_radioAddress.isNull() || m_radioPort == 0) {
        static int dropCount = 0;
        if (++dropCount <= 5)
            qWarning() << "PanadapterStream::sendToRadio: no dest! addr="
                       << m_radioAddress.toString() << "port=" << m_radioPort;
        return;
    }
    const qint64 sent = m_socket.writeDatagram(packet, m_radioAddress, m_radioPort);
    static int txCount = 0;
    ++txCount;
    if (txCount <= 5 || txCount % 1000 == 0) {
        qDebug() << "PanadapterStream::sendToRadio #" << txCount
                 << "bytes=" << packet.size()
                 << "sent=" << sent
                 << "to=" << m_radioAddress.toString() << ":" << m_radioPort
                 << "localPort=" << m_socket.localPort();
    }
    if (sent < 0) {
        static int errCount = 0;
        if (++errCount <= 10)
            qWarning() << "PanadapterStream::sendToRadio ERROR:" << m_socket.errorString();
    }
}

} // namespace AetherSDR
