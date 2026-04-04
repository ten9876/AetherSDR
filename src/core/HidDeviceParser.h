#pragma once
#ifdef HAVE_HIDAPI

#include <cstdint>
#include <cstddef>
#include <memory>

namespace AetherSDR {

struct HidEvent {
    enum Type { None, Rotate, Button };
    Type type{None};
    int  steps{0};       // for Rotate: +CW, -CCW
    int  button{0};      // 1-based button number
    int  action{0};      // 0=press, 1=release
};

struct HidDeviceId {
    uint16_t vid;
    uint16_t pid;
    const char* name;
};

class HidDeviceParser {
public:
    virtual ~HidDeviceParser() = default;
    virtual HidEvent parse(const uint8_t* buf, size_t len) = 0;
    virtual size_t reportSize() const = 0;

    static std::unique_ptr<HidDeviceParser> create(uint16_t vid, uint16_t pid);
    static const HidDeviceId* supportedDevices();
    static int supportedDeviceCount();
};

// Icom RC-28 (VID 0x0C26, PID 0x001E)
class IcomRC28Parser : public HidDeviceParser {
public:
    HidEvent parse(const uint8_t* buf, size_t len) override;
    size_t reportSize() const override { return 64; }
private:
    uint8_t m_prevEncoder{0};
    uint8_t m_prevButtons{0};
    bool m_firstReport{true};
};

// Griffin PowerMate (VID 0x077D, PID 0x0410)
class GriffinPowerMateParser : public HidDeviceParser {
public:
    HidEvent parse(const uint8_t* buf, size_t len) override;
    size_t reportSize() const override { return 6; }
private:
    uint8_t m_prevButton{0};
};

// Contour ShuttleXpress (VID 0x0B33, PID 0x0020)
class ShuttleXpressParser : public HidDeviceParser {
public:
    HidEvent parse(const uint8_t* buf, size_t len) override;
    size_t reportSize() const override { return 5; }
private:
    uint8_t m_prevJog{0};
    uint8_t m_prevButtons{0};
    bool m_firstReport{true};
};

// Contour ShuttlePro v2 (VID 0x0B33, PID 0x0030)
class ShuttleProV2Parser : public HidDeviceParser {
public:
    HidEvent parse(const uint8_t* buf, size_t len) override;
    size_t reportSize() const override { return 5; }
private:
    uint8_t m_prevJog{0};
    uint16_t m_prevButtons{0};
    bool m_firstReport{true};
};

} // namespace AetherSDR
#endif
