#include "ClientEqEditor.h"
#include "ClientEqEditorCanvas.h"
#include "ClientEqFftAnalyzer.h"
#include "ClientEqIconRow.h"
#include "ClientEqParamRow.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/ClientEq.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMoveEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <vector>

namespace AetherSDR {

namespace {

constexpr int kDefaultWidth  = 900;
constexpr int kDefaultHeight = 520;

constexpr const char* kWindowStyle =
    "QWidget { background: #08121d; color: #d7e7f2; }"
    "QLabel  { background: transparent; color: #8aa8c0; font-size: 11px; }";

// Bypass button visual: unchecked = EQ active (subtle), checked = bypass
// engaged (amber, signals "this is muting your work").  Plugin convention.
const QString kBypassStyle = QStringLiteral(
    "QPushButton {"
    "  background: #0e1b28;"
    "  color: #8aa8c0;"
    "  border: 1px solid #243a4e;"
    "  border-radius: 3px;"
    "  font-size: 11px;"
    "  font-weight: bold;"
    "  padding: 3px 12px;"
    "}"
    "QPushButton:hover { background: #1a2a3a; }"
    "QPushButton:checked {"
    "  background: #3a2a0e;"
    "  color: #f2c14e;"
    "  border: 1px solid #f2c14e;"
    "}"
    "QPushButton:checked:hover { background: #4a3a1e; }");

} // namespace

ClientEqEditor::ClientEqEditor(AudioEngine* engine, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_audio(engine)
{
    setWindowTitle("Client EQ");
    setStyleSheet(kWindowStyle);
    resize(kDefaultWidth, kDefaultHeight);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // Path indicator + interaction hint + bypass strip.
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        m_pathLabel = new QLabel("RX");
        m_pathLabel->setStyleSheet(
            "QLabel { color: #d7e7f2; font-size: 12px; font-weight: bold; }");
        row->addWidget(m_pathLabel);
        auto* hint = new QLabel(
            "Double-click to add · drag peak/shelf = freq + gain · "
            "drag HP/LP = freq + Q · Shift + drag for Q · "
            "click icon to cycle type");
        hint->setStyleSheet("QLabel { color: #506070; font-size: 10px; }");
        row->addWidget(hint, 1);

        m_bypass = new QPushButton("BYPASS");
        m_bypass->setCheckable(true);
        m_bypass->setStyleSheet(kBypassStyle);
        m_bypass->setFixedHeight(24);
        m_bypass->setToolTip(
            "Toggle EQ bypass for this path — A/B your changes.\n"
            "Shortcut: B (while this window has focus)");
        connect(m_bypass, &QPushButton::toggled, this, [this](bool bypassed) {
            ClientEq* eq = (m_path == ClientEqApplet::Path::Rx)
                ? m_audio->clientEqRx() : m_audio->clientEqTx();
            if (!eq) return;
            eq->setEnabled(!bypassed);
            if (m_audio) m_audio->saveClientEqSettings();
            if (m_canvas) m_canvas->update();  // curve gray when bypassed
            emit bypassToggled(m_path, bypassed);
        });
        row->addWidget(m_bypass);

        root->addLayout(row);
    }

    // Keyboard shortcut: B toggles bypass while the editor has focus.
    // Application-wide would collide with other shortcuts, so scope it
    // to this window.
    auto* shortcut = new QShortcut(QKeySequence(Qt::Key_B), this);
    shortcut->setContext(Qt::WindowShortcut);
    connect(shortcut, &QShortcut::activated, this, [this]() {
        if (m_bypass) m_bypass->toggle();
    });

    // Filter-type icon row: one glyph per active band at the top.
    m_iconRow = new ClientEqIconRow;
    m_iconRow->setAudioEngine(m_audio);
    root->addWidget(m_iconRow);

    // Central interactive curve canvas.
    m_canvas = new ClientEqEditorCanvas;
    m_canvas->setAudioEngine(m_audio);
    root->addWidget(m_canvas, 1);

    // Bottom parameter-text row.
    m_paramRow = new ClientEqParamRow;
    root->addWidget(m_paramRow);

    // Selection plumbing: any of the three views announcing a selection
    // change fans out to the other two, plus triggers a paint refresh.
    connect(m_canvas, &ClientEqCurveWidget::selectedBandChanged,
            this, &ClientEqEditor::syncSelection);
    connect(m_iconRow, &ClientEqIconRow::bandSelected,
            this, &ClientEqEditor::syncSelection);
    connect(m_paramRow, &ClientEqParamRow::bandSelected,
            this, &ClientEqEditor::syncSelection);

    // Any canvas-side band mutation refreshes the text-valued widgets
    // (icon row rebuilds on type / count change; param row updates
    // numeric display live during drags).
    connect(m_canvas, &ClientEqCurveWidget::bandsChanged,
            this, [this]() {
        if (m_iconRow)  m_iconRow->refresh();
        if (m_paramRow) {
            m_paramRow->refresh();
            m_paramRow->setSelectedBand(m_canvas->selectedBand());
        }
    });

    // FFT analyzer ticks on a QTimer while the editor is visible.  Pulls
    // the most-recent post-EQ samples from AudioEngine, runs the FFT on
    // the UI thread (microseconds at 256 points), and pushes smoothed
    // bins into the canvas.  The timer is stopped in hideEvent so it
    // doesn't burn CPU while the editor is closed.
    m_fftAnalyzer = std::make_unique<ClientEqFftAnalyzer>();
    m_fftTimer = new QTimer(this);
    m_fftTimer->setInterval(40);  // 25 Hz
    connect(m_fftTimer, &QTimer::timeout,
            this, &ClientEqEditor::tickFftAnalyzer);

    restoreGeometryFromSettings();
}

ClientEqEditor::~ClientEqEditor() = default;

void ClientEqEditor::tickFftAnalyzer()
{
    if (!m_audio || !m_canvas || !m_fftAnalyzer) return;

    std::vector<float> samples(ClientEqFftAnalyzer::kFftSize, 0.0f);
    bool ok = false;
    if (m_path == ClientEqApplet::Path::Rx) {
        ok = m_audio->copyRecentClientEqRxSamples(
                samples.data(), ClientEqFftAnalyzer::kFftSize);
    } else {
        ok = m_audio->copyRecentClientEqTxSamples(
                samples.data(), ClientEqFftAnalyzer::kFftSize);
    }
    if (!ok) return;

    m_fftAnalyzer->update(samples.data(), ClientEqFftAnalyzer::kFftSize);

    ClientEq* eq = (m_path == ClientEqApplet::Path::Rx)
        ? m_audio->clientEqRx() : m_audio->clientEqTx();
    const double fs = eq ? eq->sampleRate() : 24000.0;
    m_canvas->setFftBinsDb(m_fftAnalyzer->magnitudesDb(), fs);
}

void ClientEqEditor::syncBypassFromEq()
{
    if (!m_bypass || !m_audio) return;
    ClientEq* eq = (m_path == ClientEqApplet::Path::Rx)
        ? m_audio->clientEqRx() : m_audio->clientEqTx();
    if (!eq) return;
    QSignalBlocker b(m_bypass);
    m_bypass->setChecked(!eq->isEnabled());
}

void ClientEqEditor::syncSelection(int idx)
{
    if (m_iconRow)  m_iconRow->setSelectedBand(idx);
    if (m_canvas)   m_canvas->setSelectedBand(idx);
    if (m_paramRow) m_paramRow->setSelectedBand(idx);
    // Param row values also move under drags / type cycles, so refresh
    // display text whenever anything gets selected.
    if (m_paramRow) m_paramRow->refreshValues();
}

void ClientEqEditor::showForPath(ClientEqApplet::Path path)
{
    m_path = path;
    if (!m_audio || !m_canvas) return;

    ClientEq* eq = (path == ClientEqApplet::Path::Rx)
        ? m_audio->clientEqRx() : m_audio->clientEqTx();
    m_canvas->setEq(eq);
    if (m_iconRow)  m_iconRow->setEq(eq);
    if (m_paramRow) m_paramRow->setEq(eq);
    syncBypassFromEq();
    // Clear selection on path swap — the previously-selected index
    // almost certainly doesn't correspond to the other path's bands.
    syncSelection(-1);
    m_pathLabel->setText(path == ClientEqApplet::Path::Rx
                         ? "Client EQ — RX"
                         : "Client EQ — TX");
    setWindowTitle(path == ClientEqApplet::Path::Rx
                   ? "Client EQ — RX"
                   : "Client EQ — TX");

    if (!isVisible()) {
        show();
    }
    raise();
    activateWindow();
}

void ClientEqEditor::closeEvent(QCloseEvent* ev)
{
    saveGeometryToSettings();
    QWidget::closeEvent(ev);
}

void ClientEqEditor::showEvent(QShowEvent* ev)
{
    QWidget::showEvent(ev);
    if (m_fftAnalyzer) m_fftAnalyzer->reset();
    if (m_fftTimer && !m_fftTimer->isActive()) m_fftTimer->start();
}

void ClientEqEditor::hideEvent(QHideEvent* ev)
{
    QWidget::hideEvent(ev);
    if (m_fftTimer) m_fftTimer->stop();
    // Clear the canvas's last FFT snapshot so it doesn't paint stale
    // energy next time the window opens.
    if (m_canvas) m_canvas->setFftBinsDb({}, 24000.0);
}

void ClientEqEditor::moveEvent(QMoveEvent* ev)
{
    QWidget::moveEvent(ev);
    if (!m_restoring && isVisible()) saveGeometryToSettings();
}

void ClientEqEditor::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    if (!m_restoring && isVisible()) saveGeometryToSettings();
}

void ClientEqEditor::saveGeometryToSettings()
{
    auto& s = AppSettings::instance();
    const QRect g = geometry();
    s.setValue("ClientEqEditor_X", QString::number(g.x()));
    s.setValue("ClientEqEditor_Y", QString::number(g.y()));
    s.setValue("ClientEqEditor_W", QString::number(g.width()));
    s.setValue("ClientEqEditor_H", QString::number(g.height()));
    s.save();
}

void ClientEqEditor::restoreGeometryFromSettings()
{
    m_restoring = true;
    auto& s = AppSettings::instance();
    const int w = s.value("ClientEqEditor_W",
                          QString::number(kDefaultWidth)).toString().toInt();
    const int h = s.value("ClientEqEditor_H",
                          QString::number(kDefaultHeight)).toString().toInt();
    resize(std::max(w, 600), std::max(h, 320));

    // Only honour saved position if it's non-default — otherwise let the
    // window manager pick a reasonable spawn spot.
    const QString xs = s.value("ClientEqEditor_X", "").toString();
    const QString ys = s.value("ClientEqEditor_Y", "").toString();
    if (!xs.isEmpty() && !ys.isEmpty()) {
        move(xs.toInt(), ys.toInt());
    }
    m_restoring = false;
}

} // namespace AetherSDR
