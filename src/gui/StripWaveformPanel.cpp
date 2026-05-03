#include "StripWaveformPanel.h"

#include "EditorFramelessTitleBar.h"
#include "StripWaveform.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {
constexpr const char* kWindowStyle =
    "QWidget { background: #08121d; color: #d7e7f2; }"
    "QLabel  { background: transparent; color: #8aa8c0; font-size: 11px; }";
}

StripWaveformPanel::StripWaveformPanel(AudioEngine* engine, QWidget* parent)
    : QWidget(parent)
    , m_audio(engine)
{
    const QString title = QString::fromUtf8("Aetherial Waveform \xe2\x80\x94 TX");
    setWindowTitle(title);
    setStyleSheet(kWindowStyle);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 0, 8, 0);
    root->setSpacing(6);

    auto* titleBar = new EditorFramelessTitleBar;
    titleBar->setTitleText(title);
    // Embedded inside the strip — no need for the min/max/close trio
    // (the strip's own chrome carries those) and the dark fill bar
    // reads as a heavy header in this row.  Drop both so the panel
    // wears just the title text on the panel's own background.
    titleBar->setControlsVisible(false);
    titleBar->setStyleSheet("background: transparent;");

    // View-mode cycle button lives on the title-bar row.  Single press
    // advances Scope → Envelope → History → Scope.  Default starts on
    // Envelope (m_modeIdx = 1) since CE-SSB is fundamentally about
    // envelope behaviour.
    m_modeBtn = new QPushButton(this);
    m_modeBtn->setFixedSize(78, 18);
    m_modeBtn->setStyleSheet(
        "QPushButton {"
        "  background: #1a2a3a; border: 1px solid #2a4458;"
        "  border-radius: 3px; color: #c8a070;"
        "  font-size: 10px; font-weight: bold; padding: 1px 6px;"
        "}"
        "QPushButton:hover { background: #3a2818; color: #f2c14e;"
        "                    border: 1px solid #f2c14e; }");
    m_modeBtn->setToolTip("Cycle waveform view: Scope → Envelope → History");
    connect(m_modeBtn, &QPushButton::clicked,
            this, &StripWaveformPanel::cycleViewMode);

    // Time-window slider (1–20 s) + readout, also on the title row.
    // Adjusts how much wall-clock audio fits across the plot.
    m_windowSlider = new QSlider(Qt::Horizontal, this);
    m_windowSlider->setRange(1, 20);
    m_windowSlider->setSingleStep(1);
    m_windowSlider->setPageStep(1);   // mouse wheel notch = ±1 sec
    m_windowSlider->setFixedWidth(120);
    m_windowSlider->setFixedHeight(14);
    m_windowSlider->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: #1a2a3a;"
        " border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: #f2c14e;"
        " border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 10px; height: 10px;"
        " margin: -3px 0; background: #ffffff; border: 1px solid #f2c14e;"
        " border-radius: 5px; }"
        "QSlider::handle:horizontal:hover { background: #ffffff;"
        " border: 1px solid #ffd060; }");
    m_windowSlider->setToolTip(
        tr("Waveform time window — how many seconds of audio fit across "
           "the plot.  Range 1–20 s."));
    connect(m_windowSlider, &QSlider::valueChanged,
            this, &StripWaveformPanel::applyWindowSec);

    m_windowLbl = new QLabel(this);
    m_windowLbl->setFixedWidth(28);
    m_windowLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_windowLbl->setStyleSheet(
        "QLabel { background: transparent; color: #8aa8c0;"
        " font-size: 10px; font-weight: bold; }");

    auto* titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(4);
    titleRow->addWidget(titleBar, 1);
    titleRow->addWidget(m_windowSlider);
    titleRow->addWidget(m_windowLbl);
    titleRow->addWidget(m_modeBtn);
    root->addLayout(titleRow);

    m_waveform = new StripWaveform(this);
    // Restore the saved time window (or default to 20 s) and apply
    // it through the slider so the readout label updates in lockstep.
    const int savedSec = std::clamp(
        AppSettings::instance().value(
            "AetherialStripWaveformWindowSec", "20").toInt(),
        1, 20);
    {
        QSignalBlocker b(m_windowSlider);
        m_windowSlider->setValue(savedSec);
    }
    applyWindowSec(savedSec);
    // 90 Hz repaint rate so the long-window scroll reads as smooth
    // motion.  The engine's post-chain scope tap fires up to ~125 Hz
    // (kTxPostChainEmitMinIntervalMs = 8 ms) so the widget always
    // has fresh data on every frame.
    m_waveform->setRefreshRateHz(90);
    // The strip waveform is TX-only by design — pin the widget into
    // its TX render path so it never falls back to the RX buffer when
    // the radio isn't transmitting.  Source samples come from the
    // dedicated post-chain TX tap below; when we're not transmitting
    // the chain produces silence, which is the correct visual.
    m_waveform->setTransmitting(true);
    root->addWidget(m_waveform, 1);

    applyViewMode();

    if (m_audio) {
        // TX-only tap sampled at the very END of the strip's audio
        // path — after the user's DSP chain, after PC mic gain, AND
        // after the final brickwall limiter.  This is exactly what
        // gets packetised into VITA-49 and sent to the radio, so the
        // panel shows the operator's actual transmitted envelope.
        connect(m_audio, &AudioEngine::txPostChainScopeReady,
                m_waveform, [this](const QByteArray& mono, int sr) {
            if (m_waveform) m_waveform->appendScopeSamples(mono, sr, /*tx=*/true);
        });
    }
}

StripWaveformPanel::~StripWaveformPanel() = default;

void StripWaveformPanel::showForTx()
{
    show();
    raise();
    activateWindow();
}

void StripWaveformPanel::syncControlsFromEngine()
{
    // No engine-driven controls yet — kept for API symmetry with the
    // other strip panels so the AetherialAudioStrip can iterate every
    // panel uniformly when applying a preset.
}

void StripWaveformPanel::cycleViewMode()
{
    m_modeIdx = (m_modeIdx + 1) % 3;
    applyViewMode();
}

void StripWaveformPanel::applyWindowSec(int sec)
{
    sec = std::clamp(sec, 1, 20);
    if (m_waveform) m_waveform->setZoomWindowMs(sec * 1000);
    if (m_windowLbl) m_windowLbl->setText(QString("%1 s").arg(sec));
    AppSettings::instance().setValue(
        "AetherialStripWaveformWindowSec", QString::number(sec));
}

void StripWaveformPanel::applyViewMode()
{
    if (!m_waveform) return;
    StripWaveform::ViewMode mode = StripWaveform::ViewMode::Envelope;
    QString label = "ENVELOPE";
    switch (m_modeIdx) {
        case 0: mode = StripWaveform::ViewMode::Graph;    label = "SCOPE";    break;
        case 1: mode = StripWaveform::ViewMode::Envelope; label = "ENVELOPE"; break;
        case 2: mode = StripWaveform::ViewMode::Bars;     label = "HISTORY";  break;
    }
    m_waveform->setViewMode(mode);
    if (m_modeBtn) m_modeBtn->setText(label);
}

} // namespace AetherSDR
