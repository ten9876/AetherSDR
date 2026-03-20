#include "SupportDialog.h"
#include "core/LogManager.h"
#include "core/SupportBundle.h"

#include <QCheckBox>
#include <QCursor>
#include <QDesktopServices>
#include <QMessageBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QUrl>
#include <QVBoxLayout>

namespace AetherSDR {

SupportDialog::SupportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Support & Diagnostics");
    setMinimumSize(600, 520);
    resize(680, 600);
    buildUI();
    syncCheckboxes();
    refreshLog();
}

void SupportDialog::buildUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(8);

    // ── Diagnostic Logging group ──────────────────────────────────────────
    auto* logGroup = new QGroupBox("Diagnostic Logging");
    auto* gridLayout = new QGridLayout(logGroup);
    gridLayout->setSpacing(4);

    auto& mgr = LogManager::instance();
    const auto cats = mgr.categories();
    int col = 0, row = 0;
    constexpr int COLS = 3;

    for (const auto& cat : cats) {
        auto* cb = new QCheckBox(cat.label);
        cb->setToolTip(cat.description);
        cb->setStyleSheet("QCheckBox { color: #c8d8e8; }");
        gridLayout->addWidget(cb, row, col);

        connect(cb, &QCheckBox::toggled, this, [id = cat.id](bool on) {
            LogManager::instance().setEnabled(id, on);
        });

        m_checkboxes[cat.id] = cb;
        if (++col >= COLS) { col = 0; ++row; }
    }

    auto* btnRow = new QHBoxLayout;
    auto* enableAllBtn = new QPushButton("Enable All");
    auto* disableAllBtn = new QPushButton("Disable All");
    enableAllBtn->setFixedHeight(24);
    disableAllBtn->setFixedHeight(24);
    connect(enableAllBtn, &QPushButton::clicked, this, &SupportDialog::enableAll);
    connect(disableAllBtn, &QPushButton::clicked, this, &SupportDialog::disableAll);
    btnRow->addWidget(enableAllBtn);
    btnRow->addWidget(disableAllBtn);
    btnRow->addStretch();

    auto* groupLayout = new QVBoxLayout;
    groupLayout->addLayout(gridLayout);
    groupLayout->addLayout(btnRow);
    logGroup->setLayout(groupLayout);
    layout->addWidget(logGroup);

    // ── Log file info ─────────────────────────────────────────────────────
    auto* fileRow = new QHBoxLayout;
    auto* pathLabel = new QLabel(QString("Log: <code>%1</code>").arg(
        LogManager::instance().logFilePath()));
    pathLabel->setTextFormat(Qt::RichText);
    pathLabel->setStyleSheet("color: #8898a8; font-size: 11px;");
    m_sizeLabel = new QLabel;
    m_sizeLabel->setStyleSheet("color: #8898a8; font-size: 11px;");
    fileRow->addWidget(pathLabel);
    fileRow->addStretch();
    fileRow->addWidget(m_sizeLabel);
    layout->addLayout(fileRow);

    // ── Log viewer ────────────────────────────────────────────────────────
    m_logViewer = new QPlainTextEdit;
    m_logViewer->setReadOnly(true);
    m_logViewer->setMaximumBlockCount(2000);
    m_logViewer->setStyleSheet(
        "QPlainTextEdit {"
        "  background: #0a0a14;"
        "  color: #a0b0c0;"
        "  font-family: monospace;"
        "  font-size: 11px;"
        "  border: 1px solid #203040;"
        "}");
    layout->addWidget(m_logViewer, 1);  // stretch

    // ── Log action buttons ────────────────────────────────────────────────
    auto* actionRow = new QHBoxLayout;
    auto* refreshBtn = new QPushButton("Refresh");
    auto* clearBtn = new QPushButton("Clear Log");
    auto* openBtn = new QPushButton("Open Log Folder");
    refreshBtn->setFixedHeight(26);
    clearBtn->setFixedHeight(26);
    openBtn->setFixedHeight(26);
    connect(refreshBtn, &QPushButton::clicked, this, &SupportDialog::refreshLog);
    connect(clearBtn, &QPushButton::clicked, this, &SupportDialog::clearLog);
    connect(openBtn, &QPushButton::clicked, this, &SupportDialog::openLogFolder);
    auto* sendBtn = new QPushButton("Send to Support");
    sendBtn->setFixedHeight(26);
    sendBtn->setStyleSheet("QPushButton { background: #00607a; color: #e0f0ff; font-weight: bold; }");
    connect(sendBtn, &QPushButton::clicked, this, &SupportDialog::sendToSupport);
    actionRow->addWidget(refreshBtn);
    actionRow->addWidget(clearBtn);
    actionRow->addWidget(openBtn);
    actionRow->addStretch();
    actionRow->addWidget(sendBtn);
    layout->addLayout(actionRow);

    // ── Instructions ──────────────────────────────────────────────────────
    auto* instructions = new QLabel(
        "<p style='color:#7888a0; font-size:11px;'>"
        "To report an issue:<br>"
        "1. Enable logging for the relevant module(s) above<br>"
        "2. Reproduce the problem<br>"
        "3. Send the log file to <b>support@aethersdr.com</b></p>");
    instructions->setTextFormat(Qt::RichText);
    layout->addWidget(instructions);

    // ── Close button ──────────────────────────────────────────────────────
    auto* closeRow = new QHBoxLayout;
    closeRow->addStretch();
    auto* closeBtn = new QPushButton("Close");
    closeBtn->setFixedHeight(28);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    closeRow->addWidget(closeBtn);
    layout->addLayout(closeRow);
}

void SupportDialog::syncCheckboxes()
{
    auto& mgr = LogManager::instance();
    for (auto it = m_checkboxes.begin(); it != m_checkboxes.end(); ++it) {
        QSignalBlocker b(it.value());
        it.value()->setChecked(mgr.isEnabled(it.key()));
    }
}

void SupportDialog::refreshLog()
{
    auto& mgr = LogManager::instance();
    m_sizeLabel->setText(formatFileSize(mgr.logFileSize()));

    QFile f(mgr.logFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_logViewer->setPlainText("(unable to open log file)");
        return;
    }

    // Read last 200KB max to keep UI responsive
    constexpr qint64 MAX_READ = 200 * 1024;
    if (f.size() > MAX_READ)
        f.seek(f.size() - MAX_READ);

    m_logViewer->setPlainText(QString::fromUtf8(f.readAll()));
    f.close();

    // Scroll to bottom
    auto* sb = m_logViewer->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void SupportDialog::clearLog()
{
    LogManager::instance().clearLog();
    refreshLog();
}

void SupportDialog::openLogFolder()
{
    QFileInfo fi(LogManager::instance().logFilePath());
    QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
}

void SupportDialog::enableAll()
{
    LogManager::instance().setAllEnabled(true);
    syncCheckboxes();
}

void SupportDialog::disableAll()
{
    LogManager::instance().setAllEnabled(false);
    syncCheckboxes();
}

void SupportDialog::sendToSupport()
{
    setCursor(Qt::WaitCursor);

    auto sys = SupportBundle::collectSystemInfo();
    auto radio = SupportBundle::collectRadioInfo(m_radioModel);
    QString bundlePath = SupportBundle::createBundle(radio);

    setCursor(Qt::ArrowCursor);

    if (bundlePath.isEmpty()) {
        QMessageBox::warning(this, "Support Bundle",
            "Failed to create support bundle.\n"
            "Check that the log directory is writable.");
        return;
    }

    QMessageBox::information(this, "Support Bundle Created",
        QString("Support bundle saved to:\n%1\n\n"
                "Your email client will now open.\n"
                "Please attach the bundle file and describe the issue.")
            .arg(bundlePath));

    SupportBundle::openEmailClient(bundlePath, sys, radio);
}

QString SupportDialog::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

} // namespace AetherSDR
