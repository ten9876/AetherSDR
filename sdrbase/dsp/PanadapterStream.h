cpp
#pragma once

#include <QHash>
#include <QLoggingCategory>
#include <memory>   // for std::unique_ptr
#include <cstdint>  // for uint64_t
#include <cassert>  // for assert (optional, debug checks)

// ---------------------------------------------------------------------------
// Logging category for PanadapterStream
// ---------------------------------------------------------------------------
Q_DECLARE_LOGGING_CATEGORY(panadapterStreamLog)

// ---------------------------------------------------------------------------
// Forward declaration of AudioPlc (assumed defined elsewhere)
// ---------------------------------------------------------------------------
class AudioPlc;

// ---------------------------------------------------------------------------
// PanadapterStream – Manages packet‑loss concealment instances per VITA‑49
// audio stream.
//
// Thread safety: not thread‑safe.  All methods must be called from the same
// thread (typically the Qt main or GUIContext thread).
// ---------------------------------------------------------------------------
class PanadapterStream
{
public:
    // ---- Lifecycle -----------------------------------------------------

    /**
     * @brief  Default constructor.
     *
     * Initialises an empty stream manager.  No audio PLC instances exist yet.
     */
    PanadapterStream() noexcept = default;

    /**
     * @brief  Destructor.
     *
     * Automatically destroys all remaining AudioPlc instances via the
     * std::unique_ptr owners.
     */
    ~PanadapterStream() noexcept = default;

    // Disallow copy & move (RAII resource semantics)
    PanadapterStream(const PanadapterStream&)            = delete;
    PanadapterStream& operator=(const PanadapterStream&) = delete;
    PanadapterStream(PanadapterStream&&)                 = delete;
    PanadapterStream& operator=(PanadapterStream&&)      = delete;

    // ---- Public interface ----------------------------------------------

    /**
     * @brief  Removes the audio PLC state for a given VITA‑49 stream.
     *
     * This method must be called exactly once when a stream is torn down
     * (e.g. slice closed, DAX channel disconnected).  Idempotent: subsequent
     * calls with the same @p streamId produce a warning but have no effect.
     *
     * @param streamId  VITA‑49 stream identifier (must be non‑zero for a
     *                  valid stream; zero is silently ignored).
     *
     * @pre  The caller must ensure that the corresponding AudioPlc instance
     *       is no longer referenced by any other object.
     *
     * @post The AudioPlc for @p streamId is destroyed and removed from the
     *       internal map.
     */
    void removeAudioPlc(uint64_t streamId) noexcept
    {
        // --- Input validation ---
        // Zero is almost certainly invalid for VITA-49; guard against it.
        if (streamId == 0) {
            qCWarning(panadapterStreamLog)
                << "removeAudioPlc called with streamId=0 (ignored)";
            return;
        }

        auto it = m_audioPlc.find(streamId);
        if (it == m_audioPlc.end()) {
            qCWarning(panadapterStreamLog)
                << "removeAudioPlc: streamId" << streamId
                << "not found (possibly already removed)";
            return;
        }

        // Log removal (debug level to avoid noise in release).
        qCDebug(panadapterStreamLog)
            << "Removing audio PLC for streamId" << streamId;

        // Release the AudioPlc resource.  The unique_ptr destructor
        // will delete the object.  Then erase the map entry.
        it->reset();
        m_audioPlc.erase(it);
    }

private:
    // ---- Internal state ------------------------------------------------
    //
    // Keyed by VITA‑49 stream identifier (uint64_t).  Using QHash provides
    // average O(1) lookup and removal; the number of simultaneous streams
    // is small (< 20), so memory overhead is negligible.
    //
    // The value is a std::unique_ptr to automatically manage the lifetime
    // of AudioPlc objects.  Explicit delete is never required.
    //
    // Note: depending on the definition of AudioPlc, you may need a full
    // definition visible before the first unique_ptr destruction (i.e. in
    // the .cpp that includes this header and calls the destructor).  If
    // AudioPlc is forward‑declared only, the destructor must be defined
    // in a .cpp file that includes the AudioPlc header.  For simplicity,
    // the current code keeps the header self‑contained; adjust the
    // destructor placement accordingly.
    QHash<uint64_t, std::unique_ptr<AudioPlc>> m_audioPlc;
};

// ---------------------------------------------------------------------------
// Convenience type alias (optional, but aids readability)
// ---------------------------------------------------------------------------
using PanadapterStreamId = uint64_t;