#include "CwSidetoneGenerator.h"

#include <cmath>
#include <algorithm>

namespace AetherSDR {

CwSidetoneGenerator::CwSidetoneGenerator() = default;

void CwSidetoneGenerator::setPitch(int hz)
{
    m_pitch.store(std::clamp(hz, 100, 6000));
}

void CwSidetoneGenerator::setGain(int gain)
{
    m_gain.store(std::clamp(gain, 0, 100));
}

void CwSidetoneGenerator::setKeyed(bool on)
{
    m_keyed.store(on);
}

bool CwSidetoneGenerator::mixInto(float* stereoFloat32, int numFrames)
{
    const bool keyed = m_keyed.load();
    // Early exit: tone is off and envelope has fully decayed
    if (!keyed && m_envelope <= 0.0f) {
        return false;
    }

    const double freq = static_cast<double>(m_pitch.load());
    const float amplitude = m_gain.load() / 200.0f;  // 0–100 → 0.0–0.5
    const double phaseInc = 2.0 * M_PI * freq / kSampleRate;
    bool wrote = false;

    for (int i = 0; i < numFrames; ++i) {
        // Ramp envelope toward target (1.0 if keyed, 0.0 if not)
        if (keyed) {
            m_envelope = std::min(m_envelope + kRampIncrement, 1.0f);
        } else {
            m_envelope = std::max(m_envelope - kRampIncrement, 0.0f);
        }

        if (m_envelope > 0.0f) {
            float sample = static_cast<float>(std::sin(m_phase)) * amplitude * m_envelope;
            stereoFloat32[2 * i]     += sample;  // L
            stereoFloat32[2 * i + 1] += sample;  // R
            m_phase += phaseInc;
            wrote = true;
        }
    }

    // Keep phase in [0, 2π) to avoid precision loss over time
    if (m_phase > 2.0 * M_PI) {
        m_phase -= 2.0 * M_PI * std::floor(m_phase / (2.0 * M_PI));
    }

    return wrote;
}

} // namespace AetherSDR
