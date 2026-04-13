#pragma once
#ifdef HAVE_WEBSOCKETS

#include <QObject>
#include <QList>
#include <memory>

class QWebSocketServer;
class QWebSocket;
class QTimer;

namespace AetherSDR {

class RadioModel;
class AudioEngine;
class SliceModel;
class TciProtocol;
class Resampler;

// TCI WebSocket server — exposes radio state and audio over the TCI protocol.
// Phase 1: text commands (VFO, mode, filter, TX, RIT/XIT, CW, spots)
// Phase 2: binary RX/TX audio streaming
class TciServer : public QObject {
    Q_OBJECT

public:
    explicit TciServer(RadioModel* model, QObject* parent = nullptr);
    ~TciServer() override;

    bool start(quint16 port = 50001);
    void stop();

    bool isRunning() const;
    quint16 port() const;
    int clientCount() const { return m_clients.size(); }

    void setAudioEngine(AudioEngine* audio) { m_audio = audio; }

    // Wire slice signals for state change broadcasts
    void wireSlice(int trx, SliceModel* slice);
    void wireSpotModel();

public slots:
    // RX audio from main audio pipeline (float32 stereo, 24 kHz)
    void onRxAudioReady(const QByteArray& pcm);
    // RX audio from DAX pipeline (float32 stereo, 24 kHz)
    void onDaxAudioReady(int channel, const QByteArray& pcm);
    // IQ data from DAX IQ stream (big-endian float32 I/Q pairs)
    void onIqDataReady(int channel, const QByteArray& rawPayload, int sampleRate);

signals:
    void clientCountChanged(int count);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void onTextMessage(const QString& msg);
    void onBinaryMessage(const QByteArray& data);
    void broadcastStatus();

private:
    void sendInitBurst(QWebSocket* client);
    void broadcast(const QString& msg);
    void broadcastBinary(const QByteArray& data);
    void startTxChrono(QWebSocket* client, int trx);
    void stopTxChrono();

    // Build a TCI binary audio frame (64-byte header + float32 samples)
    static QByteArray buildAudioFrame(int receiver, int type,
                                      int sampleRate, int channels,
                                      const float* samples, int sampleCount);

    struct ClientState {
        QWebSocket*  socket{nullptr};
        TciProtocol* protocol{nullptr};
        bool         audioEnabled{false};   // client sent AUDIO_START
        int          audioSampleRate{48000}; // requested output rate (48kHz for WSJT-X compat)
        int          audioChannels{2};       // 1=mono, 2=stereo
        int          audioFormat{3};         // 0=int16, 3=float32
        Resampler*   resampler{nullptr};    // null if rate == 24000 (native)
        bool         rxSensorsEnabled{false};
        bool         txSensorsEnabled{false};
        bool         iqEnabled{false};       // client sent IQ_START
        int          iqChannel{0};           // TCI TRX → DAX IQ channel (0-based)
    };

    RadioModel*       m_model;
    AudioEngine*      m_audio{nullptr};
    QWebSocketServer* m_server{nullptr};
    QList<ClientState> m_clients;
    QTimer*           m_meterTimer{nullptr};  // 200ms status broadcast
    QTimer*           m_txChronoTimer{nullptr}; // TX_CHRONO frame cadence
    QWebSocket*       m_txChronoClient{nullptr};
    int               m_txChronoTrx{0};
    std::unique_ptr<Resampler> m_txResampler; // 48kHz→24kHz TX downsampler
    bool              m_lastTx{false};
    float             m_cachedSLevel[8]{-130,-130,-130,-130,-130,-130,-130,-130};
    float             m_cachedFwdPower{0};
    float             m_cachedSwr{1.0f};
    float             m_cachedMicLevel{-50.0f};
};

} // namespace AetherSDR

#endif // HAVE_WEBSOCKETS
