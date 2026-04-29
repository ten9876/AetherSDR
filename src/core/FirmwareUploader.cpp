#include "FirmwareUploader.h"
#include "LogManager.h"
#include "../models/RadioModel.h"
#include "../core/RadioConnection.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QTimer>

namespace AetherSDR {

FirmwareUploader::FirmwareUploader(RadioModel* model, QObject* parent)
    : QObject(parent), m_model(model)
{
    connect(&m_socket, &QTcpSocket::connected, this, &FirmwareUploader::onConnected);
    connect(&m_socket, &QTcpSocket::bytesWritten, this, &FirmwareUploader::onBytesWritten);
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this] { onError(); });

    // State transitions are critical breadcrumbs if the upload misbehaves —
    // capture them at info level so they're visible without the firmware
    // category having to be at debug level.
    connect(&m_socket, &QTcpSocket::stateChanged, this,
            [](QAbstractSocket::SocketState s) {
        qCInfo(lcFirmware) << "FirmwareUploader: socket state ->" << s;
    });
    connect(&m_socket, &QTcpSocket::disconnected, this, []() {
        qCInfo(lcFirmware) << "FirmwareUploader: socket disconnected";
    });
}

void FirmwareUploader::upload(const QString& filePath)
{
    if (m_uploading) {
        emit finished(false, "Upload already in progress");
        return;
    }

    // Read the file
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit finished(false, "Cannot open file: " + file.errorString());
        return;
    }

    m_fileData = file.readAll();
    file.close();

    if (m_fileData.isEmpty()) {
        emit finished(false, "File is empty");
        return;
    }

    // Sanity check size (< 500MB)
    if (m_fileData.size() > 500 * 1024 * 1024) {
        emit finished(false, "File too large (> 500MB)");
        return;
    }

    m_fileName = QFileInfo(filePath).fileName();
    m_bytesSent = 0;
    m_uploadPort = -1;
    m_uploading = true;
    m_cancelled = false;

    // Pre-flight diagnostic dump — captures everything we'd want to know
    // post-mortem if the flash misbehaves. This is the kind of state that's
    // trivially knowable now and impossible to reconstruct after the fact.
    QCryptographicHash hash(QCryptographicHash::Md5);
    hash.addData(m_fileData);
    const QString fileMd5 = hash.result().toHex().toLower();
    const QByteArray header = m_fileData.left(8);

    qCInfo(lcFirmware) << "FirmwareUploader: ===== PRE-UPLOAD DIAGNOSTIC =====";
    qCInfo(lcFirmware) << "FirmwareUploader: file       =" << filePath;
    qCInfo(lcFirmware) << "FirmwareUploader: filename   =" << m_fileName;
    qCInfo(lcFirmware) << "FirmwareUploader: size       =" << m_fileData.size()
                        << "bytes (" << (m_fileData.size() / (1024.0*1024.0)) << "MB)";
    qCInfo(lcFirmware) << "FirmwareUploader: md5        =" << fileMd5;
    qCInfo(lcFirmware) << "FirmwareUploader: header(8B) =" << header.toHex();
    qCInfo(lcFirmware) << "FirmwareUploader: header str =" << QString::fromLatin1(header);
    qCInfo(lcFirmware) << "FirmwareUploader: radio      =" << m_model->model()
                        << "serial" << m_model->serial();
    qCInfo(lcFirmware) << "FirmwareUploader: radio addr =" << m_model->radioAddress();
    qCInfo(lcFirmware) << "FirmwareUploader: cur fw     =" << m_model->softwareVersion();
    qCInfo(lcFirmware) << "FirmwareUploader: ================================";

    emit progressChanged(0, "Preparing upload...");

    // Step 1: Send filename to radio
    qCInfo(lcFirmware) << "FirmwareUploader: TX 'file filename" << m_fileName << "'";
    m_model->sendCommand("file filename " + m_fileName);

    // Step 2: Request upload — radio responds with port number
    emit progressChanged(0, "Requesting upload port...");

    const QString uploadCmd = QString("file upload %1 update").arg(m_fileData.size());
    qCInfo(lcFirmware) << "FirmwareUploader: TX '" << uploadCmd << "'";
    m_model->sendCmdPublic(uploadCmd,
        [this](int code, const QString& body) {
            qCInfo(lcFirmware) << "FirmwareUploader: RX upload response code=0x"
                << QString::number(code, 16) << "body='" << body << "'";
            onUploadPortReceived(code, body);
        });
}

void FirmwareUploader::cancel()
{
    if (!m_uploading) return;
    m_cancelled = true;
    m_socket.abort();
    m_uploading = false;
    m_fileData.clear();
    emit finished(false, "Upload cancelled");
}

void FirmwareUploader::onUploadPortReceived(int code, const QString& body)
{
    if (m_cancelled) return;

    if (code != 0) {
        m_uploading = false;
        m_fileData.clear();
        emit finished(false, QString("Radio rejected upload (error 0x%1)")
                          .arg(code, 0, 16));
        return;
    }

    // Parse port number from response body
    bool ok = false;
    int port = body.trimmed().toInt(&ok);
    if (!ok || port <= 0)
        port = DEFAULT_PORT;

    m_uploadPort = port;

    qCDebug(lcFirmware) << "FirmwareUploader: connecting to upload port" << m_uploadPort;
    emit progressChanged(0, QString("Connecting to port %1...").arg(m_uploadPort));

    // Step 3: Connect TCP to the upload port
    // Small delay to let the radio set up the server
    QTimer::singleShot(200, this, [this] {
        if (m_cancelled) return;
        m_socket.connectToHost(m_model->radioAddress(), m_uploadPort);

        // Timeout after 10 seconds
        QTimer::singleShot(10000, this, [this] {
            if (m_uploading && m_socket.state() != QAbstractSocket::ConnectedState) {
                // Try fallback port
                if (m_uploadPort != FALLBACK_PORT) {
                    m_uploadPort = FALLBACK_PORT;
                    qCDebug(lcFirmware) << "FirmwareUploader: trying fallback port" << FALLBACK_PORT;
                    emit progressChanged(0, QString("Trying fallback port %1...").arg(FALLBACK_PORT));
                    m_socket.abort();
                    m_socket.connectToHost(m_model->radioAddress(), m_uploadPort);
                } else {
                    m_uploading = false;
                    m_fileData.clear();
                    emit finished(false, "Cannot connect to upload port");
                }
            }
        });
    });
}

void FirmwareUploader::onConnected()
{
    if (m_cancelled) return;

    qCDebug(lcFirmware) << "FirmwareUploader: connected, sending" << m_fileData.size() << "bytes";
    emit progressChanged(0, "Uploading firmware...");

    // Step 4: Send the file data in chunks
    const qint64 toSend = qMin(static_cast<qint64>(CHUNK_SIZE),
                                m_fileData.size() - m_bytesSent);
    m_socket.write(m_fileData.constData() + m_bytesSent, toSend);
}

void FirmwareUploader::onBytesWritten(qint64 bytes)
{
    if (m_cancelled) return;

    m_bytesSent += bytes;
    const int percent = static_cast<int>(m_bytesSent * 100 / m_fileData.size());
    emit progressChanged(percent, QString("Uploading... %1 / %2 KB")
                             .arg(m_bytesSent / 1024)
                             .arg(m_fileData.size() / 1024));

    if (m_bytesSent >= m_fileData.size()) {
        // Upload complete
        qCDebug(lcFirmware) << "FirmwareUploader: upload complete";
        m_socket.flush();
        m_socket.disconnectFromHost();
        m_uploading = false;
        m_fileData.clear();
        emit progressChanged(100, "Upload complete — radio is rebooting...");
        emit finished(true, "Firmware uploaded successfully. The radio will reboot.");
        return;
    }

    // Send next chunk
    const qint64 toSend = qMin(static_cast<qint64>(CHUNK_SIZE),
                                m_fileData.size() - m_bytesSent);
    m_socket.write(m_fileData.constData() + m_bytesSent, toSend);
}

void FirmwareUploader::onError()
{
    if (m_cancelled) return;

    qCWarning(lcFirmware) << "FirmwareUploader: SOCKET ERROR err="
        << m_socket.error() << "msg='" << m_socket.errorString() << "'"
        << "bytesSent=" << m_bytesSent << "/" << m_fileData.size()
        << "port=" << m_uploadPort;

    // If we haven't sent anything and this is the first port attempt, try fallback
    if (m_bytesSent == 0 && m_uploadPort != FALLBACK_PORT) {
        m_uploadPort = FALLBACK_PORT;
        qCInfo(lcFirmware) << "FirmwareUploader: error on primary port, trying" << FALLBACK_PORT;
        emit progressChanged(0, QString("Retrying on port %1...").arg(FALLBACK_PORT));
        m_socket.abort();
        m_socket.connectToHost(m_model->radioAddress(), m_uploadPort);
        return;
    }

    m_uploading = false;
    m_fileData.clear();
    emit finished(false, "Upload failed: " + m_socket.errorString());
}

} // namespace AetherSDR
