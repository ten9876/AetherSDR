#pragma once

#include <QObject>
#include <QString>

#ifdef HAVE_SERIALPORT
#include <QSerialPort>
#include <QTimer>
#include <QByteArray>
#endif

namespace AetherSDR {

// Serial connection to an LDG autotuner via a USB-to-TTL adapter.
//
// Protocol uses 38400 baud, 8N1, no flow control.
// Commands are prefixed with a wake byte (0x20), followed by mode byte
// and command byte. Meter telemetry is 6 payload bytes + ";;" delimiter.
//
// Reference: NeoLDG project (github.com/RiskAndReward1337/NeoLDG)
//
// Gated by HAVE_SERIALPORT — compiles to a no-op stub without Qt6::SerialPort.

class LdgTunerConnection : public QObject {
    Q_OBJECT

public:
    explicit LdgTunerConnection(QObject* parent = nullptr);
    ~LdgTunerConnection() override;

    bool isConnected() const;
    QString portName() const;

    void connectToTuner(const QString& portName);
    void disconnect();

    // Commands — all prefixed with wake byte + control mode entry
    void sendFullTune();       // 'F' — full tune
    void sendMemoryTune();     // 'T' — memory tune
    void sendBypass();         // 'P' — bypass
    void sendAutoTuneMode();   // 'C' — return to auto tune
    void sendAntennaToggle();  // 'A' — toggle antenna
    void sendStreamingMode();  // 'S' — resume meter streaming

signals:
    void connected();
    void disconnected();
    void connectionFailed(const QString& errorString);

    // Emitted when a tune operation completes.
    // success: true if tuner reports success, false on fail/error.
    void tuneFinished(bool success);

    // Emitted when meter data is parsed from streaming frames.
    void meterDataReceived(float fwdPower, float refPower, float swr);

    // Emitted on serial errors (including unexpected disconnection).
    void errorOccurred(const QString& msg);

#ifdef HAVE_SERIALPORT
private slots:
    void onReadyRead();
    void onError(QSerialPort::SerialPortError error);
    void onTuneTimeout();

private:
    void sendCommand(char cmdByte);
    void processBuffer();
    void parseMeterFrame(const QByteArray& frame);

    QSerialPort m_port;
    QByteArray  m_readBuf;
    QTimer      m_tuneTimer;         // safety timeout for tune operations
    bool        m_connected{false};
    bool        m_tuning{false};     // waiting for tune response

    static constexpr char WAKE_BYTE = 0x20;      // space
    static constexpr int  BAUD_RATE = 38400;
    static constexpr int  TUNE_TIMEOUT_MS = 30000; // 30s safety timeout
    static constexpr int  WAKE_DELAY_MS = 50;       // delay after wake byte
#endif
};

} // namespace AetherSDR
