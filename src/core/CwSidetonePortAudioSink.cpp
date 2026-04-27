#include "CwSidetonePortAudioSink.h"
#include "CwSidetoneGenerator.h"
#include "LogManager.h"

#include <portaudio.h>

#include <cstring>

namespace AetherSDR {

CwSidetonePortAudioSink::CwSidetonePortAudioSink() = default;

CwSidetonePortAudioSink::~CwSidetonePortAudioSink()
{
    stop();
    if (m_paInitialized) {
        Pa_Terminate();
        m_paInitialized = false;
    }
}

bool CwSidetonePortAudioSink::start(const QAudioDevice& /*device*/,
                                    int desiredRateHz,
                                    CwSidetoneGenerator* generator)
{
    if (m_stream) return true;
    if (!generator) return false;

    if (!m_paInitialized) {
        const PaError err = Pa_Initialize();
        if (err != paNoError) {
            qCWarning(lcAudio) << "CwSidetonePortAudioSink: Pa_Initialize failed —"
                               << Pa_GetErrorText(err);
            return false;
        }
        m_paInitialized = true;
    }

    // We deliberately don't try to map QAudioDevice → PortAudio device
    // index.  The two libraries enumerate differently (Qt names devices
    // by Pulse/PipeWire/CoreAudio descriptor, PortAudio by host-API
    // index), and PortAudio's default-output choice tracks the OS's
    // default-sink setting reliably on every supported platform.  If a
    // user reports that sidetone goes to the wrong device we'll add a
    // mapping pass; until then `paNoDevice → use default` is the right
    // call.
    const PaDeviceIndex devIdx = Pa_GetDefaultOutputDevice();
    if (devIdx == paNoDevice) {
        qCWarning(lcAudio) << "CwSidetonePortAudioSink: no default output device";
        return false;
    }

    const PaDeviceInfo* devInfo = Pa_GetDeviceInfo(devIdx);
    if (!devInfo) {
        qCWarning(lcAudio) << "CwSidetonePortAudioSink: Pa_GetDeviceInfo returned null";
        return false;
    }

    const double sampleRate = desiredRateHz > 0 ? desiredRateHz : 48000;

    PaStreamParameters outParams{};
    outParams.device = devIdx;
    outParams.channelCount = 2;
    outParams.sampleFormat = paFloat32;
    // Lowest latency the device supports.  PortAudio uses this as a hint
    // and may give us a smaller actual latency.
    outParams.suggestedLatency = devInfo->defaultLowOutputLatency;
    outParams.hostApiSpecificStreamInfo = nullptr;

    // Store generator BEFORE opening so the very first callback (which
    // can fire before Pa_OpenStream returns on some platforms) sees it.
    m_generator.store(generator, std::memory_order_release);
    generator->setSampleRateHz(static_cast<int>(sampleRate));

    PaError err = Pa_OpenStream(&m_stream,
                                /*input*/  nullptr,
                                /*output*/ &outParams,
                                sampleRate,
                                paFramesPerBufferUnspecified,
                                paNoFlag,
                                &CwSidetonePortAudioSink::paCallback,
                                this);
    if (err != paNoError) {
        qCWarning(lcAudio) << "CwSidetonePortAudioSink: Pa_OpenStream failed —"
                           << Pa_GetErrorText(err);
        m_generator.store(nullptr, std::memory_order_release);
        return false;
    }

    err = Pa_StartStream(m_stream);
    if (err != paNoError) {
        qCWarning(lcAudio) << "CwSidetonePortAudioSink: Pa_StartStream failed —"
                           << Pa_GetErrorText(err);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
        m_generator.store(nullptr, std::memory_order_release);
        return false;
    }

    m_actualRate = static_cast<int>(sampleRate);

    const PaStreamInfo* streamInfo = Pa_GetStreamInfo(m_stream);
    qCInfo(lcAudio) << "CwSidetonePortAudioSink: started"
                    << "device=" << devInfo->name
                    << "rate=" << m_actualRate << "Hz"
                    << "outputLatency=" << (streamInfo ? streamInfo->outputLatency * 1000.0 : 0.0)
                    << "ms";
    return true;
}

int CwSidetonePortAudioSink::paCallback(const void* /*input*/,
                                        void* output,
                                        unsigned long frameCount,
                                        const PaStreamCallbackTimeInfo* /*timeInfo*/,
                                        PaStreamCallbackFlags /*statusFlags*/,
                                        void* userData)
{
    auto* self = static_cast<CwSidetonePortAudioSink*>(userData);
    auto* dst = static_cast<float*>(output);

    // Always start from silence — PortAudio doesn't guarantee zeroed
    // buffers and the generator mixes additively.
    std::memset(dst, 0, frameCount * 2 * sizeof(float));

    auto* gen = self->m_generator.load(std::memory_order_acquire);
    if (gen) gen->process(dst, static_cast<int>(frameCount));

    return paContinue;
}

void CwSidetonePortAudioSink::stop()
{
    if (m_stream) {
        // Halt the callback before clearing the generator pointer so we
        // don't race with paCallback dereferencing a torn-down generator.
        Pa_StopStream(m_stream);
        m_generator.store(nullptr, std::memory_order_release);
        Pa_CloseStream(m_stream);
        m_stream = nullptr;
    } else {
        m_generator.store(nullptr, std::memory_order_release);
    }
    m_actualRate = 0;
}

} // namespace AetherSDR
