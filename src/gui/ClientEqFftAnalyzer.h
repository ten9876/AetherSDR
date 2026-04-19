#pragma once

#include <array>
#include <vector>

namespace AetherSDR {

// Small, self-contained FFT analyzer used by the Client EQ editor to
// render a live spectrum behind the response curve. Fixed 256-point
// radix-2 Cooley-Tukey — enough bins for voice-band visualization at
// 24 kHz, runs in microseconds on the UI thread so we can tick it on
// a simple QTimer without plumbing through a worker.
//
// Usage:
//   ClientEqFftAnalyzer fft;
//   fft.update(samples, 256);          // from audio tap
//   for (auto db : fft.magnitudesDb()) ...
//
// Magnitude bins are exponentially smoothed per-bin with asymmetric
// attack (fast) and decay (slow) — the classic "analyzer follow" feel.
class ClientEqFftAnalyzer {
public:
    static constexpr int kFftSize = 256;
    static constexpr int kBinCount = kFftSize / 2 + 1;  // 0 Hz .. Nyquist

    ClientEqFftAnalyzer();

    // Feed the most-recent kFftSize samples. The window (Hann) and the
    // FFT run inline; smoothed magnitudes are updated afterwards.
    void update(const float* samples, int count) noexcept;

    // Reset smoothing state — e.g. when the editor hides so the next
    // opening doesn't show frozen bars from the last session.
    void reset() noexcept;

    // Magnitudes in dB, length kBinCount. Floor is kFloorDb to keep the
    // log curve from collapsing to -infinity at silent bins.
    const std::vector<float>& magnitudesDb() const { return m_smoothedDb; }

    // Frequency of bin index i for a given sample rate.
    static float binFreq(int bin, double sampleRate) {
        return static_cast<float>(bin * sampleRate / kFftSize);
    }

    static constexpr float kFloorDb = -100.0f;

private:
    void buildWindow();

    std::array<float, kFftSize>   m_window;    // precomputed Hann
    std::vector<float>            m_smoothedDb; // size kBinCount
    bool                          m_primed{false};
};

} // namespace AetherSDR
