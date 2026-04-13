#include "QsoRecorder.h"
#include "AppSettings.h"
#include "LogManager.h"
#include "../models/SliceModel.h"

#include <QDir>
#include <QStandardPaths>
#include <QtEndian>

namespace AetherSDR {

QsoRecorder::QsoRecorder(QObject* parent)
    : QObject(parent)
{
    // Default recording directory: ~/Documents/AetherSDR/Recordings
    m_recordingDir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                     + "/AetherSDR/Recordings";

    // Restore settings
    auto& s = AppSettings::instance();
    m_recordingDir = s.value("QsoRecordingDir", m_recordingDir).toString();
    m_idleTimeoutSecs = s.value("QsoRecordingIdleTimeout", 120).toInt();
    m_autoRecord = s.value("QsoRecordingAutoRecord", false).toBool();
    m_includeDate = s.value("QsoRecordingIncludeDate", true).toBool();
    m_includeTime = s.value("QsoRecordingIncludeTime", true).toBool();
    m_includeFreq = s.value("QsoRecordingIncludeFreq", true).toBool();
    m_includeMode = s.value("QsoRecordingIncludeMode", true).toBool();

    // Idle timer — fires when no TX activity for m_idleTimeoutSecs
    m_idleTimer = new QTimer(this);
    m_idleTimer->setSingleShot(true);
    connect(m_idleTimer, &QTimer::timeout, this, [this]() {
        if (m_recording) {
            qCInfo(lcAudio) << "QsoRecorder: idle timeout, stopping recording";
            stopRecording();
        }
    });
}

QsoRecorder::~QsoRecorder()
{
    if (m_recording)
        finalizeFile();
}

void QsoRecorder::setRecordingDir(const QString& path)
{
    m_recordingDir = path;
    AppSettings::instance().setValue("QsoRecordingDir", path);
}

void QsoRecorder::setIdleTimeoutSecs(int secs)
{
    m_idleTimeoutSecs = qBound(10, secs, 3600);
    AppSettings::instance().setValue("QsoRecordingIdleTimeout", m_idleTimeoutSecs);
}

void QsoRecorder::setAutoRecordEnabled(bool on)
{
    m_autoRecord = on;
    AppSettings::instance().setValue("QsoRecordingAutoRecord", on);
}

void QsoRecorder::setCallsign(const QString& call)
{
    m_callsign = call.trimmed().toUpper();
}

void QsoRecorder::setSlice(SliceModel* slice)
{
    m_slice = slice;
}

int QsoRecorder::recordingDurationSecs() const
{
    if (!m_recording) return 0;
    return static_cast<int>(m_startTime.secsTo(QDateTime::currentDateTimeUtc()));
}

// ── Manual control ──────────────────────────────────────────────────────────

void QsoRecorder::startRecording()
{
    if (m_recording) return;
    startFile();
}

void QsoRecorder::stopRecording()
{
    if (!m_recording) return;
    m_idleTimer->stop();
    finalizeFile();
}

// ── Audio feeds ─────────────────────────────────────────────────────────────

void QsoRecorder::feedRxAudio(const QByteArray& pcm)
{
    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (!m_recording || !m_file) return;
    m_file->write(pcm);
    m_dataBytes += static_cast<quint32>(pcm.size());
}

void QsoRecorder::feedTxAudio(const QByteArray& pcm)
{
    std::lock_guard<std::mutex> lock(m_writeMutex);
    if (!m_recording || !m_file) return;
    m_file->write(pcm);
    m_dataBytes += static_cast<quint32>(pcm.size());
}

// ── TX state tracking ───────────────────────────────────────────────────────

void QsoRecorder::onMoxChanged(bool mox)
{
    if (mox) {
        // TX started — begin recording if auto-record is on and not already recording
        if (m_autoRecord && !m_recording)
            startFile();

        // Reset idle timer on each TX
        m_idleTimer->stop();
    } else {
        // TX ended — start idle countdown
        if (m_recording)
            m_idleTimer->start(m_idleTimeoutSecs * 1000);
    }
}

// ── File management ─────────────────────────────────────────────────────────

void QsoRecorder::startFile()
{
    // Capture metadata from active slice at recording start
    if (m_slice) {
        m_freqMhz = m_slice->frequency();
        m_mode = m_slice->mode();
    } else {
        m_freqMhz = 0.0;
        m_mode.clear();
    }

    m_startTime = QDateTime::currentDateTimeUtc();
    m_dataBytes = 0;

    // Ensure directory exists
    QDir dir(m_recordingDir);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            emit recordingError("Cannot create recording directory: " + m_recordingDir);
            return;
        }
    }

    QString filePath = m_recordingDir + "/" + buildFilename();

    m_file = new QFile(filePath, this);
    if (!m_file->open(QIODevice::WriteOnly)) {
        emit recordingError("Cannot create recording file: " + m_file->errorString());
        delete m_file;
        m_file = nullptr;
        return;
    }

    writeWavHeader();
    m_recording = true;

    qCInfo(lcAudio) << "QsoRecorder: started recording to" << filePath;
    emit recordingStarted(filePath);
}

void QsoRecorder::finalizeFile()
{
    {
        std::lock_guard<std::mutex> lock(m_writeMutex);
        m_recording = false;
    }

    if (!m_file) return;

    patchWavHeader();
    QString filePath = m_file->fileName();
    int durationSecs = static_cast<int>(m_startTime.secsTo(QDateTime::currentDateTimeUtc()));

    m_file->close();
    m_file->deleteLater();
    m_file = nullptr;

    qCInfo(lcAudio) << "QsoRecorder: stopped recording," << durationSecs << "seconds,"
                     << m_dataBytes << "bytes";
    emit recordingStopped(filePath, durationSecs);
}

QString QsoRecorder::buildFilename() const
{
    QStringList parts;

    if (m_includeDate)
        parts << m_startTime.toString("yyyy-MM-dd");

    if (m_includeTime)
        parts << m_startTime.toString("HHmmss") + "Z";

    if (m_includeFreq && m_freqMhz > 0.0) {
        // Format frequency: e.g. 14.200 MHz → "14.200"
        parts << QString::number(m_freqMhz, 'f', 3) + "MHz";
    }

    if (m_includeMode && !m_mode.isEmpty())
        parts << m_mode;

    if (!m_callsign.isEmpty())
        parts << m_callsign;

    if (parts.isEmpty())
        parts << "QSO";

    return parts.join("_") + ".wav";
}

void QsoRecorder::writeWavHeader()
{
    // Write a placeholder WAV header (44 bytes). The data size fields
    // will be patched in finalizeFile() once we know the total size.
    char header[WAV_HEADER_SIZE] = {};

    const int byteRate = SAMPLE_RATE * NUM_CHANNELS * (BITS_PER_SAMPLE / 8);
    const int blockAlign = NUM_CHANNELS * (BITS_PER_SAMPLE / 8);

    // RIFF header
    memcpy(header + 0, "RIFF", 4);
    // header[4..7] = file size - 8 (patched later)
    memcpy(header + 8, "WAVE", 4);

    // fmt sub-chunk
    memcpy(header + 12, "fmt ", 4);
    qToLittleEndian<quint32>(16, header + 16);               // sub-chunk size
    qToLittleEndian<quint16>(1, header + 20);                 // audio format (1 = PCM)
    qToLittleEndian<quint16>(NUM_CHANNELS, header + 22);      // channels
    qToLittleEndian<quint32>(SAMPLE_RATE, header + 24);       // sample rate
    qToLittleEndian<quint32>(byteRate, header + 28);          // byte rate
    qToLittleEndian<quint16>(blockAlign, header + 32);        // block align
    qToLittleEndian<quint16>(BITS_PER_SAMPLE, header + 34);   // bits per sample

    // data sub-chunk
    memcpy(header + 36, "data", 4);
    // header[40..43] = data size (patched later)

    m_file->write(header, WAV_HEADER_SIZE);
}

void QsoRecorder::patchWavHeader()
{
    if (!m_file || !m_file->isOpen()) return;

    // Seek back and patch the two size fields in the WAV header
    m_file->seek(4);
    quint32 riffSize = m_dataBytes + WAV_HEADER_SIZE - 8;
    char buf[4];
    qToLittleEndian<quint32>(riffSize, buf);
    m_file->write(buf, 4);

    m_file->seek(40);
    qToLittleEndian<quint32>(m_dataBytes, buf);
    m_file->write(buf, 4);
}

} // namespace AetherSDR
