#include "RigctlPty.h"
#include "LogManager.h"
#include "RigctlProtocol.h"
#include "models/RadioModel.h"

#include <QSocketNotifier>
#include <QTimer>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

#ifdef __APPLE__
#include <util.h>       // openpty() on macOS
#else
#include <pty.h>        // openpty() on Linux
#endif
#endif // !_WIN32

namespace AetherSDR {

RigctlPty::RigctlPty(RadioModel* model, QObject* parent)
    : QObject(parent)
    , m_model(model)
{}

RigctlPty::~RigctlPty()
{
    stop();
}

// ─── Windows named pipe implementation ───────────────────────────────

#ifdef _WIN32

bool RigctlPty::createPipe()
{
    m_pipeHandle = CreateNamedPipeA(
        m_symlinkPath.toLocal8Bit().constData(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1,      // single instance
        4096,   // out buffer
        4096,   // in buffer
        0,      // default timeout
        nullptr // default security
    );

    if (m_pipeHandle == INVALID_HANDLE_VALUE) {
        m_pipeHandle = nullptr;
        return false;
    }
    return true;
}

bool RigctlPty::start()
{
    if (m_pipeHandle)
        return true;  // already running

    if (!createPipe()) {
        qCWarning(lcCat) << "RigctlPty: CreateNamedPipe() failed for"
                         << m_symlinkPath;
        return false;
    }

    m_slavePath = m_symlinkPath;

    // Allocate OVERLAPPED structure
    auto* ov = new OVERLAPPED{};
    ov->hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    m_overlapped = ov;

    // Set up protocol handler
    m_protocol = new RigctlProtocol(m_model);
    m_protocol->setSliceIndex(m_sliceIndex);

    // Poll timer for overlapped I/O completion (10 ms)
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(10);
    connect(m_pollTimer, &QTimer::timeout, this, &RigctlPty::onDataReady);
    m_pollTimer->start();

    // Start waiting for a client to connect
    m_clientConnected = false;
    m_pendingRead = false;
    waitForClient();

    qCInfo(lcCat) << "RigctlPty: named pipe started on" << m_symlinkPath;
    emit started(m_symlinkPath);
    return true;
}

void RigctlPty::waitForClient()
{
    auto* ov = static_cast<OVERLAPPED*>(m_overlapped);
    ResetEvent(ov->hEvent);
    BOOL ok = ConnectNamedPipe(m_pipeHandle, ov);
    if (ok) {
        m_clientConnected = true;
        startAsyncRead();
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_PIPE_CONNECTED) {
            // Client already connected before we called ConnectNamedPipe
            m_clientConnected = true;
            startAsyncRead();
        } else if (err == ERROR_IO_PENDING) {
            // Waiting for connection — poll timer will check
            m_clientConnected = false;
        } else {
            qCWarning(lcCat) << "RigctlPty: ConnectNamedPipe failed, error"
                             << err;
        }
    }
}

void RigctlPty::startAsyncRead()
{
    if (m_pendingRead || !m_pipeHandle)
        return;

    auto* ov = static_cast<OVERLAPPED*>(m_overlapped);
    ResetEvent(ov->hEvent);
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(m_pipeHandle, m_readBuf, sizeof(m_readBuf),
                       &bytesRead, ov);
    if (ok && bytesRead > 0) {
        m_buffer.append(m_readBuf, static_cast<int>(bytesRead));
        m_pendingRead = false;
    } else {
        DWORD err = GetLastError();
        if (err == ERROR_IO_PENDING) {
            m_pendingRead = true;
        } else if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
            // Client disconnected — reset pipe for next client
            DisconnectNamedPipe(m_pipeHandle);
            m_clientConnected = false;
            m_pendingRead = false;
            waitForClient();
        }
    }
}

void RigctlPty::checkOverlappedResult()
{
    auto* ov = static_cast<OVERLAPPED*>(m_overlapped);
    DWORD bytesTransferred = 0;
    BOOL ok = GetOverlappedResult(m_pipeHandle, ov, &bytesTransferred, FALSE);

    if (!ok) {
        DWORD err = GetLastError();
        if (err == ERROR_IO_INCOMPLETE) {
            return;  // still pending
        }
        if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA) {
            DisconnectNamedPipe(m_pipeHandle);
            m_clientConnected = false;
            m_pendingRead = false;
            waitForClient();
            return;
        }
        return;
    }

    if (!m_clientConnected) {
        // ConnectNamedPipe completed — client connected
        m_clientConnected = true;
        m_pendingRead = false;
        startAsyncRead();
        return;
    }

    if (m_pendingRead && bytesTransferred > 0) {
        m_buffer.append(m_readBuf, static_cast<int>(bytesTransferred));
        m_pendingRead = false;
    }
}

void RigctlPty::stop()
{
    if (!m_pipeHandle)
        return;

    delete m_pollTimer;
    m_pollTimer = nullptr;

    delete m_protocol;
    m_protocol = nullptr;

    if (m_clientConnected) {
        FlushFileBuffers(m_pipeHandle);
        DisconnectNamedPipe(m_pipeHandle);
    }

    auto* ov = static_cast<OVERLAPPED*>(m_overlapped);
    if (ov) {
        CancelIo(m_pipeHandle);
        if (ov->hEvent)
            CloseHandle(ov->hEvent);
        delete ov;
        m_overlapped = nullptr;
    }

    CloseHandle(m_pipeHandle);
    m_pipeHandle = nullptr;
    m_clientConnected = false;
    m_pendingRead = false;
    m_buffer.clear();

    qCInfo(lcCat) << "RigctlPty: stopped";
    emit stopped();
}

void RigctlPty::onDataReady()
{
    if (!m_pipeHandle)
        return;

    // Check for overlapped I/O completion
    checkOverlappedResult();

    // If connected but no pending read, start one
    if (m_clientConnected && !m_pendingRead)
        startAsyncRead();

    // Process complete lines from buffer
    while (true) {
        int nlPos = m_buffer.indexOf('\n');
        if (nlPos < 0) {
            nlPos = m_buffer.indexOf('\r');
            if (nlPos < 0) break;
        }

        QString line = QString::fromUtf8(m_buffer.left(nlPos));
        m_buffer.remove(0, nlPos + 1);

        if (line.trimmed().isEmpty())
            continue;

        QString response = m_protocol->handleLine(line);
        if (!response.isEmpty()) {
            QByteArray data = response.toUtf8();
            DWORD written = 0;
            WriteFile(m_pipeHandle, data.constData(),
                      static_cast<DWORD>(data.size()), &written, nullptr);
        }
    }
}

// ─── Unix PTY implementation ─────────────────────────────────────────

#else

bool RigctlPty::start()
{
    if (m_masterFd >= 0)
        return true;  // already running

    char slaveName[256] = {};
    if (openpty(&m_masterFd, &m_slaveFd, slaveName, nullptr, nullptr) != 0) {
        qCWarning(lcCat) << "RigctlPty: openpty() failed";
        return false;
    }

    m_slavePath = QString::fromLocal8Bit(slaveName);

    // Set master FD to non-blocking
    int flags = fcntl(m_masterFd, F_GETFL);
    fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);

    // Configure slave terminal: raw mode, no echo
    struct termios tio;
    if (tcgetattr(m_slaveFd, &tio) == 0) {
        cfmakeraw(&tio);
        tio.c_cc[VMIN] = 1;
        tio.c_cc[VTIME] = 0;
        tcsetattr(m_slaveFd, TCSANOW, &tio);
    }

    // Create symlink for convenience
    ::unlink(m_symlinkPath.toLocal8Bit().constData());
    if (::symlink(slaveName, m_symlinkPath.toLocal8Bit().constData()) != 0) {
        qCWarning(lcCat) << "RigctlPty: symlink failed:" << m_symlinkPath;
    }

    // Set up protocol handler
    m_protocol = new RigctlProtocol(m_model);
    m_protocol->setSliceIndex(m_sliceIndex);

    // Watch for data on the master FD
    m_notifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &RigctlPty::onDataReady);

    qCInfo(lcCat) << "RigctlPty: started on" << m_slavePath << "symlink:" << m_symlinkPath;
    emit started(m_symlinkPath);
    return true;
}

void RigctlPty::stop()
{
    if (m_masterFd < 0)
        return;

    delete m_notifier;
    m_notifier = nullptr;

    delete m_protocol;
    m_protocol = nullptr;

    ::close(m_masterFd);
    ::close(m_slaveFd);
    m_masterFd = -1;
    m_slaveFd = -1;
    m_buffer.clear();

    // Remove symlink
    ::unlink(m_symlinkPath.toLocal8Bit().constData());

    qCInfo(lcCat) << "RigctlPty: stopped";
    emit stopped();
}

void RigctlPty::onDataReady()
{
    char buf[4096];
    ssize_t n = ::read(m_masterFd, buf, sizeof(buf));
    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            // PTY closed by the other end — keep it open for reconnection
        }
        return;
    }

    m_buffer.append(buf, static_cast<int>(n));

    // Process complete lines
    while (true) {
        int nlPos = m_buffer.indexOf('\n');
        if (nlPos < 0) {
            // Also try \r as line terminator (some serial software uses \r only)
            nlPos = m_buffer.indexOf('\r');
            if (nlPos < 0) break;
        }

        QString line = QString::fromUtf8(m_buffer.left(nlPos));
        m_buffer.remove(0, nlPos + 1);

        // Skip empty lines
        if (line.trimmed().isEmpty())
            continue;

        QString response = m_protocol->handleLine(line);
        if (!response.isEmpty()) {
            QByteArray data = response.toUtf8();
            ::write(m_masterFd, data.constData(), data.size());
        }
    }
}

#endif // _WIN32

} // namespace AetherSDR
