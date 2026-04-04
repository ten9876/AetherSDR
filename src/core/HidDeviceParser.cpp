#ifdef HAVE_HIDAPI
#include "HidDeviceParser.h"

namespace AetherSDR {

static const HidDeviceId kSupportedDevices[] = {
    {0x0C26, 0x001E, "Icom RC-28"},
    {0x077D, 0x0410, "Griffin PowerMate"},
    {0x0B33, 0x0020, "Contour ShuttleXpress"},
    {0x0B33, 0x0030, "Contour ShuttlePro v2"},
};

const HidDeviceId* HidDeviceParser::supportedDevices() { return kSupportedDevices; }
int HidDeviceParser::supportedDeviceCount() { return static_cast<int>(std::size(kSupportedDevices)); }

std::unique_ptr<HidDeviceParser> HidDeviceParser::create(uint16_t vid, uint16_t pid)
{
    if (vid == 0x0C26 && pid == 0x001E) return std::make_unique<IcomRC28Parser>();
    if (vid == 0x077D && pid == 0x0410) return std::make_unique<GriffinPowerMateParser>();
    if (vid == 0x0B33 && pid == 0x0020) return std::make_unique<ShuttleXpressParser>();
    if (vid == 0x0B33 && pid == 0x0030) return std::make_unique<ShuttleProV2Parser>();
    return nullptr;
}

// ── Icom RC-28 ──────────────────────────────────────────────────────────────
// 64-byte HID reports. Byte 0 = encoder position (0-255 wrapping).
// Byte 1 = button bits (bit 0 = encoder push, bit 1 = secondary).

HidEvent IcomRC28Parser::parse(const uint8_t* buf, size_t len)
{
    if (len < 2) return {};

    uint8_t enc = buf[0];
    uint8_t btns = buf[1];

    // Check buttons first (priority over encoder)
    if (btns != m_prevButtons) {
        for (int b = 0; b < 2; ++b) {
            uint8_t mask = 1 << b;
            if ((btns & mask) != (m_prevButtons & mask)) {
                m_prevButtons = btns;
                return {HidEvent::Button, 0, b + 1, (btns & mask) ? 0 : 1};
            }
        }
        m_prevButtons = btns;
    }

    // Encoder rotation
    if (m_firstReport) {
        m_firstReport = false;
        m_prevEncoder = enc;
        return {};
    }

    if (enc != m_prevEncoder) {
        int delta = static_cast<int>(enc) - static_cast<int>(m_prevEncoder);
        // Handle wrap-around (0→255 = -1, 255→0 = +1)
        if (delta > 128) delta -= 256;
        if (delta < -128) delta += 256;
        m_prevEncoder = enc;
        return {HidEvent::Rotate, delta, 0, 0};
    }

    return {};
}

// ── Griffin PowerMate ───────────────────────────────────────────────────────
// 6-byte reports. Byte 0 = button (0/1). Byte 1 = signed rotation delta.

HidEvent GriffinPowerMateParser::parse(const uint8_t* buf, size_t len)
{
    if (len < 2) return {};

    uint8_t btn = buf[0];
    auto rot = static_cast<int8_t>(buf[1]);

    // Button change
    if (btn != m_prevButton) {
        m_prevButton = btn;
        return {HidEvent::Button, 0, 1, btn ? 0 : 1};
    }

    // Rotation
    if (rot != 0)
        return {HidEvent::Rotate, static_cast<int>(rot), 0, 0};

    return {};
}

// ── Contour ShuttleXpress ───────────────────────────────────────────────────
// 5-byte reports. Byte 0 = shuttle position (signed, -7..+7).
// Byte 1 = jog counter (wrapping uint8). Bytes 2-3 = button bitmask (5 btns).

HidEvent ShuttleXpressParser::parse(const uint8_t* buf, size_t len)
{
    if (len < 4) return {};

    uint8_t jog = buf[1];
    uint8_t btns = buf[3];

    // Buttons
    if (btns != m_prevButtons) {
        for (int b = 0; b < 5; ++b) {
            uint8_t mask = 1 << b;
            if ((btns & mask) != (m_prevButtons & mask)) {
                m_prevButtons = btns;
                return {HidEvent::Button, 0, b + 1, (btns & mask) ? 0 : 1};
            }
        }
        m_prevButtons = btns;
    }

    // Jog wheel (relative, wrapping)
    if (m_firstReport) {
        m_firstReport = false;
        m_prevJog = jog;
        return {};
    }

    if (jog != m_prevJog) {
        int delta = static_cast<int>(jog) - static_cast<int>(m_prevJog);
        if (delta > 128) delta -= 256;
        if (delta < -128) delta += 256;
        m_prevJog = jog;
        return {HidEvent::Rotate, delta, 0, 0};
    }

    return {};
}

// ── Contour ShuttlePro v2 ──────────────────────────────────────────────────
// Same layout as ShuttleXpress but 15 buttons across bytes 2-3.

HidEvent ShuttleProV2Parser::parse(const uint8_t* buf, size_t len)
{
    if (len < 4) return {};

    uint8_t jog = buf[1];
    uint16_t btns = static_cast<uint16_t>(buf[3] << 8 | buf[2]);

    // Buttons
    if (btns != m_prevButtons) {
        for (int b = 0; b < 15; ++b) {
            uint16_t mask = 1 << b;
            if ((btns & mask) != (m_prevButtons & mask)) {
                m_prevButtons = btns;
                return {HidEvent::Button, 0, b + 1, (btns & mask) ? 0 : 1};
            }
        }
        m_prevButtons = btns;
    }

    // Jog wheel
    if (m_firstReport) {
        m_firstReport = false;
        m_prevJog = jog;
        return {};
    }

    if (jog != m_prevJog) {
        int delta = static_cast<int>(jog) - static_cast<int>(m_prevJog);
        if (delta > 128) delta -= 256;
        if (delta < -128) delta += 256;
        m_prevJog = jog;
        return {HidEvent::Rotate, delta, 0, 0};
    }

    return {};
}

} // namespace AetherSDR
#endif
