#pragma once

#include <QObject>

class QSocketNotifier;
class QTimer;

namespace AetherSDR {

class RadioModel;
class RigctlProtocol;

// Virtual serial port implementing the Hamlib rigctld protocol.
//
// On Linux/macOS: creates a pseudo-terminal pair using openpty().
// The slave device path (e.g. /dev/ttys004) can be pointed to by
// CAT-capable software. A symlink at /tmp/AetherSDR-CAT is also
// created for convenience.
//
// On Windows: creates a named pipe (e.g. \\.\pipe\AetherSDR-CAT-A)
// that CAT-capable software can open as a serial-like device.
class RigctlPty : public QObject {
    Q_OBJECT

public:
    explicit RigctlPty(RadioModel* model, QObject* parent = nullptr);
    ~RigctlPty() override;

    bool start();
    void stop();

#ifdef _WIN32
    bool isRunning() const { return m_pipeHandle != nullptr; }
#else
    bool isRunning() const { return m_masterFd >= 0; }
#endif
    QString slavePath() const { return m_slavePath; }
    QString symlinkPath() const { return m_symlinkPath; }

    // Which slice index this PTY's protocol will control.
    void setSliceIndex(int idx) { m_sliceIndex = idx; }
    int  sliceIndex() const     { return m_sliceIndex; }

    // Override the default symlink/pipe path.
    // Unix:    e.g. /tmp/AetherSDR-CAT-B
    // Windows: e.g. \\.\pipe\AetherSDR-CAT-B
    void setSymlinkPath(const QString& path) { m_symlinkPath = path; }

signals:
    void started(const QString& path);
    void stopped();

private slots:
    void onDataReady();

private:
    RadioModel*      m_model;
    RigctlProtocol*  m_protocol{nullptr};
    int              m_sliceIndex{0};
    QString          m_slavePath;
    QByteArray       m_buffer;

#ifdef _WIN32
    // Windows named pipe
    void*            m_pipeHandle{nullptr};   // HANDLE
    void*            m_overlapped{nullptr};    // OVERLAPPED*
    QTimer*          m_pollTimer{nullptr};
    char             m_readBuf[4096]{};
    bool             m_pendingRead{false};
    bool             m_clientConnected{false};
    QString          m_symlinkPath{"\\\\.\\pipe\\AetherSDR-CAT"};

    bool createPipe();
    void startAsyncRead();
    void checkOverlappedResult();
    void waitForClient();
#else
    // Unix PTY
    int              m_masterFd{-1};
    int              m_slaveFd{-1};
    QString          m_symlinkPath{"/tmp/AetherSDR-CAT"};
    QSocketNotifier* m_notifier{nullptr};
#endif
};

} // namespace AetherSDR
