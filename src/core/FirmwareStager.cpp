#include "FirmwareStager.h"
#include "LogManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

namespace AetherSDR {

FirmwareStager::FirmwareStager(QObject* parent)
    : QObject(parent)
{
}

QString FirmwareStager::stagingDir()
{
    const QString dir = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation) + "/firmware";
    QDir().mkpath(dir);
    return dir;
}

QString FirmwareStager::modelToFamily(const QString& model)
{
    // Platform codenames (from FlexLib ModelInfo.cs):
    //   Microburst  = FLEX-6300, 6500, 6700, 6700R       → FLEX-6x00 firmware
    //   DeepEddy    = FLEX-6400, 6400M, 6600, 6600M      → FLEX-6x00 firmware
    //   BigBend     = FLEX-8400, 8400M, 8600, 8600M,     → FLEX-6x00 firmware
    //                 AU-510, AU-510M, AU-520, AU-520M
    //   DragonFire  = FLEX-9600 (government only)         → FLEX-9600 firmware
    //
    // ALL consumer radios use FLEX-6x00 firmware.
    if (model.contains("9600"))
        return "9600";
    return "6x00";
}

bool FirmwareStager::versionUsesMsi(const QString& version)
{
    // FlexRadio switched from InnoSetup (.exe) to WiX MSI in SmartSDR v4.2.
    const auto parts = version.split('.');
    if (parts.size() < 2) return false;
    bool ok1 = false, ok2 = false;
    const int major = parts[0].toInt(&ok1);
    const int minor = parts[1].toInt(&ok2);
    if (!ok1 || !ok2) return false;
    return (major > 4) || (major == 4 && minor >= 2);
}

QString FirmwareStager::findExtractionTool()
{
    // 7z handles both MSI (OLE Compound File) and the LZX-compressed CABs
    // inside, so we use it as a single tool. Try the common binary names.
    static const QStringList candidates = {
        "7z", "7zz", "7za",
        "/usr/local/bin/7z",       // macOS Intel Homebrew
        "/opt/homebrew/bin/7z",    // macOS Apple Silicon Homebrew
        "/opt/homebrew/bin/7zz",
    };
    for (const QString& cand : candidates) {
        // Absolute paths: check existence directly.
        if (cand.startsWith('/')) {
            if (QFileInfo::exists(cand) && QFileInfo(cand).isExecutable())
                return cand;
            continue;
        }
        // Otherwise probe via QProcess (catches PATH lookups portably).
        QProcess p;
        p.setProgram(cand);
        p.setArguments({"--help"});
        p.start();
        if (p.waitForStarted(2000)) {
            p.waitForFinished(3000);
            return cand;
        }
    }
    return QString();
}

// ─── Step 1: Check for update ────────────────────────────────────────────────

void FirmwareStager::checkForUpdate(const QString& currentVersion)
{
    emit stageProgress(0, "Checking for updates...");

    auto* reply = m_nam.get(QNetworkRequest(QUrl(SOFTWARE_PAGE)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, currentVersion]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit updateCheckFailed("Cannot reach FlexRadio: " + reply->errorString());
            return;
        }

        const QString html = QString::fromUtf8(reply->readAll());

        // Find latest version from the software page
        // Pattern: SmartSDR v4.1.5 or smartsdr-v4-1-5
        QRegularExpression re(R"(SmartSDR[- _]v?(\d+\.\d+\.\d+))",
                              QRegularExpression::CaseInsensitiveOption);
        QString latest;
        auto it = re.globalMatch(html);
        while (it.hasNext()) {
            auto m = it.next();
            const QString v = m.captured(1);
            if (latest.isEmpty() || v > latest)
                latest = v;
        }

        if (latest.isEmpty()) {
            emit updateCheckFailed("Could not determine latest version from FlexRadio website.");
            return;
        }

        // Compare major.minor.patch only (ignore build number like .39794)
        auto normalize = [](const QString& v) {
            const auto parts = v.split('.');
            if (parts.size() >= 3)
                return QString("%1.%2.%3").arg(parts[0], parts[1], parts[2]);
            return v;
        };
        const QString normLatest  = normalize(latest);
        const QString normCurrent = normalize(currentVersion);
        const bool updateAvailable = (normLatest > normCurrent);
        emit updateCheckComplete(latest, updateAvailable);
    });
}

// ─── Step 2a: Stage from a user-selected local file ─────────────────────────

void FirmwareStager::stageFromLocalFile(const QString& installerPath,
                                         const QString& modelFamily)
{
    m_modelFamily   = modelFamily;
    m_cancelled     = false;
    m_stagedPath.clear();
    m_stagedVersion.clear();
    m_installerPath = installerPath;
    m_expectedMd5.clear();   // user's responsibility — we don't have an authoritative source

    QFileInfo fi(installerPath);
    if (!fi.exists() || !fi.isReadable()) {
        emit stageFailed("Cannot read installer: " + installerPath);
        return;
    }

    // Parse version from filename so the staged .ssdr ends up named correctly.
    // Patterns we support:
    //   SmartSDR_v4.2.18_x64.msi
    //   SmartSDR_v4.1.5_Installer.exe
    //   FLEX-6x00_v4.2.18.41174.ssdr      (already-extracted firmware)
    QRegularExpression verRe(R"(v(\d+\.\d+\.\d+(?:\.\d+)?))",
                              QRegularExpression::CaseInsensitiveOption);
    auto m = verRe.match(fi.fileName());
    m_targetVersion = m.hasMatch() ? m.captured(1) : QStringLiteral("unknown");

    // If the user picked a pre-extracted .ssdr, no extraction needed —
    // copy it into the staging dir under our canonical name and we're done.
    if (fi.suffix().compare("ssdr", Qt::CaseInsensitive) == 0) {
        emit stageProgress(20, "Validating .ssdr file...");
        QFile probe(installerPath);
        if (!probe.open(QIODevice::ReadOnly)) {
            emit stageFailed("Cannot open .ssdr: " + probe.errorString());
            return;
        }
        const QByteArray hdr = probe.read(8);
        probe.close();
        if (hdr != QByteArrayLiteral("Salted__")) {
            emit stageFailed("Selected .ssdr file has invalid header.");
            return;
        }

        emit stageProgress(60, "Staging firmware...");
        const QString outName = "FLEX-" + m_modelFamily + "_v" + m_targetVersion + ".ssdr";
        const QString outPath = stagingDir() + "/" + outName;
        QFile::remove(outPath);
        if (!QFile::copy(installerPath, outPath)) {
            emit stageFailed("Failed to copy .ssdr into staging directory.");
            return;
        }

        QFile h(outPath);
        if (!h.open(QIODevice::ReadOnly)) {
            emit stageFailed("Cannot read staged firmware for hashing.");
            return;
        }
        QCryptographicHash fwHash(QCryptographicHash::Md5);
        fwHash.addData(&h);
        const QString fwMd5 = fwHash.result().toHex().toLower();
        const qint64 fwSize = h.size();
        h.close();

        m_stagedPath = outPath;
        m_stagedVersion = m_targetVersion;
        emit stageProgress(100, QString("Firmware staged and ready ✓\n"
            "%1 (%2 MB)\nMD5: %3").arg(outName).arg(fwSize / (1024*1024)).arg(fwMd5));
        emit stageComplete(outPath, m_targetVersion);
        return;
    }

    // Otherwise it's an installer (.msi or .exe) — go straight to extraction.
    // verifyAndExtract() handles format detection from the file header.
    verifyAndExtract();
}

// ─── Step 2: Download installer ──────────────────────────────────────────────

void FirmwareStager::downloadAndStage(const QString& version, const QString& modelFamily)
{
    m_targetVersion = version;
    m_modelFamily = modelFamily;
    m_cancelled = false;
    m_stagedPath.clear();
    m_stagedVersion.clear();

    // First, fetch the MD5 hash file
    emit stageProgress(0, "Fetching integrity hash...");

    const QString md5Url = QString(MD5_URL_FMT).arg(version);
    auto* md5Reply = m_nam.get(QNetworkRequest(QUrl(md5Url)));
    connect(md5Reply, &QNetworkReply::finished, this, [this, md5Reply, version]() {
        md5Reply->deleteLater();
        if (m_cancelled) return;

        if (md5Reply->error() == QNetworkReply::NoError) {
            // Parse MD5 from the hash file
            // Format: "MD-5:	17b7880f646dbaec9e387c2db35a997c"
            const QString body = QString::fromUtf8(md5Reply->readAll());
            QRegularExpression re(R"(MD-5:\s*([0-9a-fA-F]{32}))");
            auto m = re.match(body);
            if (m.hasMatch()) {
                m_expectedMd5 = m.captured(1).toLower();
                qCDebug(lcFirmware) << "FirmwareStager: expected installer MD5:" << m_expectedMd5;
            } else {
                qCWarning(lcFirmware) << "FirmwareStager: could not parse MD5 from hash file";
            }
        } else {
            qCWarning(lcFirmware) << "FirmwareStager: MD5 hash file not available (non-fatal)";
        }

        // Now download the installer. v4.2+ ships as a WiX MSI; older
        // releases used an InnoSetup .exe.
        const bool useMsi = versionUsesMsi(version);
        const QString installerUrl = useMsi
            ? QString(INSTALLER_URL_FMT_MSI).arg(version)
            : QString(INSTALLER_URL_FMT_EXE).arg(version);
        m_installerPath = stagingDir()
            + (useMsi ? "/SmartSDR_v" + version + "_x64.msi"
                      : "/SmartSDR_v" + version + "_Installer.exe");

        // Check if we already have it cached
        if (QFileInfo::exists(m_installerPath) && !m_expectedMd5.isEmpty()) {
            emit stageProgress(50, "Verifying cached installer...");
            QFile f(m_installerPath);
            if (f.open(QIODevice::ReadOnly)) {
                QCryptographicHash hash(QCryptographicHash::Md5);
                hash.addData(&f);
                const QString md5 = hash.result().toHex().toLower();
                f.close();
                if (md5 == m_expectedMd5) {
                    emit stageProgress(50, "Using cached installer (MD5 verified) ✓");
                    verifyAndExtract();
                    return;
                }
                qCDebug(lcFirmware) << "FirmwareStager: cached installer MD5 mismatch, re-downloading";
            }
        }

        emit stageProgress(1, "Downloading SmartSDR installer...");

        QNetworkRequest req{QUrl{installerUrl}};
        m_downloadReply = m_nam.get(req);
        connect(m_downloadReply, &QNetworkReply::downloadProgress,
                this, &FirmwareStager::onInstallerDownloadProgress);
        connect(m_downloadReply, &QNetworkReply::finished,
                this, &FirmwareStager::onInstallerDownloadFinished);
    });
}

void FirmwareStager::cancel()
{
    m_cancelled = true;
    if (m_downloadReply) {
        m_downloadReply->abort();
        m_downloadReply = nullptr;
    }
    emit stageFailed("Cancelled.");
}

void FirmwareStager::onInstallerDownloadProgress(qint64 received, qint64 total)
{
    if (m_cancelled) return;
    if (total <= 0) {
        emit stageProgress(1, QString("Downloading... %1 MB").arg(received / (1024*1024)));
        return;
    }
    // Download is 0-50% of total progress
    const int pct = static_cast<int>(received * 50 / total);
    emit stageProgress(pct, QString("Downloading... %1 / %2 MB")
                           .arg(received / (1024*1024))
                           .arg(total / (1024*1024)));
}

void FirmwareStager::onInstallerDownloadFinished()
{
    auto* reply = m_downloadReply;
    m_downloadReply = nullptr;
    if (!reply) return;
    reply->deleteLater();

    if (m_cancelled) return;

    if (reply->error() != QNetworkReply::NoError) {
        emit stageFailed("Download failed: " + reply->errorString());
        return;
    }

    // Save to disk
    emit stageProgress(50, "Saving installer...");
    QFile f(m_installerPath);
    if (!f.open(QIODevice::WriteOnly)) {
        emit stageFailed("Cannot write installer: " + f.errorString());
        return;
    }
    f.write(reply->readAll());
    f.close();

    verifyAndExtract();
}

// ─── Steps 3-4: Verify and extract ──────────────────────────────────────────

void FirmwareStager::verifyAndExtract()
{
    if (m_cancelled) return;

    // Step 3: Verify MD5
    if (!m_expectedMd5.isEmpty()) {
        emit stageProgress(55, "Verifying installer integrity...");

        QFile f(m_installerPath);
        if (!f.open(QIODevice::ReadOnly)) {
            emit stageFailed("Cannot read installer for verification.");
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Md5);
        hash.addData(&f);
        const QString md5 = hash.result().toHex().toLower();
        f.close();

        if (md5 != m_expectedMd5) {
            emit stageFailed(QString("Installer integrity check failed.\n"
                "Expected: %1\nGot: %2").arg(m_expectedMd5, md5));
            QFile::remove(m_installerPath);
            return;
        }
        emit stageProgress(65, "Installer integrity verified ✓");
    } else {
        emit stageProgress(65, "MD5 hash not available — skipping verification.");
    }

    // Step 4: Extract .ssdr firmware
    emit stageProgress(70, "Extracting firmware...");

    // Detect installer format from the first 8 bytes:
    //   InnoSetup .exe → "MZ" (PE/COFF) header
    //   WiX MSI        → OLE Compound File: D0 CF 11 E0 A1 B1 1A E1
    QFile probe(m_installerPath);
    if (!probe.open(QIODevice::ReadOnly)) {
        emit stageFailed("Cannot open installer for extraction.");
        return;
    }
    const QByteArray header = probe.read(8);
    probe.close();

    const QByteArray oleMagic("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1", 8);
    const bool isMsi = (header == oleMagic);

    // Compute output path early so both extractors can target the same place.
    const QString targetName = "FLEX-" + m_modelFamily + "_v" + m_targetVersion + ".ssdr";
    const QString outPath = stagingDir() + "/" + targetName;
    QFile::remove(outPath);

    bool ok = false;
    if (isMsi) {
        ok = extractFromMsi(m_installerPath, outPath);
    } else {
        QFile installer(m_installerPath);
        if (!installer.open(QIODevice::ReadOnly)) {
            emit stageFailed("Cannot open installer for extraction.");
            return;
        }
        const QByteArray data = installer.readAll();
        installer.close();
        ok = extractFromInnoSetup(data, outPath);
    }
    if (!ok) {
        // The format-specific extractor already emitted stageFailed.
        return;
    }

    // Common downstream: validate header, MD5, emit completion.
    emit stageProgress(92, "Validating extracted firmware...");
    QFile check(outPath);
    if (!check.open(QIODevice::ReadOnly)) {
        emit stageFailed("Cannot read extracted firmware for validation.");
        return;
    }
    const QByteArray fwHeader = check.read(8);
    check.close();

    if (fwHeader != QByteArrayLiteral("Salted__")) {
        emit stageFailed("Extracted firmware failed validation (bad header).");
        QFile::remove(outPath);
        return;
    }

    QFile hashFile(outPath);
    hashFile.open(QIODevice::ReadOnly);
    QCryptographicHash fwHash(QCryptographicHash::Md5);
    fwHash.addData(&hashFile);
    const QString fwMd5 = fwHash.result().toHex().toLower();
    const qint64 fwSize = hashFile.size();
    hashFile.close();

    m_stagedPath = outPath;
    m_stagedVersion = m_targetVersion;

    emit stageProgress(100, QString("Firmware staged and ready ✓\n"
        "%1 (%2 MB)\nMD5: %3")
        .arg(targetName)
        .arg(fwSize / (1024*1024))
        .arg(fwMd5));
    emit stageComplete(outPath, m_targetVersion);
}

// ─── Format-specific extractors ─────────────────────────────────────────────

bool FirmwareStager::extractFromInnoSetup(const QByteArray& data, const QString& outPath)
{
    // v4.1.x and earlier: InnoSetup self-extracting .exe with two .ssdr blobs
    // stored as OpenSSL-encrypted "Salted__"-prefixed payloads, separated by an
    // InnoSetup LZMA block marker. Boundaries are deduced by scanning.

    const QByteArray saltSig("Salted__");
    QList<qint64> saltOffsets;
    qint64 pos = 0;
    while (true) {
        qint64 idx = data.indexOf(saltSig, pos);
        if (idx < 0) break;
        saltOffsets.append(idx);
        pos = idx + 1;
    }

    if (saltOffsets.size() < 2) {
        emit stageFailed(QString("Expected 2 firmware files in installer, found %1.\n"
            "The installer format may have changed.")
            .arg(saltOffsets.size()));
        return false;
    }

    emit stageProgress(75, QString("Found %1 firmware files.").arg(saltOffsets.size()));

    const qint64 off1 = saltOffsets[0];
    const qint64 off2 = saltOffsets[1];
    const qint64 size1 = off2 - off1;

    // Find end of file 2: next InnoSetup LZMA block header at least 50 MB out.
    const QByteArray zlbSig("\x7a\x6c\x62\x1a\x5d\x00\x00\x80", 8);
    qint64 size2 = -1;
    pos = off2 + 1024;
    while (true) {
        qint64 idx = data.indexOf(zlbSig, pos);
        if (idx < 0) break;
        const qint64 candidate = idx - off2;
        if (candidate > 50 * 1024 * 1024) {
            size2 = candidate;
            break;
        }
        pos = idx + 1;
    }
    if (size2 < 0) {
        size2 = data.size() - off2;
        qCWarning(lcFirmware) << "FirmwareStager: could not find LZMA boundary, using"
                              << size2 << "bytes";
    }

    // File 1 is FLEX-6x00 (consumer multi-platform), File 2 is FLEX-9600.
    const qint64 offset = (m_modelFamily == "9600") ? off2  : off1;
    const qint64 size   = (m_modelFamily == "9600") ? size2 : size1;

    emit stageProgress(85, QString("Extracting %1 MB...").arg(size / (1024*1024)));

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly)) {
        emit stageFailed("Cannot write firmware file: " + out.errorString());
        return false;
    }
    out.write(data.constData() + offset, size);
    out.close();
    return true;
}

bool FirmwareStager::extractFromMsi(const QString& msiPath, const QString& outPath)
{
    // v4.2+: WiX MSI (OLE Compound File) with the .ssdr files stored as
    // LZX-compressed CAB streams (cab1.cab, cab2.cab, ...). Each cab contains
    // exactly one payload file, hashed-named, that decompresses to a raw
    // Salted__-prefixed .ssdr blob.
    //
    // Strategy: shell out to 7-Zip to do the OLE+LZX heavy lifting, scan the
    // decompressed payloads for the Salted__ magic, sort by size, and pick the
    // matching firmware family (consumer 6x00 is several hundred MB; the 9600
    // government build is much smaller).

    const QString sevenZ = findExtractionTool();
    if (sevenZ.isEmpty()) {
        emit stageFailed(
            "Firmware extraction requires 7-Zip but it could not be found.\n\n"
            "Install 7-Zip and ensure it is on PATH:\n"
            "  • Linux:   sudo pacman -S p7zip   (or apt/dnf equivalent)\n"
            "  • macOS:   brew install p7zip\n"
            "  • Windows: install from https://7-zip.org and add to PATH"
        );
        return false;
    }

    const QString tempDir = stagingDir() + "/_msi_tmp";
    {
        QDir td(tempDir);
        if (td.exists()) td.removeRecursively();
    }
    if (!QDir().mkpath(tempDir)) {
        emit stageFailed("Cannot create extraction temp directory.");
        return false;
    }

    auto runSevenZip = [&](const QStringList& args, const QString& step) -> bool {
        QProcess p;
        p.setProgram(sevenZ);
        p.setArguments(args);
        p.start();
        if (!p.waitForStarted(5000)) {
            emit stageFailed(step + " failed to start: " + p.errorString());
            return false;
        }
        // MSI extraction can be slow on large installers — give it 5 minutes.
        if (!p.waitForFinished(300000)) {
            p.kill();
            emit stageFailed(step + " timed out.");
            return false;
        }
        if (p.exitCode() != 0) {
            const QString err = QString::fromUtf8(p.readAllStandardError()).trimmed();
            emit stageFailed(step + " failed: " + (err.isEmpty() ? "exit code "
                + QString::number(p.exitCode()) : err));
            return false;
        }
        return true;
    };

    // Extract the embedded CAB streams from the MSI's OLE Compound File.
    emit stageProgress(75, "Extracting installer streams...");
    if (!runSevenZip({"x", "-y", "-o" + tempDir, msiPath, "cab*.cab"},
                     "MSI stream extraction"))
        return false;

    QDir td(tempDir);
    QStringList cabFiles = td.entryList({"cab*.cab"}, QDir::Files);
    if (cabFiles.isEmpty()) {
        emit stageFailed("No CAB streams found in MSI.");
        return false;
    }

    // Each CAB has a single LZX-compressed payload; decompress all.
    emit stageProgress(82, QString("Decompressing %1 CAB streams...").arg(cabFiles.size()));
    for (const QString& cab : cabFiles) {
        if (!runSevenZip({"x", "-y", "-o" + tempDir, tempDir + "/" + cab},
                         "CAB " + cab + " decompression"))
            return false;
    }

    // Scan the decompressed payloads for Salted__ magic.
    emit stageProgress(88, "Locating firmware blobs...");
    struct Blob { QString path; qint64 size; };
    QList<Blob> blobs;
    for (const QFileInfo& fi : td.entryInfoList(QDir::Files)) {
        if (fi.fileName().startsWith("cab"))
            continue;
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        const QByteArray hdr = f.read(8);
        f.close();
        if (hdr == QByteArrayLiteral("Salted__"))
            blobs.append({fi.absoluteFilePath(), fi.size()});
    }

    if (blobs.isEmpty()) {
        emit stageFailed("No firmware blobs (Salted__ header) found in MSI cabinets.");
        return false;
    }

    // FLEX-6x00 consumer firmware bundles every consumer platform and is
    // significantly larger than the FLEX-9600 government build, so size order
    // gives us the family mapping reliably.
    std::sort(blobs.begin(), blobs.end(), [](const Blob& a, const Blob& b) {
        return a.size > b.size;
    });

    int targetIdx = -1;
    if (m_modelFamily == "9600") {
        if (blobs.size() < 2) {
            emit stageFailed("FLEX-9600 firmware not present in this installer.");
            td.removeRecursively();
            return false;
        }
        targetIdx = blobs.size() - 1;  // smallest blob
    } else {
        targetIdx = 0;                  // largest blob (multi-platform consumer)
    }

    const Blob& target = blobs[targetIdx];
    emit stageProgress(90, QString("Staging firmware (%1 MB)...").arg(target.size / (1024*1024)));

    if (!QFile::rename(target.path, outPath)) {
        // Cross-device fallback (e.g. /tmp on tmpfs vs ~/.config on disk).
        if (!QFile::copy(target.path, outPath)) {
            emit stageFailed("Failed to stage firmware file from temp dir.");
            td.removeRecursively();
            return false;
        }
        QFile::remove(target.path);
    }

    td.removeRecursively();
    return true;
}

} // namespace AetherSDR
