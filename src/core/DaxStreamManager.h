#pragma once

#include <QObject>
#include <QMap>

namespace AetherSDR {

class RadioModel;
class PanadapterStream;

// Manages creation and teardown of DAX RX audio streams from the radio.
// Sends "stream create type=dax_rx dax_channel=N" commands for channels 1-4,
// registers the resulting stream IDs with PanadapterStream for routing,
// assigns the first available slice to each DAX channel,
// and re-emits decoded DAX audio per channel.
class DaxStreamManager : public QObject {
    Q_OBJECT

public:
    static constexpr int MAX_DAX_CHANNELS = 4;

    explicit DaxStreamManager(RadioModel* model, PanadapterStream* panStream,
                              QObject* parent = nullptr);

    // Request all 4 DAX RX streams from the radio.
    void requestDaxStreams();

    // Release (remove) all DAX streams.
    void releaseDaxStreams();

    // Returns the stream ID for a given DAX channel (1-4), or 0 if not active.
    quint32 streamId(int channel) const;

signals:
    // Emitted when decoded audio arrives for a DAX channel (1-4).
    // pcm format: int16 stereo, 24 kHz, little-endian (same as audioDataReady).
    void daxAudioReady(int channel, const QByteArray& pcm);

    // Emitted when a DAX stream is successfully created or removed.
    void streamCreated(int channel, quint32 streamId);
    void streamRemoved(int channel);

private:
    RadioModel*       m_model;
    PanadapterStream* m_panStream;

    // DAX channel (1-4) → VITA-49 stream ID
    QMap<int, quint32> m_streamIds;
};

} // namespace AetherSDR
