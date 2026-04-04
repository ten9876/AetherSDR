#pragma once
#ifdef HAVE_HIDAPI

#include <cstdint>
#include <cstddef>
#include <memory>
#include <vector>
#include <QByteArray>
#include <QString>
#include <hidapi/hidapi.h>

namespace AetherSDR {

// ── Device descriptor ──────────────────────────────────────────────────────

struct StreamDeckDeviceInfo {
    uint16_t    pid;
    const char* name;
    int         keyCount;
    int         keyCols;
    int         keyRows;
    int         keyWidth;       // pixel width per key (0 for Pedal)
    int         keyHeight;
    int         dialCount;
    int         touchWidth;     // touchscreen width (0 if none)
    int         touchHeight;
    bool        useJpeg;        // false = BMP (Gen1)
    bool        flipH;
    bool        flipV;
    int         rotation;       // 0 or 90
    enum Family { Gen1, Gen2, Plus } family;
    int         inputReportLen; // HID input report size
    int         featureReportLen; // 17 (Gen1) or 32 (Gen2/Plus)
};

// ── Input events ───────────────────────────────────────────────────────────

struct SDKeyEvent    { int key; bool pressed; };
struct SDDialEvent   { int dial; int delta; };    // turn: signed steps
struct SDDialPush    { int dial; bool pressed; };
struct SDTouchEvent  { enum Type { Short, Long, Drag } type; int x, y, xOut, yOut; };

struct SDInputEvents {
    std::vector<SDKeyEvent>   keys;
    std::vector<SDDialEvent>  dials;
    std::vector<SDDialPush>   dialPushes;
    std::vector<SDTouchEvent> touches;
};

// ── Protocol interface ─────────────────────────────────────────────────────

class StreamDeckProtocol {
public:
    virtual ~StreamDeckProtocol() = default;

    virtual QString serialNumber(hid_device* dev) const = 0;
    virtual QString firmwareVersion(hid_device* dev) const = 0;
    virtual void reset(hid_device* dev) const = 0;
    virtual void setBrightness(hid_device* dev, int percent) const = 0;
    virtual void writeKeyImage(hid_device* dev, int key, const QByteArray& imageData,
                               const StreamDeckDeviceInfo& info) const = 0;
    virtual void writeTouchscreenImage(hid_device* dev, const QByteArray& imageData,
                                       int x, int y, int w, int h,
                                       const StreamDeckDeviceInfo& info) const = 0;
    virtual SDInputEvents parseInput(const uint8_t* buf, int len,
                                     const StreamDeckDeviceInfo& info) const = 0;
};

// ── Factory / table ────────────────────────────────────────────────────────

static constexpr uint16_t STREAMDECK_VID = 0x0FD9;

const StreamDeckDeviceInfo* streamDeckDeviceTable();
int streamDeckDeviceCount();
const StreamDeckDeviceInfo* findStreamDeckDevice(uint16_t pid);
std::unique_ptr<StreamDeckProtocol> createProtocol(StreamDeckDeviceInfo::Family family);

} // namespace AetherSDR
#endif
