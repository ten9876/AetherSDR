#pragma once

#include <atomic>
#include <cstdint>

class QByteArray;

namespace AetherSDR {

// Generates a sine wave tone at the configured CW pitch frequency for local
// PC sidetone feedback.  Produces stereo float32 PCM at 24 kHz matching the
// existing RX audio format in AudioEngine.
//
// Thread safety: all setters use std::atomic so they can be called from the
// main thread while the audio thread calls generate().
class CwSidetoneGenerator {
public:
    static constexpr int kSampleRate = 24000;

    CwSidetoneGenerator();

    // Set the tone frequency (Hz).  Typical CW pitch: 400–800 Hz.
    void setPitch(int hz);
    int  pitch() const { return m_pitch.load(); }

    // Set the volume (0–100).  Maps linearly to amplitude 0.0–0.5.
    void setGain(int gain);
    int  gain() const { return m_gain.load(); }

    // Key the tone on/off.  Uses a short ramp (~2ms) to avoid clicks.
    void setKeyed(bool on);
    bool isKeyed() const { return m_keyed.load(); }

    // Generate numFrames of stereo float32 PCM and mix (add) into the
    // provided buffer.  The buffer must already contain valid audio data
    // (or silence) — the tone is added on top.  Returns true if any
    // non-zero samples were written (tone was active or ramping).
    bool mixInto(float* stereoFloat32, int numFrames);

private:
    std::atomic<int>  m_pitch{600};
    std::atomic<int>  m_gain{50};
    std::atomic<bool> m_keyed{false};

    // Phase accumulator (audio thread only — no atomic needed)
    double m_phase{0.0};

    // Envelope for click-free keying (audio thread only)
    float m_envelope{0.0f};

    // Ramp time in samples (~2ms at 24kHz = 48 samples)
    static constexpr int kRampSamples = 48;
    static constexpr float kRampIncrement = 1.0f / kRampSamples;
};

} // namespace AetherSDR
