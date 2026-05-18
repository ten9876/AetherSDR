cpp
#include "PanadapterStream.h"
#include "AudioPlc.h"

#include <QLoggingCategory>
#include <QSet>
#include <memory>

Q_LOGGING_CATEGORY(panadapterStreamLog, "sdrbase.dsp.PanadapterStream", QtInfoMsg)

namespace sdrbase {
namespace dsp {

/**
 * @brief Constructs a PanadapterStream.
 * @param parent Parent QObject.
 */
PanadapterStream::PanadapterStream(QObject* parent)
    : QObject(parent)
    , m_ownedStreamIds()
{
    qCDebug(panadapterStreamLog) << "PanadapterStream created";
}

PanadapterStream::~PanadapterStream()
{
    qCDebug(panadapterStreamLog) << "PanadapterStream destructor running, cleaning up"
                                 << m_audioPlc.size() << "AudioPlc entries";
    // m_audioPlc is automatically cleaned up by std::unique_ptr
    m_audioPlc.clear();
}

/**
 * @brief Replaces the set of owned stream IDs, starting new streams and
 *        stopping any that are no longer owned.
 *
 * This method cleanly handles stream lifecycle: for streams that are removed,
 * any associated AudioPlc instance (if present) is destroyed and the audio
 * stream is stopped. For new streams, only audio stream setup is triggered;
 * AudioPlc instances are created lazily when the first audio packet arrives.
 *
 * @param newStreamIds  The new set of stream IDs to own.
 *
 * @note This method is typically called when the radio connection updates
 *       active slices or DAX channels. It emit changes via ownedStreamIdsChanged.
 */
void PanadapterStream::setOwnedStreamIds(const QSet<uint64_t>& newStreamIds)
{
    // Ensure input is not null or absurdly large; no security issues expected.
    if (newStreamIds.isEmpty() && m_ownedStreamIds.isEmpty()) {
        return; // No change
    }

    // Compute differences using set operations (O(N log N) typical).
    const QSet<uint64_t> addedIds   = newStreamIds - m_ownedStreamIds;
    const QSet<uint64_t> removedIds = m_ownedStreamIds - newStreamIds;

    // --- Handle removed streams ---
    for (uint64_t removedId : removedIds) {
        // Remove any existing AudioPlc entry (if it exists) – RAII destroys the object.
        auto it = m_audioPlc.find(removedId);
        if (it != m_audioPlc.end()) {
            m_audioPlc.erase(it);
            qCDebug(panadapterStreamLog) << "Removed AudioPlc for stream" << removedId;
        }
        // Always stop the audio stream, even if PLC was never created.
        stopAudioStream(removedId);
        // Remove from owned set (will be updated at end)
        m_ownedStreamIds.remove(removedId);
    }

    // --- Handle added streams ---
    for (uint64_t addedId : addedIds) {
        // Lazily inserted AudioPlc – only start the underlying stream now.
        startAudioStream(addedId);
        m_ownedStreamIds.insert(addedId);
    }

    // Emit notification about the final set (order not guaranteed but consistent)
    emit ownedStreamIdsChanged(m_ownedStreamIds);
}

/**
 * @brief Starts or initialises an audio stream identified by `streamId`.
 *
 * This is a stub method; real implementations would set up VITA-49
 * decoders, buffer pools, or hardware resources. Errors should be
 * logged and possibly signalled.
 *
 * @param streamId  VITA-49 stream identifier.
 */
void PanadapterStream::startAudioStream(uint64_t streamId)
{
    Q_UNUSED(streamId)
    qCDebug(panadapterStreamLog) << "Starting audio stream" << streamId;
    // TODO: actual stream start logic
}

/**
 * @brief Stops and cleans up an audio stream identified by `streamId`.
 *
 * This is a stub method; real implementations would tear down any
 * resources allocated in startAudioStream(). Errors should be logged.
 *
 * @param streamId  VITA-49 stream identifier.
 */
void PanadapterStream::stopAudioStream(uint64_t streamId)
{
    Q_UNUSED(streamId)
    qCDebug(panadapterStreamLog) << "Stopping audio stream" << streamId;
    // TODO: actual stream stop logic
}

} // namespace dsp
} // namespace sdrbase