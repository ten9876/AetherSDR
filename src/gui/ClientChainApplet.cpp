#include "ClientChainApplet.h"
#include "ClientChainWidget.h"
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
        m_rxBtn->setToolTip("Show and edit the RX DSP chain (coming soon)");
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

    // ── Chain strip (TX) + RX placeholder, stacked — only one visible
    m_chain = new ClientChainWidget;
    outer->addWidget(m_chain);

    m_rxPlaceholder = new QLabel("Client RX chain — coming in a future phase");
    m_rxPlaceholder->setAlignment(Qt::AlignCenter);
    m_rxPlaceholder->setStyleSheet(
        "QLabel { color: #607888; font-size: 10px; font-style: italic;"
        "         background: #0e1b28; border: 1px dashed #243a4e;"
        "         border-radius: 3px; padding: 14px; }");
    m_rxPlaceholder->hide();
    outer->addWidget(m_rxPlaceholder);

    // ── Hint (below the chain) ──────────────────────────────────
    m_hint = new QLabel(
        "Click to bypass · Double click to edit · Drag to reorder");
    m_hint->setStyleSheet(
        "QLabel { color: #607888; font-size: 9px;"
        " background: transparent; border: none; }");
    m_hint->setWordWrap(false);
    outer->addWidget(m_hint);

    connect(m_chain, &ClientChainWidget::editRequested,
            this, &ClientChainApplet::editRequested);
    connect(m_chain, &ClientChainWidget::stageEnabledChanged,
            this, &ClientChainApplet::stageEnabledChanged);
    connect(m_chain, &ClientChainWidget::chainReordered,
            this, &ClientChainApplet::chainReordered);

    hide();  // hidden until toggled on from the applet tray
}

void ClientChainApplet::setAudioEngine(AudioEngine* engine)
{
    m_audio = engine;
    if (m_chain) m_chain->setAudioEngine(engine);
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
    if (m_chain)          m_chain->setVisible(tx);
    if (m_rxPlaceholder)  m_rxPlaceholder->setVisible(!tx);
    // Hint text only makes sense for TX since that's the only
    // interactive chain today.
    if (m_hint)           m_hint->setVisible(tx);
}

void ClientChainApplet::onBypassToggled(bool checked)
{
    if (!m_audio) return;
    if (m_mode != ChainMode::Tx) {
        // RX mode: no DSP yet.  Leave the button in whatever state
        // the user clicked — it's a no-op until the RX chain ships.
        return;
    }

    // Dispatch on stage so we can route to the right engine getter
    // and settings-save function in one place.  Returns true if the
    // stage is currently enabled.
    auto setStageEnabled = [&](AudioEngine::TxChainStage stage, bool on) {
        switch (stage) {
            case AudioEngine::TxChainStage::Eq:
                if (auto* d = m_audio->clientEqTx()) {
                    d->setEnabled(on);
                    m_audio->saveClientEqSettings();
                }
                break;
            case AudioEngine::TxChainStage::Comp:
                if (auto* d = m_audio->clientCompTx()) {
                    d->setEnabled(on);
                    m_audio->saveClientCompSettings();
                }
                break;
            case AudioEngine::TxChainStage::Gate:
                if (auto* d = m_audio->clientGateTx()) {
                    d->setEnabled(on);
                    m_audio->saveClientGateSettings();
                }
                break;
            case AudioEngine::TxChainStage::DeEss:
                if (auto* d = m_audio->clientDeEssTx()) {
                    d->setEnabled(on);
                    m_audio->saveClientDeEssSettings();
                }
                break;
            case AudioEngine::TxChainStage::Tube:
                if (auto* d = m_audio->clientTubeTx()) {
                    d->setEnabled(on);
                    m_audio->saveClientTubeSettings();
                }
                break;
            case AudioEngine::TxChainStage::Enh:   // PUDU slot
                if (auto* d = m_audio->clientPuduTx()) {
                    d->setEnabled(on);
                    m_audio->saveClientPuduSettings();
                }
                break;
            case AudioEngine::TxChainStage::Reverb:
                if (auto* d = m_audio->clientReverbTx()) {
                    d->setEnabled(on);
                    m_audio->saveClientReverbSettings();
                }
                break;
            case AudioEngine::TxChainStage::None:
                break;
        }
        emit stageEnabledChanged(stage, on);
    };

    auto isEnabled = [&](AudioEngine::TxChainStage stage) {
        switch (stage) {
            case AudioEngine::TxChainStage::Eq:
                return m_audio->clientEqTx() && m_audio->clientEqTx()->isEnabled();
            case AudioEngine::TxChainStage::Comp:
                return m_audio->clientCompTx() && m_audio->clientCompTx()->isEnabled();
            case AudioEngine::TxChainStage::Gate:
                return m_audio->clientGateTx() && m_audio->clientGateTx()->isEnabled();
            case AudioEngine::TxChainStage::DeEss:
                return m_audio->clientDeEssTx() && m_audio->clientDeEssTx()->isEnabled();
            case AudioEngine::TxChainStage::Tube:
                return m_audio->clientTubeTx() && m_audio->clientTubeTx()->isEnabled();
            case AudioEngine::TxChainStage::Enh:
                return m_audio->clientPuduTx() && m_audio->clientPuduTx()->isEnabled();
            case AudioEngine::TxChainStage::Reverb:
                return m_audio->clientReverbTx() && m_audio->clientReverbTx()->isEnabled();
            case AudioEngine::TxChainStage::None:
                return false;
        }
        return false;
    };

    static const QVector<AudioEngine::TxChainStage> kAllStages{
        AudioEngine::TxChainStage::Eq,
        AudioEngine::TxChainStage::Comp,
        AudioEngine::TxChainStage::Gate,
        AudioEngine::TxChainStage::DeEss,
        AudioEngine::TxChainStage::Tube,
        AudioEngine::TxChainStage::Enh,
        AudioEngine::TxChainStage::Reverb,
    };

    if (checked) {
        // Snapshot whatever was on, then kill them all.
        m_bypassSnapshot.clear();
        for (auto s : kAllStages) {
            if (isEnabled(s)) {
                m_bypassSnapshot.append(s);
                setStageEnabled(s, false);
            }
        }
    } else {
        // Restore only the stages we had on before.  If the user
        // flipped anything on manually while bypass was active,
        // those survive here because they're not in the snapshot.
        for (auto s : m_bypassSnapshot) setStageEnabled(s, true);
        m_bypassSnapshot.clear();
    }

    if (m_chain) m_chain->update();
}

} // namespace AetherSDR
