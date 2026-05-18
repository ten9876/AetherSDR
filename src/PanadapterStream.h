cpp
#ifndef PANADAPTERSTREAM_H
#define PANADAPTERSTREAM_H

#include <QMap>
#include <QSet>
#include <QObject>
#include <QLoggingCategory>
#include <cstdint>
#include <utility>

// Forward declaration for the per-stream PLC state
struct AudioPlcState;

// Declare a logging category for this class (used for debug/info/warning messages)
Q_DECLARE_LOGGING_CATEGORY(panadapterStreamLog)

/**
 * @brief Manages a stream of panadapter (spectrum/waterfall) audio data.
 *
 * This class handles the lifecycle of audio streams identified by VITA-49
 * stream identifiers.  It maintains packet-loss concealment state per stream
 * and provides callbacks for stream addition and removal.
 *
 * Thread safety: This class is **not** thread-safe. All operations must be
 * performed from the same thread (typically the Qt main event loop).
 */
class PanadapterStream : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Constructs a PanadapterStream.
     * @param parent  QObject parent (default nullptr).
     */
    explicit PanadapterStream(QObject *parent = nullptr);

    /**
     * @brief Destructor.
     *
     * Emits streamRemoved for every stream that still has PLC state,
     * then cleans up all internal data.
     */
    ~PanadapterStream() override;

    // ------------------------------------------------------------------
    // Public interface
    // ------------------------------------------------------------------

    /**
     * @brief Sets the set of stream IDs that this panadapter owns.
     *
     * Ownership means the panadapter is the active sink for these streams.
     * Streams that were previously owned but are no longer present in \a ids
     * are torn down (PLC state removed, resources released, signal emitted).
     *
     * New stream IDs are recognised but no explicit signal is emitted for
     * them – the PLC state is created lazily when `plcState()` is first
     * called with the new ID.
     *
     * @param ids  Set of owned VITA-49 stream identifiers.
     *
     * @note  Duplicate IDs are silently ignored.
     */
    void setOwnedStreamIds(const QSet<uint64_t> &ids);

    /**
     * @brief Returns the PLC state for a given stream, creating it if needed.
     *
     * If the stream ID has not been seen before, a new AudioPlcState is
     * created and stored internally.  The caller receives a modifiable
     * reference to the PLC state.
     *
     * @warning  The returned reference may become invalid if the stream is
     * removed.  Do not store the reference across calls that could trigger
     * stream removal (e.g. setOwnedStreamIds()).
     *
     * @param streamId  VITA-49 stream identifier.
     * @return Reference to the AudioPlcState for the stream.
     *
     * @throws std::out_of_range  Only if an invalid zero stream ID is given;
     *                            otherwise always succeeds.
     */
    AudioPlcState &plcState(uint64_t streamId);

    /**
     * @brief Returns the current set of known stream IDs (that have PLC state).
     *
     * This is a snapshot; the set may change asynchronously.
     *
     * @return Unordered set of VITA-49 stream identifiers.
     */
    QSet<uint64_t> knownStreamIds() const;

    /**
     * @brief Removes **all** stream PLC states.
     *
     * Emits streamRemoved for each removed stream.
     * Equivalent to calling setOwnedStreamIds(QSet<uint64_t>()).
     */
    void clearAllStreams();

signals:
    /**
     * @brief Emitted when a stream is removed (torn down).
     *
     * The PLC state and any associated resources for this stream have
     * been released before the signal is emitted.
     *
     * @param streamId  The VITA-49 stream identifier that was removed.
     */
    void streamRemoved(uint64_t streamId);

private:
    // ------------------------------------------------------------------
    // Private helpers
    // ------------------------------------------------------------------

    /**
     * @brief Removes PLC state and tears down the given stream.
     *
     * Removes the entry from the internal map, emits streamRemoved,
     * and logs the removal.
     *
     * @param streamId  Stream identifier to remove.
     *
     * @note  This is a no-op if the stream does not exist.
     */
    void removeStream(uint64_t streamId);

    /**
     * @brief Logs stream-related events with appropriate severity.
     *
     * @param level  QtMsgType (QtDebugMsg, QtInfoMsg, QtWarningMsg, etc.)
     * @param message  Human-readable message (ideally including stream ID).
     */
    void logStreamEvent(QtMsgType level, const QString &message) const;

    // ------------------------------------------------------------------
    // Data members
    // ------------------------------------------------------------------

    /// Per-stream PLC state, keyed by VITA-49 stream ID.
    /// QMap is used for deterministic iteration order (by stream ID).
    /// For large numbers of streams consider switching to QHash.
    QMap<uint64_t, AudioPlcState> m_audioPlc;
};

#endif // PANADAPTERSTREAM_H