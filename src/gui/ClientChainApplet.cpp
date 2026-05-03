#include "ClientChainApplet.h"
#include "ClientChainWidget.h"
#include "ClientRxChainWidget.h"
#include "core/AppSettings.h"
#include "core/ClientComp.h"
#include "core/ClientDeEss.h"
#include "core/ClientEq.h"
#include "core/ClientGate.h"
#include "core/ClientPudu.h"
#include "core/ClientReverb.h"
#include "core/ClientTube.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace AetherSDR {

namespace {

// TX / RX mode-select buttons — checkable, part of an exclusive
// group.  Checked state uses the amber PooDoo colour so the active
// chain reads at a glance.
const QString kModeBtnStyle = QStringLiteral(
    "QPushButton {"
    "  background: #1a2a3a; border: 1px solid #2a4458; border-radius: 3px;"
    "  color: #8aa8c0; font-size: 10px; font-weight: bold;"
    "  padding: 2px 10px; min-width: 30px;"
    "}"
    "QPushButton:hover { background: #24384e; }"
    "QPushButton:checked {"
    "  background: #3a2a0e; color: #f2c14e; border: 1px solid #f2c14e;"
    "}");

// PUDU monitor icon buttons — modelled on VfoWidget's per-slice
// record/play buttons (see src/gui/VfoWidget.cpp:414-454).  20x20,
// rounded, unicode glyphs.  Pulsing is a 500 ms tick toggling
// dim/bright via re-applied stylesheet.
constexpr const char* kMonBtnBase =
    "QPushButton { background: rgba(255,255,255,15); border: none; "
    " border-radius: 10px; font-size: 11px; padding: 0; }"
    "QPushButton:hover:enabled { background: rgba(255,255,255,40); }"
    "QPushButton:disabled { color: #303030; "
    " background: rgba(255,255,255,5); }";

// Style fragments per state for the record/play buttons.  Idle red
// is dimmed; active red is bright + filled.  Pulse alternates between
// two levels.  Play mirrors with green colours.
constexpr const char* kMonRecIdle  =
    "QPushButton:enabled { color: #804040; }";
constexpr const char* kMonRecActiveBright =
    " color: #ff2020; background: rgba(255,50,50,60);";
constexpr const char* kMonRecActiveDim =
    " color: #601010; background: rgba(255,50,50,20);";
constexpr const char* kMonPlayIdle =
    "QPushButton:enabled { color: #406040; }";
constexpr const char* kMonPlayActiveBright =
    " color: #30d050; background: rgba(50,200,80,60);";
constexpr const char* kMonPlayActiveDim =
    " color: #103010; background: rgba(50,200,80,20);";

// BYPASS — checkable toggle.  Idle: muted amber.  Checked: saturated
// amber border + fill so an active bypass can't be missed.
const QString kBypassBtnStyle = QStringLiteral(
    "QPushButton {"
    "  background: #1a2a3a; border: 1px solid #4a3020; border-radius: 3px;"
    "  color: #c8a070; font-size: 10px; font-weight: bold;"
    "  padding: 2px 10px;"
    "}"
    "QPushButton:hover { background: #3a2818; color: #f2c14e;"
    "                    border: 1px solid #f2c14e; }"
    "QPushButton:checked {"
    "  background: #4a3818; color: #f2c14e; border: 1px solid #f2c14e;"
    "}"
    "QPushButton:checked:hover { background: #5a4a28; }");

} // namespace

ClientChainApplet::ClientChainApplet(QWidget* parent) : QWidget(parent)
{
    setStyleSheet("QWidget { background: transparent; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    // ── Header: TX | RX | BYPASS ────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        auto* group = new QButtonGroup(this);
        group->setExclusive(true);

        m_txBtn = new QPushButton("TX");
        m_txBtn->setCheckable(true);
        m_txBtn->setStyleSheet(kModeBtnStyle);
        m_txBtn->setFixedHeight(22);
        m_txBtn->setToolTip("Show and edit the TX DSP chain");
        m_txBtn->setChecked(true);
        group->addButton(m_txBtn, static_cast<int>(ChainMode::Tx));
        row->addWidget(m_txBtn);

        m_rxBtn = new QPushButton("RX");
        m_rxBtn->setCheckable(true);
        m_rxBtn->setStyleSheet(kModeBtnStyle);
        m_rxBtn->setFixedHeight(22);
        m_rxBtn->setToolTip("Show and edit the RX DSP chain");
        group->addButton(m_rxBtn, static_cast<int>(ChainMode::Rx));
        row->addWidget(m_rxBtn);

        // ── PUDU monitor buttons (right of the mode toggles) ────
        // Small icon buttons for record/playback of the post-PUDU
        // TX audio.  Separate from the mode toggles so the visual
        // grouping stays intact.
        row->addSpacing(6);

        m_monRecBtn = new QPushButton(QString::fromUtf8("\xe2\x8f\xba"));   // ⏺
        m_monRecBtn->setCheckable(true);
        m_monRecBtn->setFixedSize(20, 20);
        m_monRecBtn->setEnabled(false);
        m_monRecBtn->setToolTip(
            "Record up to 30 s of post-PooDoo™ TX audio (MIC must be set "
            "to PC and DAX off).  Click again to stop; playback starts "
            "automatically.");
        connect(m_monRecBtn, &QPushButton::clicked, this, [this]() {
            emit monitorRecordClicked();
        });
        row->addWidget(m_monRecBtn);

        m_monPlayBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xb6")); // ▶
        m_monPlayBtn->setCheckable(true);
        m_monPlayBtn->setFixedSize(20, 20);
        m_monPlayBtn->setEnabled(false);
        m_monPlayBtn->setToolTip(
            "Play back the captured PooDoo™ audio.  Click again to "
            "cancel playback.");
        connect(m_monPlayBtn, &QPushButton::clicked, this, [this]() {
            emit monitorPlayClicked();
        });
        row->addWidget(m_monPlayBtn);

        // Pulse timers — lazy-start so idle buttons don't tick.
        m_monRecPulse = new QTimer(this);
        m_monRecPulse->setInterval(500);
        connect(m_monRecPulse, &QTimer::timeout, this, [this]() {
            m_monRecPulseDim = !m_monRecPulseDim;
            applyRecordButtonStyle();
        });
        m_monPlayPulse = new QTimer(this);
        m_monPlayPulse->setInterval(500);
        connect(m_monPlayPulse, &QTimer::timeout, this, [this]() {
            m_monPlayPulseDim = !m_monPlayPulseDim;
            applyPlayButtonStyle();
        });

        applyRecordButtonStyle();
        applyPlayButtonStyle();

        row->addStretch();

        m_bypassBtn = new QPushButton("BYPASS");
        m_bypassBtn->setCheckable(true);
        m_bypassBtn->setStyleSheet(kBypassBtnStyle);
        m_bypassBtn->setFixedHeight(22);
        m_bypassBtn->setToolTip(
            "Disable every stage in the selected chain.  Click again "
            "to restore the stages that were on before.");
        connect(m_bypassBtn, &QPushButton::toggled,
                this, &ClientChainApplet::onBypassToggled);
        row->addWidget(m_bypassBtn);

        connect(group, &QButtonGroup::idToggled, this,
                [this](int id, bool checked) {
            if (checked) setMode(static_cast<ChainMode>(id));
        });

        outer->addLayout(row);
    }

    // ── Chain strips (TX + RX), stacked — only one visible at a time.
    // Phase 0: the RX strip ships with three live status tiles
    // (RADIO / DSP / SPEAK) bracketing five "coming soon" placeholders
    // for the user-controllable stages.
    m_chain = new ClientChainWidget;
    outer->addWidget(m_chain);

    m_rxChain = new ClientRxChainWidget;
    m_rxChain->hide();
    outer->addWidget(m_rxChain);

    // ── Hint (below the chain) ──────────────────────────────────
    m_hint = new QLabel(
        "Click to bypass · Double click to edit · Drag to reorder");
    m_hint->setStyleSheet(
        "QLabel { color: #607888; font-size: 9px;"
        " background: transparent; border: none; }");
    m_hint->setWordWrap(false);

    // Easter-egg nub at the bottom-right — opens the Aetherial Audio
    // Channel Strip (unified TX DSP window, issue #2301).  Bare egg
    // glyph, no chrome — discoverable only to people who know it's
    // there until the strip becomes canonical.
    m_stripNub = new QPushButton(QString::fromUtf8("\xF0\x9F\xA5\x9A"), this);
    m_stripNub->setFixedSize(16, 16);
    m_stripNub->setFlat(true);
    m_stripNub->setFocusPolicy(Qt::NoFocus);
    m_stripNub->setStyleSheet(
        "QPushButton { background: transparent; border: none;"
        " font-size: 11px; padding: 0; }");
    connect(m_stripNub, &QPushButton::clicked,
            this, &ClientChainApplet::aetherialStripToggleRequested);

    auto* hintRow = new QHBoxLayout;
    hintRow->setContentsMargins(0, 0, 0, 0);
    hintRow->setSpacing(4);
    hintRow->addWidget(m_hint, 1);
    hintRow->addWidget(m_stripNub, 0, Qt::AlignBottom | Qt::AlignRight);
    outer->addLayout(hintRow);

    connect(m_chain, &ClientChainWidget::editRequested,
            this, &ClientChainApplet::editRequested);
    connect(m_chain, &ClientChainWidget::stageEnabledChanged,
            this, &ClientChainApplet::stageEnabledChanged);
    connect(m_chain, &ClientChainWidget::chainReordered,
            this, &ClientChainApplet::chainReordered);
    connect(m_rxChain, &ClientRxChainWidget::editRequested,
            this, &ClientChainApplet::rxEditRequested);
    connect(m_rxChain, &ClientRxChainWidget::dspEditRequested,
            this, &ClientChainApplet::rxDspEditRequested);
    connect(m_rxChain, &ClientRxChainWidget::nr2EnableWithWisdomRequested,
            this, &ClientChainApplet::rxNr2EnableWithWisdomRequested);
    connect(m_rxChain, &ClientRxChainWidget::stageEnabledChanged,
            this, &ClientChainApplet::rxStageEnabledChanged);
    connect(m_rxChain, &ClientRxChainWidget::chainReordered,
            this, &ClientChainApplet::rxChainReordered);

    // Restore last-active tab now that everything is wired.  Toggling
    // the button fires the group's idToggled signal which calls
    // setMode(), swapping widget visibility and saving the setting.
    const QString savedTab = AppSettings::instance()
        .value("PooDooAudioActiveTab", "TX").toString();
    if (savedTab == "RX" && m_rxBtn) {
        m_rxBtn->setChecked(true);
    }

    hide();  // hidden until toggled on from the applet tray
}

void ClientChainApplet::setAudioEngine(AudioEngine* engine)
{
    m_audio = engine;
    if (m_chain)   m_chain->setAudioEngine(engine);
    if (m_rxChain) m_rxChain->setAudioEngine(engine);

    // Master TX bypass is now an engine-owned state — observe its
    // signal so the BYPASS button mirrors clicks from the channel
    // strip's BYPASS button (and vice versa).  Only update the visual
    // when we're actually showing the TX side.
    if (m_audio && m_bypassBtn) {
        QSignalBlocker blocker(m_bypassBtn);
        if (m_mode == ChainMode::Tx) {
            m_bypassBtn->setChecked(m_audio->isTxBypassed());
        }
        connect(m_audio, &AudioEngine::txBypassChanged,
                this, [this](bool on) {
            if (!m_bypassBtn) return;
            if (m_mode != ChainMode::Tx) return;
            QSignalBlocker b(m_bypassBtn);
            m_bypassBtn->setChecked(on);
            if (m_chain) m_chain->update();
        });
    }
}

void ClientChainApplet::refreshFromEngine()
{
    if (m_chain) m_chain->update();
}

void ClientChainApplet::setMicInputReady(bool ready)
{
    if (m_chain) m_chain->setMicInputReady(ready);
    m_micReady = ready;
    updateMonitorButtonEnables();
}

void ClientChainApplet::setMonitorRecording(bool on)
{
    if (m_monRecording == on) return;
    m_monRecording = on;
    if (m_monRecBtn) {
        QSignalBlocker b(m_monRecBtn);
        m_monRecBtn->setChecked(on);
    }
    if (on) {
        m_monRecPulseDim = false;
        if (m_monRecPulse) m_monRecPulse->start();
    } else if (m_monRecPulse) {
        m_monRecPulse->stop();
    }
    applyRecordButtonStyle();
    updateMonitorButtonEnables();
}

void ClientChainApplet::setMonitorPlaying(bool on)
{
    if (m_monPlaying == on) return;
    m_monPlaying = on;
    if (m_monPlayBtn) {
        QSignalBlocker b(m_monPlayBtn);
        m_monPlayBtn->setChecked(on);
    }
    if (on) {
        m_monPlayPulseDim = false;
        if (m_monPlayPulse) m_monPlayPulse->start();
    } else if (m_monPlayPulse) {
        m_monPlayPulse->stop();
    }
    applyPlayButtonStyle();
    updateMonitorButtonEnables();
}

void ClientChainApplet::setMonitorHasRecording(bool has)
{
    if (m_monHasRecording == has) return;
    m_monHasRecording = has;
    updateMonitorButtonEnables();
}

void ClientChainApplet::applyRecordButtonStyle()
{
    if (!m_monRecBtn) return;
    QString style = kMonBtnBase;
    if (m_monRecording) {
        style += "QPushButton { ";
        style += m_monRecPulseDim ? kMonRecActiveDim : kMonRecActiveBright;
        style += " }";
    } else {
        style += kMonRecIdle;
    }
    m_monRecBtn->setStyleSheet(style);
}

void ClientChainApplet::applyPlayButtonStyle()
{
    if (!m_monPlayBtn) return;
    QString style = kMonBtnBase;
    if (m_monPlaying) {
        style += "QPushButton { ";
        style += m_monPlayPulseDim ? kMonPlayActiveDim : kMonPlayActiveBright;
        style += " }";
    } else {
        style += kMonPlayIdle;
    }
    m_monPlayBtn->setStyleSheet(style);
}

void ClientChainApplet::updateMonitorButtonEnables()
{
    // Record: enabled when MIC is ready AND we're not mid-playback.
    //   - While recording, stays enabled so the user can click it to
    //     stop (the button itself is the stop target).
    // Play: enabled when we have a recording AND we're not mid-record.
    //   - While playing, stays enabled so the user can click to cancel.
    if (m_monRecBtn) {
        const bool en = m_monRecording
            || (m_micReady && !m_monPlaying);
        m_monRecBtn->setEnabled(en);
    }
    if (m_monPlayBtn) {
        const bool en = m_monPlaying
            || (m_monHasRecording && !m_monRecording);
        m_monPlayBtn->setEnabled(en);
    }
}

void ClientChainApplet::setTxActive(bool active)
{
    if (m_chain) m_chain->setTxActive(active);
}

void ClientChainApplet::setMode(ChainMode m)
{
    if (m == m_mode) return;
    m_mode = m;

    const bool tx = (m == ChainMode::Tx);
    if (m_chain)      m_chain->setVisible(tx);
    if (m_rxChain)    m_rxChain->setVisible(!tx);
    // Monitor record/play buttons capture post-PUDU TX audio; they're
    // meaningless on the RX chain so hide them when RX is showing.
    if (m_monRecBtn)  m_monRecBtn->setVisible(tx);
    if (m_monPlayBtn) m_monPlayBtn->setVisible(tx);
    // Hint text applies to whichever chain is showing — both sides
    // now support the click-bypass / double-click-edit gestures.
    if (m_hint)       m_hint->setVisible(true);

    // BYPASS button visual must reflect the *current* tab's bypass
    // state — each side has its own snapshot.  QSignalBlocker keeps
    // the toggled() handler from re-firing onBypassToggled and
    // touching the engine.
    if (m_bypassBtn) {
        const bool bypassed = tx ? (m_audio && m_audio->isTxBypassed())
                                 : !m_rxBypassSnapshot.isEmpty();
        QSignalBlocker blocker(m_bypassBtn);
        m_bypassBtn->setChecked(bypassed);
    }

    AppSettings::instance().setValue(
        "PooDooAudioActiveTab", tx ? "TX" : "RX");

    emit chainModeChanged(m);
}

void ClientChainApplet::setActiveTab(ChainMode m)
{
    if (m == m_mode) return;
    // Re-route through the button group so the visual checked state
    // tracks; setMode() below runs as a side effect of the toggle.
    QPushButton* btn = (m == ChainMode::Tx) ? m_txBtn : m_rxBtn;
    if (btn) {
        QSignalBlocker blocker(btn);
        btn->setChecked(true);
    }
    setMode(m);
}

void ClientChainApplet::setRxPcAudioEnabled(bool on)
{
    if (m_rxChain) m_rxChain->setPcAudioEnabled(on);
}

void ClientChainApplet::setRxClientDspActive(bool on, const QString& label)
{
    if (m_rxChain) m_rxChain->setClientDspActive(on, label);
}

void ClientChainApplet::setRxOutputUnmuted(bool on)
{
    if (m_rxChain) m_rxChain->setOutputUnmuted(on);
}

void ClientChainApplet::onBypassToggled(bool checked)
{
    if (!m_audio) return;
    if (m_mode == ChainMode::Rx) {
        // RX BYPASS — same snapshot-and-disable / restore-on-uncheck
        // behaviour as TX, but routed through the RX engine instances
        // and per-side AppSettings keys.
        auto setRxStageEnabled = [&](AudioEngine::RxChainStage stage, bool on) {
            switch (stage) {
                case AudioEngine::RxChainStage::Eq:
                    if (auto* d = m_audio->clientEqRx()) {
                        d->setEnabled(on);
                        m_audio->saveClientEqSettings();
                    }
                    break;
                case AudioEngine::RxChainStage::Gate:
                    if (auto* d = m_audio->clientGateRx()) {
                        d->setEnabled(on);
                        m_audio->saveClientGateRxSettings();
                    }
                    break;
                case AudioEngine::RxChainStage::Comp:
                    if (auto* d = m_audio->clientCompRx()) {
                        d->setEnabled(on);
                        m_audio->saveClientCompRxSettings();
                    }
                    break;
                case AudioEngine::RxChainStage::Tube:
                    if (auto* d = m_audio->clientTubeRx()) {
                        d->setEnabled(on);
                        m_audio->saveClientTubeRxSettings();
                    }
                    break;
                case AudioEngine::RxChainStage::Pudu:
                    if (auto* d = m_audio->clientPuduRx()) {
                        d->setEnabled(on);
                        m_audio->saveClientPuduRxSettings();
                    }
                    break;
                case AudioEngine::RxChainStage::None:
                    break;
            }
            emit rxStageEnabledChanged(stage, on);
        };

        auto isRxEnabled = [&](AudioEngine::RxChainStage stage) {
            switch (stage) {
                case AudioEngine::RxChainStage::Eq:
                    return m_audio->clientEqRx() && m_audio->clientEqRx()->isEnabled();
                case AudioEngine::RxChainStage::Gate:
                    return m_audio->clientGateRx() && m_audio->clientGateRx()->isEnabled();
                case AudioEngine::RxChainStage::Comp:
                    return m_audio->clientCompRx() && m_audio->clientCompRx()->isEnabled();
                case AudioEngine::RxChainStage::Tube:
                    return m_audio->clientTubeRx() && m_audio->clientTubeRx()->isEnabled();
                case AudioEngine::RxChainStage::Pudu:
                    return m_audio->clientPuduRx() && m_audio->clientPuduRx()->isEnabled();
                case AudioEngine::RxChainStage::None:
                    return false;
            }
            return false;
        };

        static const QVector<AudioEngine::RxChainStage> kAllRxStages{
            AudioEngine::RxChainStage::Eq,
            AudioEngine::RxChainStage::Gate,
            AudioEngine::RxChainStage::Comp,
            AudioEngine::RxChainStage::Tube,
            AudioEngine::RxChainStage::Pudu,
        };

        if (checked) {
            m_rxBypassSnapshot.clear();
            for (auto s : kAllRxStages) {
                if (isRxEnabled(s)) {
                    m_rxBypassSnapshot.append(s);
                    setRxStageEnabled(s, false);
                }
            }
        } else {
            for (auto s : m_rxBypassSnapshot) setRxStageEnabled(s, true);
            m_rxBypassSnapshot.clear();
        }

        if (m_rxChain) m_rxChain->update();
        return;
    }

    // TX bypass — route through the engine so the strip's BYPASS
    // button stays in lock-step.  The engine owns the snapshot now;
    // ClientChainApplet observes txBypassChanged to update its own
    // button visual when the user toggles bypass elsewhere.
    m_audio->setTxBypassed(checked);
    if (m_chain) m_chain->update();
}

} // namespace AetherSDR
