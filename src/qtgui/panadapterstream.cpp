cpp
// panadapterstream.cpp
// ---------------------------------------------------------------------------
// Copyright (C) 2025   <Your Company or Personal Credit>
// SPDX-License-Identifier: GPL-3.0-or-later
// ---------------------------------------------------------------------------

#include "panadapterstream.h"

#include <QLoggingCategory>
#include <QMap>
#include <QSet>
#include <QAtomicInt>
#include <memory>
#include <limits>
#include <utility>

#include "packetlossconcealment.h"

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------
Q_LOGGING_CATEGORY(lcPanadapterStream, "PanadapterStream")
static constexpr qsizetype kMaxExpectedPlcCount = 50;
static constexpr quint32 kInvalidStreamId        = 0;

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

/*!
 * \class PanadapterStream
 * \brief Manages audio streams and associated packet-loss concealment (PLC).
 *
 * Maintains a map of PacketLossConcealment objects keyed by VITA-49 stream
 * identifier.  Entries are created lazily upon first audio packet reception
 * and are automatically removed when the corresponding stream is torn down.
 *
 * \par Thread safety
 * This class is **not** thread-safe. All public methods must be called from
 * the same thread (typically the Qt main thread).  No internal locking is
 * performed.
 *
 * \warning Stream identifier `0` is reserved and treated as invalid.
 *          All methods will silently ignore or reject `0` and log a warning.
 */

/*!
 * \brief Constructs a PanadapterStream.
 * \param parent  Parent QObject (optional, may be nullptr).
 */
PanadapterStream::PanadapterStream(QObject *parent) noexcept
    : QObject(parent)
{
    qCDebug(lcPanadapterStream()) << "PanadapterStream created at" << this;
}

/*!
 * \brief Destructor. Clears all PLC entries and logs the count.
 */
PanadapterStream::~PanadapterStream() noexcept
{
    const qsizetype count = m_audioPlc.size();
    if (count > 0) {
        qCInfo(lcPanadapterStream())
            << "PanadapterStream at" << this << "destroyed – clearing"
            << count << "PLC entries";
    }
    m_audioPlc.clear();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/*!
 * \brief Removes all PLC entries whose stream IDs are not in \a owned.
 *
 * This method is the primary teardown hook for bulk stream removal.
 * Call it whenever the set of streams owned by this panadapter changes
 * (e.g., slice subscriptions are updated, DAX channel disconnected).
 *
 * \param owned  Set of stream identifiers that are currently owned.
 *               Duplicates are handled automatically by QSet.
 *
 * \par Performance
 * The implementation uses an iterator-based erase loop that performs
 * exactly one lookup per key (O(n) where n = number of PLC entries).
 * No check for `owned.isEmpty()` is performed because QSet::contains()
 * is constant time.
 *
 * \note A warning is logged if the total number of PLC entries exceeds
 *       \c kMaxExpectedPlcCount, as that may indicate a resource leak.
 *
 * \sa streamRemoved()
 */
void PanadapterStream::setOwnedStreamIds(const QSet<quint32>& owned) noexcept
{
    const qsizetype ownedCount = owned.size();
    const qsizetype plcCount   = m_audioPlc.size();

    qCDebug(lcPanadapterStream())
        << "setOwnedStreamIds: new owned set size =" << ownedCount
        << ", existing PLC entries =" << plcCount;

    // Guard against unbounded growth – log a warning to aid debugging.
    if (plcCount > kMaxExpectedPlcCount) {
        qCWarning(lcPanadapterStream())
            << "Unexpectedly large number of PLC entries:" << plcCount
            << " – expected at most" << kMaxExpectedPlcCount
            << ". Possible resource leak.";
    }

    // Iterator-based erase avoids a second lookup via contains().
    auto it = m_audioPlc.begin();
    while (it != m_audioPlc.end()) {
        if (Q_UNLIKELY(!owned.contains(it.key()))) {
            qCInfo(lcPanadapterStream())
                << "Removing PLC for stream" << it.key()
                << " – no longer owned";
            it = m_audioPlc.erase(it);
        } else {
            ++it;
        }
    }
}

/*!
 * \brief Removes the PLC entry for a single stream that has gone away.
 *
 * This is the fine-grained teardown hook, intended to be called from
 * signals such as `sliceRemoved`, `daxChannelDisconnected`, or
 * `audioStreamError`.  It avoids the cost of scanning the entire map
 * when only one stream is affected.
 *
 * \param streamId  VITA-49 stream identifier. Must be non-zero.
 *
 * \note If the stream has no associated PLC entry, the call is a no-op
 *       (logged at INFO level).
 *
 * \sa setOwnedStreamIds()
 */
void PanadapterStream::streamRemoved(quint32 streamId) noexcept
{
    // Input validation
    if (Q_UNLIKELY(streamId == kInvalidStreamId)) {
        qCWarning(lcPanadapterStream())
            << "streamRemoved called with zero streamId – ignoring";
        return;
    }

    // Perform removal
    const qsizetype removedCount = m_audioPlc.remove(streamId);
    if (removedCount == 0) {
        qCInfo(lcPanadapterStream())
            << "streamRemoved: no PLC entry for stream" << streamId
            << "(already removed?)";
    } else {
        qCInfo(lcPanadapterStream())
            << "streamRemoved: removed PLC entry for stream" << streamId;
    }

    // Notify sub‑systems (e.g., statistics, logging aggregator)
    Q_EMIT streamRemovedInternal(streamId);
}

// ---------------------------------------------------------------------------
// PLC accessors
// ---------------------------------------------------------------------------

/*!
 * \brief Returns the PLC object for \a streamId, or \c nullptr if none exists.
 *
 * \param streamId  VITA-49 stream identifier (must be non-zero).
 *
 * \return Pointer to the PacketLossConcealment object, or \c nullptr if the
 *         stream has no PLC entry or if \a streamId is zero.
 *
 * \par Ownership
 * The returned pointer remains valid as long as no PLC entries are added or
 * removed for the same stream.  Callers must **not** delete the returned
 * object; ownership stays with the PanadapterStream.
 *
 * \sa createOrGetAudioPlc()
 */
PacketLossConcealment* PanadapterStream::audioPlc(quint32 streamId) const noexcept
{
    if (Q_UNLIKELY(streamId == kInvalidStreamId)) {
        qCWarning(lcPanadapterStream())
            << "audioPlc called with zero streamId – returning nullptr";
        return nullptr;
    }

    const auto it = m_audioPlc.constFind(streamId);
    if (it == m_audioPlc.constEnd()) {
        qCDebug(lcPanadapterStream())
            << "audioPlc: no PLC for stream" << streamId;
        return nullptr;
    }

    return it->get();
}

/*!
 * \brief Returns the PLC object for \a streamId, creating one if absent.
 *
 * If the stream already has a PLC entry, the existing object is returned.
 * Otherwise a new PacketLossConcealment is allocated using \c std::make_unique.
 * If the allocation fails (very unlikely on modern systems), \c nullptr is
 * returned and a critical error is logged.
 *
 * \param streamId  VITA-49 stream identifier (must be non-zero).
 *
 * \return Pointer to a PacketLossConcealment, or \c nullptr on failure or
 *         invalid stream ID.
 *
 * \par Ownership
 * The returned pointer remains valid as long as the PLC entry for that stream
 * is not removed.  Callers must **not** delete the object; ownership stays
 * with the PanadapterStream.
 *
 * \par Exception safety
 * This function is noexcept; if \c std::make_unique throws
 * (which only happens if the allocator throws, and that typically terminates),
 * the program will abort. For production robustness, consider replacing with
 * a nothrow alternative if heap exhaustion is a concern.
 *
 * \sa audioPlc()
 */
PacketLossConcealment* PanadapterStream::createOrGetAudioPlc(quint32 streamId) noexcept
{
    // Input validation
    if (Q_UNLIKELY(streamId == kInvalidStreamId)) {
        qCWarning(lcPanadapterStream())
            << "createOrGetAudioPlc called with zero streamId – returning nullptr";
        return nullptr;
    }

    // Fast path: existing entry.
    {
        const auto it = m_audioPlc.constFind(streamId);
        if (it != m_audioPlc.constEnd()) {
            qCDebug(lcPanadapterStream())
                << "createOrGetAudioPlc: reusing existing PLC for stream" << streamId;
            return it->get();
        }
    }

    // Slow path: create a new PLC object.
    qCDebug(lcPanadapterStream())
        << "createOrGetAudioPlc: creating new PLC for stream" << streamId;

    // std::make_unique may throw std::bad_alloc – production code might
    // want to catch it (not done here because it's noexcept).  If we
    // truly need to avoid termination, a nothrow path could be added.
    auto plc = std::make_unique<PacketLossConcealment>();

    // Sanity check – make_unique should always succeed on a well‑provisioned
    // system, but we keep a safety net.
    if (Q_UNLIKELY(!plc)) {
        qCritical(lcPanadapterStream())
            << "Failed to allocate PacketLossConcealment for stream" << streamId;
        Q_EMIT plcAllocationFailed(streamId);
        return nullptr;
    }

    // Store the PLC object.
    PacketLossConcealment *rawPtr = plc.get();
    m_audioPlc.insert(streamId, std::move(plc));

    return rawPtr;
}