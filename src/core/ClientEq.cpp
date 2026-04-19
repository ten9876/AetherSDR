#include "ClientEq.h"

#include <algorithm>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

// Minimum difference in smoothed params before we bother recomputing the
// biquad. Tuned so sub-audible parameter motion doesn't burn CPU on
// coefficient updates we can't hear.
constexpr float kFreqEpsHz = 0.05f;
constexpr float kGainEpsDb = 0.001f;
constexpr float kQEps      = 0.0005f;

// One-pole smoother time constant (samples to reach ~63% of target) —
// 15ms at 24 kHz is ~360 samples, computed in prepare().
constexpr float kSmoothTimeConstantSec = 0.015f;

float dbToAmp(float db) noexcept
{
    // A = 10^(dB / 40) — the sqrt() form used in RBJ peaking/shelf
    return std::pow(10.0f, db * 0.025f);
}

} // namespace

ClientEq::ClientEq()
{
    // Default: all bands initialised with reasonable peak-at-1kHz values
    // (already handled by AtomicBand default member initialisers). Just
    // mark runtime state as matching so we don't recompute on first run.
    for (int i = 0; i < kMaxBands; ++i) {
        m_runtime[i].current.freqHz = m_bands[i].freqHz.load(std::memory_order_relaxed);
        m_runtime[i].current.gainDb = m_bands[i].gainDb.load(std::memory_order_relaxed);
        m_runtime[i].current.q      = m_bands[i].q.load(std::memory_order_relaxed);
        m_runtime[i].cachedType     = static_cast<FilterType>(
            m_bands[i].type.load(std::memory_order_relaxed));
        m_runtime[i].cachedEnabled  = m_bands[i].enabled.load(std::memory_order_relaxed);
    }
}

void ClientEq::prepare(double sampleRate)
{
    m_sampleRate = sampleRate;
    // One-pole smoother coefficient: y += a * (target - y).
    // a = 1 - exp(-1 / (fs * τ)). Approximated via 1 / (fs * τ) for small τ.
    m_smoothCoeff = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * kSmoothTimeConstantSec));

    // Force coefficient recompute on first process() call
    for (int i = 0; i < kMaxBands; ++i) {
        computeCoefficients(m_runtime[i]);
        m_runtime[i].z1L = m_runtime[i].z2L = 0.0f;
        m_runtime[i].z1R = m_runtime[i].z2R = 0.0f;
    }
}

void ClientEq::setEnabled(bool on) noexcept
{
    m_enabled.store(on, std::memory_order_release);
}

bool ClientEq::isEnabled() const noexcept
{
    return m_enabled.load(std::memory_order_acquire);
}

void ClientEq::setBand(int idx, const BandParams& p) noexcept
{
    if (idx < 0 || idx >= kMaxBands) return;
    AtomicBand& b = m_bands[idx];
    b.freqHz.store(std::clamp(p.freqHz, 10.0f, 20000.0f), std::memory_order_relaxed);
    b.gainDb.store(std::clamp(p.gainDb, -24.0f, 24.0f),   std::memory_order_relaxed);
    b.q     .store(std::clamp(p.q,      0.1f,  18.0f),    std::memory_order_relaxed);
    b.type  .store(static_cast<int>(p.type),              std::memory_order_relaxed);
    b.enabled.store(p.enabled,                            std::memory_order_relaxed);
    // Bump version last so audio thread sees a consistent snapshot
    b.version.fetch_add(1, std::memory_order_release);
}

ClientEq::BandParams ClientEq::band(int idx) const noexcept
{
    BandParams p;
    if (idx < 0 || idx >= kMaxBands) return p;
    const AtomicBand& b = m_bands[idx];
    p.freqHz  = b.freqHz.load(std::memory_order_relaxed);
    p.gainDb  = b.gainDb.load(std::memory_order_relaxed);
    p.q       = b.q.load(std::memory_order_relaxed);
    p.type    = static_cast<FilterType>(b.type.load(std::memory_order_relaxed));
    p.enabled = b.enabled.load(std::memory_order_relaxed);
    return p;
}

void ClientEq::setActiveBandCount(int n) noexcept
{
    m_activeBandCount.store(std::clamp(n, 0, kMaxBands), std::memory_order_release);
}

int ClientEq::activeBandCount() const noexcept
{
    return m_activeBandCount.load(std::memory_order_acquire);
}

void ClientEq::reset() noexcept
{
    for (int i = 0; i < kMaxBands; ++i) {
        m_runtime[i].z1L = m_runtime[i].z2L = 0.0f;
        m_runtime[i].z1R = m_runtime[i].z2R = 0.0f;
    }
}

bool ClientEq::smoothTowardTarget(int idx, Runtime& runtime,
                                  const AtomicBand& target,
                                  float smoothCoeff) noexcept
{
    const float tFreq = target.freqHz.load(std::memory_order_relaxed);
    const float tGain = target.gainDb.load(std::memory_order_relaxed);
    const float tQ    = target.q.load(std::memory_order_relaxed);

    const float newFreq = runtime.current.freqHz + smoothCoeff * (tFreq - runtime.current.freqHz);
    const float newGain = runtime.current.gainDb + smoothCoeff * (tGain - runtime.current.gainDb);
    const float newQ    = runtime.current.q      + smoothCoeff * (tQ    - runtime.current.q);

    const bool changed = (std::fabs(newFreq - runtime.current.freqHz) > kFreqEpsHz)
                      || (std::fabs(newGain - runtime.current.gainDb) > kGainEpsDb)
                      || (std::fabs(newQ    - runtime.current.q     ) > kQEps);

    runtime.current.freqHz = newFreq;
    runtime.current.gainDb = newGain;
    runtime.current.q      = newQ;
    (void)idx;
    return changed;
}

void ClientEq::computeCoefficients(Runtime& runtime) noexcept
{
    // RBJ Audio EQ Cookbook. Each branch produces b0/b1/b2/a0/a1/a2,
    // we normalise by a0 at the end so process() can skip the division.
    const float freq = std::clamp(runtime.current.freqHz,
                                  10.0f, static_cast<float>(m_sampleRate * 0.49f));
    const float q    = std::max(0.1f, runtime.current.q);
    const float A    = dbToAmp(runtime.current.gainDb);
    const float omega = kTwoPi * freq / static_cast<float>(m_sampleRate);
    const float cosW  = std::cos(omega);
    const float sinW  = std::sin(omega);
    const float alpha = sinW / (2.0f * q);

    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a0 = 1.0f, a1 = 0.0f, a2 = 0.0f;

    switch (runtime.cachedType) {
    case FilterType::Peak: {
        b0 = 1.0f + alpha * A;
        b1 = -2.0f * cosW;
        b2 = 1.0f - alpha * A;
        a0 = 1.0f + alpha / A;
        a1 = -2.0f * cosW;
        a2 = 1.0f - alpha / A;
        break;
    }
    case FilterType::LowShelf: {
        const float sqrtA  = std::sqrt(A);
        const float twoSqrtAalpha = 2.0f * sqrtA * alpha;
        b0 =        A * ((A + 1.0f) - (A - 1.0f) * cosW + twoSqrtAalpha);
        b1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cosW);
        b2 =        A * ((A + 1.0f) - (A - 1.0f) * cosW - twoSqrtAalpha);
        a0 =             (A + 1.0f) + (A - 1.0f) * cosW + twoSqrtAalpha;
        a1 =     -2.0f * ((A - 1.0f) + (A + 1.0f) * cosW);
        a2 =             (A + 1.0f) + (A - 1.0f) * cosW - twoSqrtAalpha;
        break;
    }
    case FilterType::HighShelf: {
        const float sqrtA  = std::sqrt(A);
        const float twoSqrtAalpha = 2.0f * sqrtA * alpha;
        b0 =        A * ((A + 1.0f) + (A - 1.0f) * cosW + twoSqrtAalpha);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cosW);
        b2 =        A * ((A + 1.0f) + (A - 1.0f) * cosW - twoSqrtAalpha);
        a0 =             (A + 1.0f) - (A - 1.0f) * cosW + twoSqrtAalpha;
        a1 =      2.0f * ((A - 1.0f) - (A + 1.0f) * cosW);
        a2 =             (A + 1.0f) - (A - 1.0f) * cosW - twoSqrtAalpha;
        break;
    }
    case FilterType::LowPass: {
        b0 = (1.0f - cosW) * 0.5f;
        b1 =  1.0f - cosW;
        b2 = (1.0f - cosW) * 0.5f;
        a0 =  1.0f + alpha;
        a1 = -2.0f * cosW;
        a2 =  1.0f - alpha;
        break;
    }
    case FilterType::HighPass: {
        b0 =  (1.0f + cosW) * 0.5f;
        b1 = -(1.0f + cosW);
        b2 =  (1.0f + cosW) * 0.5f;
        a0 =  1.0f + alpha;
        a1 = -2.0f * cosW;
        a2 =  1.0f - alpha;
        break;
    }
    }

    const float invA0 = 1.0f / a0;
    runtime.coeff.b0 = b0 * invA0;
    runtime.coeff.b1 = b1 * invA0;
    runtime.coeff.b2 = b2 * invA0;
    runtime.coeff.a1 = a1 * invA0;
    runtime.coeff.a2 = a2 * invA0;
}

void ClientEq::process(float* interleaved, int frames, int channels) noexcept
{
    if (!m_enabled.load(std::memory_order_acquire)) return;
    if (channels != 1 && channels != 2) return;
    if (frames <= 0) return;

    const int activeCount = m_activeBandCount.load(std::memory_order_acquire);
    if (activeCount <= 0) return;

    // Scale the per-sample smoother coefficient up to match this block's
    // duration. effCoeff = 1 - (1 - α)^frames is the closed form of N
    // per-sample one-pole steps applied at once, giving the expected
    // ~15ms time constant regardless of block size.
    const float effCoeff = 1.0f - std::pow(1.0f - m_smoothCoeff,
                                           static_cast<float>(frames));

    // Per-band: check version, smooth parameters, recompute coefficients
    // when anything changes. We force a recompute on version change too,
    // because a filter-type swap won't move the smoothed params at all
    // and would otherwise keep running the old coefficients.
    for (int i = 0; i < activeCount; ++i) {
        Runtime& rt = m_runtime[i];
        AtomicBand& ab = m_bands[i];

        bool needRecompute = false;
        const uint64_t version = ab.version.load(std::memory_order_acquire);
        if (version != rt.lastVersion) {
            rt.cachedType = static_cast<FilterType>(
                ab.type.load(std::memory_order_relaxed));
            rt.cachedEnabled = ab.enabled.load(std::memory_order_relaxed);
            rt.lastVersion = version;
            needRecompute = true;
        }

        if (smoothTowardTarget(i, rt, ab, effCoeff)) {
            needRecompute = true;
        }
        if (needRecompute) {
            computeCoefficients(rt);
        }
    }

    // Process — per-sample, cascade all active enabled bands.
    // Direct Form II Transposed biquad:
    //   y[n] = b0*x[n] + z1
    //   z1   = b1*x[n] - a1*y[n] + z2
    //   z2   = b2*x[n] - a2*y[n]
    if (channels == 2) {
        for (int f = 0; f < frames; ++f) {
            float l = interleaved[f * 2];
            float r = interleaved[f * 2 + 1];
            for (int i = 0; i < activeCount; ++i) {
                Runtime& rt = m_runtime[i];
                if (!rt.cachedEnabled) continue;
                const Coeff& c = rt.coeff;

                const float yl = c.b0 * l + rt.z1L;
                rt.z1L = c.b1 * l - c.a1 * yl + rt.z2L;
                rt.z2L = c.b2 * l - c.a2 * yl;
                l = yl;

                const float yr = c.b0 * r + rt.z1R;
                rt.z1R = c.b1 * r - c.a1 * yr + rt.z2R;
                rt.z2R = c.b2 * r - c.a2 * yr;
                r = yr;
            }
            interleaved[f * 2]     = l;
            interleaved[f * 2 + 1] = r;
        }
    } else {
        for (int f = 0; f < frames; ++f) {
            float x = interleaved[f];
            for (int i = 0; i < activeCount; ++i) {
                Runtime& rt = m_runtime[i];
                if (!rt.cachedEnabled) continue;
                const Coeff& c = rt.coeff;
                const float y = c.b0 * x + rt.z1L;
                rt.z1L = c.b1 * x - c.a1 * y + rt.z2L;
                rt.z2L = c.b2 * x - c.a2 * y;
                x = y;
            }
            interleaved[f] = x;
        }
    }
}

} // namespace AetherSDR
