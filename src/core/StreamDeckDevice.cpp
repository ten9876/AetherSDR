#ifdef HAVE_HIDAPI
#include "StreamDeckDevice.h"
#include <cstring>
#include <algorithm>
#include <QImage>
#include <QBuffer>
#include <QTransform>

namespace AetherSDR {

// ── Device table ───────────────────────────────────────────────────────────

static const StreamDeckDeviceInfo kDevices[] = {
    // Gen1
    {0x0063, "Stream Deck Mini",     6,  3, 2, 80, 80,  0, 0, 0,   false, false, true,  90, StreamDeckDeviceInfo::Gen1, 17, 17},
    {0x0090, "Stream Deck Mini MK2", 6,  3, 2, 80, 80,  0, 0, 0,   false, false, true,  90, StreamDeckDeviceInfo::Gen1, 17, 32},
    {0x0060, "Stream Deck Original", 15, 5, 3, 72, 72,  0, 0, 0,   false, true,  true,  0,  StreamDeckDeviceInfo::Gen1, 17, 17},
    // Gen2
    {0x006d, "Stream Deck Orig V2",  15, 5, 3, 72, 72,  0, 0, 0,   true,  true,  true,  0,  StreamDeckDeviceInfo::Gen2, 36, 32},
    {0x0080, "Stream Deck MK2",      15, 5, 3, 72, 72,  0, 0, 0,   true,  true,  true,  0,  StreamDeckDeviceInfo::Gen2, 36, 32},
    {0x00a5, "Stream Deck MK2 Sci",  15, 5, 3, 72, 72,  0, 0, 0,   true,  true,  true,  0,  StreamDeckDeviceInfo::Gen2, 36, 32},
    {0x006c, "Stream Deck XL",       32, 8, 4, 96, 96,  0, 0, 0,   true,  true,  true,  0,  StreamDeckDeviceInfo::Gen2, 36, 32},
    {0x008f, "Stream Deck XL V2",    32, 8, 4, 96, 96,  0, 0, 0,   true,  true,  true,  0,  StreamDeckDeviceInfo::Gen2, 36, 32},
    {0x009a, "Stream Deck Neo",      8,  4, 2, 72, 72,  0, 0, 0,   true,  true,  true,  0,  StreamDeckDeviceInfo::Gen2, 36, 32},
    {0x0086, "Stream Deck Pedal",    3,  3, 1, 0,  0,   0, 0, 0,   false, false, false, 0,  StreamDeckDeviceInfo::Gen2, 36, 32},
    // Plus
    {0x0084, "Stream Deck +",        8,  4, 2, 120,120, 4, 800,100, true,  false, false, 0,  StreamDeckDeviceInfo::Plus, 14, 32},
    {0x00c6, "Stream Deck + XL",     36, 9, 4, 112,112, 6, 1200,100,true,  false, false, 90, StreamDeckDeviceInfo::Plus, 64, 32},
};

const StreamDeckDeviceInfo* streamDeckDeviceTable() { return kDevices; }
int streamDeckDeviceCount() { return static_cast<int>(std::size(kDevices)); }

const StreamDeckDeviceInfo* findStreamDeckDevice(uint16_t pid)
{
    for (const auto& d : kDevices)
        if (d.pid == pid) return &d;
    return nullptr;
}

// ── Helper: extract string from feature report ─────────────────────────────

static QString extractString(const uint8_t* buf, int offset, int len)
{
    QByteArray raw(reinterpret_cast<const char*>(buf + offset), len - offset);
    // Trim null bytes
    int end = raw.indexOf('\0');
    if (end >= 0) raw.truncate(end);
    return QString::fromUtf8(raw).trimmed();
}

// ── Gen2 Protocol ──────────────────────────────────────────────────────────
// Covers: Original V2, MK2, XL, XL V2, Neo, Pedal

class Gen2Protocol : public StreamDeckProtocol {
public:
    QString serialNumber(hid_device* dev) const override {
        uint8_t buf[32]{};
        buf[0] = 0x06;
        if (hid_get_feature_report(dev, buf, 32) < 0) return {};
        return extractString(buf, 2, 32);
    }

    QString firmwareVersion(hid_device* dev) const override {
        uint8_t buf[32]{};
        buf[0] = 0x05;
        if (hid_get_feature_report(dev, buf, 32) < 0) return {};
        return extractString(buf, 6, 32);
    }

    void reset(hid_device* dev) const override {
        uint8_t buf[32]{};
        buf[0] = 0x03; buf[1] = 0x02;
        hid_send_feature_report(dev, buf, 32);
    }

    void setBrightness(hid_device* dev, int percent) const override {
        uint8_t buf[32]{};
        buf[0] = 0x03; buf[1] = 0x08;
        buf[2] = static_cast<uint8_t>(std::clamp(percent, 0, 100));
        hid_send_feature_report(dev, buf, 32);
    }

    void writeKeyImage(hid_device* dev, int key, const QByteArray& imageData,
                       const StreamDeckDeviceInfo&) const override {
        int pageNumber = 0;
        int bytesRemaining = imageData.size();
        const int payloadLen = 1024 - 8;

        while (bytesRemaining > 0) {
            int thisLen = std::min(bytesRemaining, payloadLen);
            int bytesSent = pageNumber * payloadLen;

            uint8_t packet[1024]{};
            packet[0] = 0x02;
            packet[1] = 0x07;
            packet[2] = static_cast<uint8_t>(key);
            packet[3] = (thisLen == bytesRemaining) ? 1 : 0;
            packet[4] = thisLen & 0xFF;
            packet[5] = (thisLen >> 8) & 0xFF;
            packet[6] = pageNumber & 0xFF;
            packet[7] = (pageNumber >> 8) & 0xFF;

            std::memcpy(packet + 8, imageData.constData() + bytesSent, thisLen);
            hid_write(dev, packet, 1024);

            bytesRemaining -= thisLen;
            pageNumber++;
        }
    }

    void writeTouchscreenImage(hid_device*, const QByteArray&,
                                int, int, int, int,
                                const StreamDeckDeviceInfo&) const override {
        // Gen2 devices don't have touchscreens (except Neo info strip — todo)
    }

    SDInputEvents parseInput(const uint8_t* buf, int len,
                             const StreamDeckDeviceInfo& info) const override {
        SDInputEvents events;
        if (len < 4 + info.keyCount) return events;
        // Gen2: keys start at byte 4
        for (int i = 0; i < info.keyCount; ++i)
            events.keys.push_back({i, buf[4 + i] != 0});
        return events;
    }
};

// ── Plus Protocol ──────────────────────────────────────────────────────────
// Covers: Stream Deck +, Stream Deck + XL

class PlusProtocol : public StreamDeckProtocol {
public:
    QString serialNumber(hid_device* dev) const override {
        uint8_t buf[32]{};
        buf[0] = 0x06;
        if (hid_get_feature_report(dev, buf, 32) < 0) return {};
        // Plus XL uses offset 2, Plus uses offset 5
        // Try offset 2 first — if it starts with printable char, use it
        QString s2 = extractString(buf, 2, 32);
        if (!s2.isEmpty() && s2[0].isPrint()) return s2;
        return extractString(buf, 5, 32);
    }

    QString firmwareVersion(hid_device* dev) const override {
        uint8_t buf[32]{};
        buf[0] = 0x05;
        if (hid_get_feature_report(dev, buf, 32) < 0) return {};
        QString v6 = extractString(buf, 6, 32);
        if (!v6.isEmpty() && v6[0].isDigit()) return v6;
        return extractString(buf, 5, 32);
    }

    void reset(hid_device* dev) const override {
        uint8_t buf[32]{};
        buf[0] = 0x03; buf[1] = 0x02;
        hid_send_feature_report(dev, buf, 32);
    }

    void setBrightness(hid_device* dev, int percent) const override {
        uint8_t buf[32]{};
        buf[0] = 0x03; buf[1] = 0x08;
        buf[2] = static_cast<uint8_t>(std::clamp(percent, 0, 100));
        hid_send_feature_report(dev, buf, 32);
    }

    void writeKeyImage(hid_device* dev, int key, const QByteArray& imageData,
                       const StreamDeckDeviceInfo&) const override {
        // Same 8-byte header as Gen2
        int pageNumber = 0;
        int bytesRemaining = imageData.size();
        const int payloadLen = 1024 - 8;

        while (bytesRemaining > 0) {
            int thisLen = std::min(bytesRemaining, payloadLen);
            int bytesSent = pageNumber * payloadLen;

            uint8_t packet[1024]{};
            packet[0] = 0x02;
            packet[1] = 0x07;
            packet[2] = static_cast<uint8_t>(key);
            packet[3] = (thisLen == bytesRemaining) ? 1 : 0;
            packet[4] = thisLen & 0xFF;
            packet[5] = (thisLen >> 8) & 0xFF;
            packet[6] = pageNumber & 0xFF;
            packet[7] = (pageNumber >> 8) & 0xFF;

            std::memcpy(packet + 8, imageData.constData() + bytesSent, thisLen);
            hid_write(dev, packet, 1024);

            bytesRemaining -= thisLen;
            pageNumber++;
        }
    }

    void writeTouchscreenImage(hid_device* dev, const QByteArray& imageData,
                                int x, int y, int w, int h,
                                const StreamDeckDeviceInfo& info) const override {
        QByteArray sendData = imageData;

        // Plus XL: internal LCD is portrait — rotate image and swap coordinates
        int intX = x, intY = y, intW = w, intH = h;
        if (info.rotation == 90) {
            // Decode JPEG, rotate 90 CCW, re-encode
            QImage img;
            img.loadFromData(imageData, "JPEG");
            if (!img.isNull()) {
                QTransform t;
                t.rotate(-90);  // CCW 90 to match internal portrait orientation
                QImage rotated = img.transformed(t);
                QBuffer buf(&sendData);
                buf.open(QIODevice::WriteOnly);
                sendData.clear();
                rotated.save(&buf, "JPEG", 80);
            }
            intX = y; intY = x; intW = h; intH = w;
        }

        int pageNumber = 0;
        int bytesRemaining = sendData.size();
        const int payloadLen = 1024 - 16;

        while (bytesRemaining > 0) {
            int thisLen = std::min(bytesRemaining, payloadLen);
            int bytesSent = pageNumber * payloadLen;

            uint8_t packet[1024]{};
            packet[0]  = 0x02;
            packet[1]  = 0x0c;
            packet[2]  = intX & 0xFF;
            packet[3]  = (intX >> 8) & 0xFF;
            packet[4]  = intY & 0xFF;
            packet[5]  = (intY >> 8) & 0xFF;
            packet[6]  = intW & 0xFF;
            packet[7]  = (intW >> 8) & 0xFF;
            packet[8]  = intH & 0xFF;
            packet[9]  = (intH >> 8) & 0xFF;
            packet[10] = (thisLen == bytesRemaining) ? 1 : 0;
            packet[11] = pageNumber & 0xFF;
            packet[12] = (pageNumber >> 8) & 0xFF;
            packet[13] = thisLen & 0xFF;
            packet[14] = (thisLen >> 8) & 0xFF;
            packet[15] = 0x00;

            std::memcpy(packet + 16, sendData.constData() + bytesSent, thisLen);
            hid_write(dev, packet, 1024);

            bytesRemaining -= thisLen;
            pageNumber++;
        }
    }

    SDInputEvents parseInput(const uint8_t* buf, int len,
                             const StreamDeckDeviceInfo& info) const override {
        SDInputEvents events;
        if (len < 5) return events;

        uint8_t eventType = buf[1];

        if (eventType == 0x00) {
            // Key event: keys at bytes 4..4+keyCount
            int offset = 4;
            for (int i = 0; i < info.keyCount && offset + i < len; ++i)
                events.keys.push_back({i, buf[offset + i] != 0});
        }
        else if (eventType == 0x03) {
            // Dial event: byte 4 = subtype (0=push, 1=turn)
            uint8_t subtype = buf[4];
            int offset = 5;
            for (int i = 0; i < info.dialCount && offset + i < len; ++i) {
                if (subtype == 0x00) {
                    // Push
                    events.dialPushes.push_back({i, buf[offset + i] != 0});
                } else if (subtype == 0x01) {
                    // Turn: two's complement signed byte
                    auto raw = static_cast<int8_t>(buf[offset + i]);
                    if (raw != 0)
                        events.dials.push_back({i, static_cast<int>(raw)});
                }
            }
        }
        else if (eventType == 0x02) {
            // Touchscreen event
            if (len < 14) return events;
            SDTouchEvent te{};
            uint8_t touchType = buf[4];
            if (touchType == 1) te.type = SDTouchEvent::Short;
            else if (touchType == 2) te.type = SDTouchEvent::Long;
            else if (touchType == 3) te.type = SDTouchEvent::Drag;
            else return events;

            te.x = buf[6] | (buf[7] << 8);
            te.y = buf[8] | (buf[9] << 8);
            if (te.type == SDTouchEvent::Drag && len >= 14) {
                te.xOut = buf[10] | (buf[11] << 8);
                te.yOut = buf[12] | (buf[13] << 8);
            }
            events.touches.push_back(te);
        }

        return events;
    }
};

// ── Gen1 Protocol (stub — Mini/Original BMP devices) ───────────────────────

class Gen1Protocol : public StreamDeckProtocol {
public:
    QString serialNumber(hid_device* dev) const override {
        uint8_t buf[32]{};
        buf[0] = 0x03;
        if (hid_get_feature_report(dev, buf, 17) < 0) return {};
        return extractString(buf, 5, 17);
    }

    QString firmwareVersion(hid_device* dev) const override {
        uint8_t buf[32]{};
        buf[0] = 0x04;
        if (hid_get_feature_report(dev, buf, 17) < 0) return {};
        return extractString(buf, 5, 17);
    }

    void reset(hid_device* dev) const override {
        uint8_t buf[17]{};
        buf[0] = 0x0B; buf[1] = 0x63;
        hid_send_feature_report(dev, buf, 17);
    }

    void setBrightness(hid_device* dev, int percent) const override {
        uint8_t buf[17]{};
        buf[0] = 0x05; buf[1] = 0x55; buf[2] = 0xAA;
        buf[3] = 0xD1; buf[4] = 0x01;
        buf[5] = static_cast<uint8_t>(std::clamp(percent, 0, 100));
        hid_send_feature_report(dev, buf, 17);
    }

    void writeKeyImage(hid_device* dev, int key, const QByteArray& imageData,
                       const StreamDeckDeviceInfo& info) const override {
        // Gen1 Mini: 1024-byte packets with 16-byte header
        int pageNumber = 0;
        int bytesRemaining = imageData.size();
        const int payloadLen = 1024 - 16;

        // Original uses mirrored key indices per row
        int writeKey = key;
        if (info.keyCols == 5) {
            int row = key / 5;
            int col = key % 5;
            writeKey = row * 5 + (4 - col);
        }

        while (bytesRemaining > 0) {
            int thisLen = std::min(bytesRemaining, payloadLen);
            int bytesSent = pageNumber * payloadLen;

            uint8_t packet[1024]{};
            packet[0] = 0x02;
            packet[1] = 0x01;
            packet[2] = static_cast<uint8_t>(pageNumber);
            packet[3] = 0;
            packet[4] = (thisLen == bytesRemaining) ? 1 : 0;
            packet[5] = static_cast<uint8_t>(writeKey + 1);

            std::memcpy(packet + 16, imageData.constData() + bytesSent, thisLen);
            hid_write(dev, packet, 1024);

            bytesRemaining -= thisLen;
            pageNumber++;
        }
    }

    void writeTouchscreenImage(hid_device*, const QByteArray&,
                                int, int, int, int,
                                const StreamDeckDeviceInfo&) const override {
        // Gen1 has no touchscreen
    }

    SDInputEvents parseInput(const uint8_t* buf, int len,
                             const StreamDeckDeviceInfo& info) const override {
        SDInputEvents events;
        if (len < 1 + info.keyCount) return events;
        // Gen1: keys start at byte 1
        for (int i = 0; i < info.keyCount; ++i)
            events.keys.push_back({i, buf[1 + i] != 0});
        return events;
    }
};

// ── Factory ────────────────────────────────────────────────────────────────

std::unique_ptr<StreamDeckProtocol> createProtocol(StreamDeckDeviceInfo::Family family)
{
    switch (family) {
    case StreamDeckDeviceInfo::Gen1: return std::make_unique<Gen1Protocol>();
    case StreamDeckDeviceInfo::Gen2: return std::make_unique<Gen2Protocol>();
    case StreamDeckDeviceInfo::Plus: return std::make_unique<PlusProtocol>();
    }
    return nullptr;
}

} // namespace AetherSDR
#endif
