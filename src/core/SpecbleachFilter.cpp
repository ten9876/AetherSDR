#ifdef HAVE_SPECBLEACH

#include "SpecbleachFilter.h"
#include <specbleach_denoiser.h>
#include <cstring>
#include <algorithm>
#include <QDebug>

namespace AetherSDR {

static constexpr int kSampleRate = 24000;
static constexpr float kFrameSizeMs = 40.0f;  // 40ms frames (~960 samples)

SpecbleachFilter::SpecbleachFilter()
{
    m_handle = specbleach_initialize(kSampleRate, kFrameSizeMs);
    if (!m_handle) {
        qWarning() << "SpecbleachFilter: failed to initialize";
        return;
    }
    applyParams();
    qDebug() << "SpecbleachFilter: initialized, latency ="
             << specbleach_get_latency(m_handle) << "samples";
}

SpecbleachFilter::~SpecbleachFilter()
{
    if (m_handle)
        specbleach_free(m_handle);
}

void SpecbleachFilter::reset()
{
    if (m_handle)
        specbleach_reset_noise_profile(m_handle);
}

void SpecbleachFilter::applyParams()
{
    if (!m_handle) return;

    SpectralBleachDenoiserParameters params{};
    params.learn_noise = 0;
    params.residual_listen = false;
    params.reduction_amount = m_reduction.load();
    params.smoothing_factor = m_smoothing.load();
    params.whitening_factor = m_whitening.load();
    params.adaptive_noise = m_adaptive.load() ? 1 : 0;
    params.noise_estimation_method = m_noiseMethod.load();
    params.masking_depth = m_maskingDepth.load();
    params.suppression_strength = m_suppression.load();
    params.aggressiveness = 0.0f;
    params.tonal_reduction = 0.0f;

    specbleach_load_parameters(m_handle, params);
    m_paramsDirty = false;
}

QByteArray SpecbleachFilter::process(const QByteArray& pcm24kStereo)
{
    if (!m_handle)
        return pcm24kStereo;

    // Apply parameter changes if dirty
    if (m_paramsDirty.load())
        applyParams();

    const int totalSamples = pcm24kStereo.size() / sizeof(int16_t);
    const int monoSamples = totalSamples / 2;
    if (monoSamples <= 0)
        return pcm24kStereo;

    // Resize buffers if needed
    if (static_cast<int>(m_monoIn.size()) < monoSamples) {
        m_monoIn.resize(monoSamples);
        m_monoOut.resize(monoSamples);
    }

    // Stereo int16 → mono float
    const auto* in = reinterpret_cast<const int16_t*>(pcm24kStereo.constData());
    for (int i = 0; i < monoSamples; ++i)
        m_monoIn[i] = (in[i * 2] + in[i * 2 + 1]) / (2.0f * 32768.0f);

    // Process
    specbleach_process(m_handle, monoSamples, m_monoIn.data(), m_monoOut.data());

    // Mono float → stereo int16
    QByteArray result(totalSamples * sizeof(int16_t), Qt::Uninitialized);
    auto* out = reinterpret_cast<int16_t*>(result.data());
    for (int i = 0; i < monoSamples; ++i) {
        int16_t s = static_cast<int16_t>(std::clamp(m_monoOut[i] * 32768.0f, -32768.0f, 32767.0f));
        out[i * 2]     = s;
        out[i * 2 + 1] = s;
    }

    return result;
}

// Parameter setters — mark dirty so next process() applies them
void SpecbleachFilter::setReductionAmount(float dB)   { m_reduction = std::clamp(dB, 0.0f, 40.0f); m_paramsDirty = true; }
void SpecbleachFilter::setSmoothingFactor(float pct)   { m_smoothing = std::clamp(pct, 0.0f, 100.0f); m_paramsDirty = true; }
void SpecbleachFilter::setWhiteningFactor(float pct)   { m_whitening = std::clamp(pct, 0.0f, 100.0f); m_paramsDirty = true; }
void SpecbleachFilter::setAdaptiveNoise(bool on)       { m_adaptive = on; m_paramsDirty = true; }
void SpecbleachFilter::setNoiseEstimationMethod(int m) { m_noiseMethod = std::clamp(m, 0, 2); m_paramsDirty = true; }
void SpecbleachFilter::setMaskingDepth(float v)        { m_maskingDepth = std::clamp(v, 0.0f, 1.0f); m_paramsDirty = true; }
void SpecbleachFilter::setSuppressionStrength(float v) { m_suppression = std::clamp(v, 0.0f, 1.0f); m_paramsDirty = true; }

} // namespace AetherSDR

#endif // HAVE_SPECBLEACH
