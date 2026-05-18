cpp
// src/PanadapterStream.cpp - Improved production-quality implementation

// -----------------------------------------------------------------------------
//  onStreamRemoved
// -----------------------------------------------------------------------------
//  Called when the underlying audio stream is torn down (e.g., slice closed,
//  DAX channel disconnected).  Removes the corresponding packet-loss
//  concealment state from the map to prevent a slow, monotonic leak across
//  reconnect cycles.
//
//  @param streamId  VITA-49 stream identifier of the stream being removed
// -----------------------------------------------------------------------------
void PanadapterStream::onStreamRemoved(const quint64 streamId)
{
    // -------------------------------------------------------------------------
    //  Input validation:  A zero streamId is not legal in the VITA-49 context.
    //  Log a warning and return early to avoid adding unexpected entries.
    // -------------------------------------------------------------------------
    if (streamId == 0) {
        qWarning() << Q_FUNC_INFO << "Ignored removal request for invalid streamId (0)";
        return;
    }

    // -------------------------------------------------------------------------
    //  Remove the PLC state from the map.  If the key does not exist the
    //  operation is a no-op, but we log a warning to catch inconsistent
    //  teardown ordering during development.
    // -------------------------------------------------------------------------
    if (m_audioPlc.contains(streamId)) {
        m_audioPlc.remove(streamId);
        qInfo() << "PanadapterStream: removed PLC state for stream" << streamId
                << "map size now" << m_audioPlc.size();
    } else {
        qWarning() << Q_FUNC_INFO << "Attempted to remove PLC state for unknown stream"
                   << streamId << "- stream was already cleaned up or never registered";
    }
}