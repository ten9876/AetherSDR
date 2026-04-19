#include "ClientCompEditor.h"
#include "ClientCompEditorCanvas.h"
#include "ClientCompKnob.h"
#include "ClientCompMeter.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/ClientComp.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QMoveEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr int kDefaultWidth  = 860;
constexpr int kDefaultHeight = 400;

constexpr const char* kWindowStyle =
    "QWidget { background: #08121d; color: #d7e7f2; }"
    "QLabel  { background: transparent; color: #8aa8c0; font-size: 11px; }";

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

const QString kLimBtnStyle = QStringLiteral(
    "QPushButton {"
    "  background: #1a2a3a; border: 1px solid #205070; border-radius: 3px;"
    "  color: #c8d8e8; font-size: 10px; font-weight: bold; padding: 3px 6px;"
    "}"
    "QPushButton:hover { background: #204060; }"
    "QPushButton:checked {"
    "  background: #006040; color: #00ff88; border: 1px solid #00a060;"
    "}");

} // namespace

ClientCompEditor::ClientCompEditor(AudioEngine* engine, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_audio(engine)
{
    setWindowTitle("Client Compressor");
    setStyleSheet(kWindowStyle);
    resize(kDefaultWidth, kDefaultHeight);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(6);

    // ── Header strip: bypass + chain-order segmented picker ─────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);

        m_bypass = new QPushButton("Bypass");
        m_bypass->setCheckable(true);
        m_bypass->setStyleSheet(kBypassStyle);
        m_bypass->setFixedHeight(22);
        m_bypass->setToolTip("Bypass the compressor (signal passes through unshaped)");
        row->addWidget(m_bypass);

        row->addStretch();

        auto* lbl = new QLabel("Chain:");
        row->addWidget(lbl);

        m_chainOrder = new QComboBox;
        m_chainOrder->addItem("CMP → EQ");
        m_chainOrder->addItem("EQ → CMP");
        m_chainOrder->setToolTip(
            "Signal flow order from the mic to the radio. "
            "CMP→EQ colours the raw mic first then shapes it. "
            "EQ→CMP shapes first then tames peaks.");
        row->addWidget(m_chainOrder);

        root->addLayout(row);
    }

    // ── Main body: knobs column | canvas | meters | makeup/limiter ──
    {
        auto* body = new QHBoxLayout;
        body->setSpacing(8);

        // Knobs column (Ratio / Attack / Release / Knee).
        {
            auto* col = new QVBoxLayout;
            col->setSpacing(4);

            m_ratio = new ClientCompKnob;
            m_ratio->setLabel("Ratio");
            m_ratio->setRange(1.0f, 20.0f);
            m_ratio->setDefault(3.0f);
            m_ratio->setValueFromNorm([](float n) {
                // Exponential — 1:1 at 0, 20:1 at 1, with a gentle curve
                // so the middle of the knob lands near 4:1.
                return 1.0f * std::pow(20.0f, n);
            });
            m_ratio->setNormFromValue([](float v) {
                if (v <= 1.0f) return 0.0f;
                return std::log(v) / std::log(20.0f);
            });
            m_ratio->setLabelFormat([](float v) {
                return QString::number(v, 'f', 2) + " :1";
            });
            col->addWidget(m_ratio);

            m_attack = new ClientCompKnob;
            m_attack->setLabel("Attack");
            m_attack->setRange(0.1f, 300.0f);
            m_attack->setDefault(20.0f);
            m_attack->setValueFromNorm([](float n) {
                return 0.1f * std::pow(3000.0f, n);  // 0.1 .. 300 ms
            });
            m_attack->setNormFromValue([](float v) {
                if (v <= 0.1f) return 0.0f;
                return std::log(v / 0.1f) / std::log(3000.0f);
            });
            m_attack->setLabelFormat([](float v) {
                return v < 10.0f ? QString::number(v, 'f', 1) + " ms"
                                  : QString::number(v, 'f', 0) + " ms";
            });
            col->addWidget(m_attack);

            m_release = new ClientCompKnob;
            m_release->setLabel("Release");
            m_release->setRange(5.0f, 2000.0f);
            m_release->setDefault(200.0f);
            m_release->setValueFromNorm([](float n) {
                return 5.0f * std::pow(400.0f, n);    // 5 .. 2000 ms
            });
            m_release->setNormFromValue([](float v) {
                if (v <= 5.0f) return 0.0f;
                return std::log(v / 5.0f) / std::log(400.0f);
            });
            m_release->setLabelFormat([](float v) {
                return QString::number(v, 'f', 0) + " ms";
            });
            col->addWidget(m_release);

            m_knee = new ClientCompKnob;
            m_knee->setLabel("Knee");
            m_knee->setRange(0.0f, 24.0f);
            m_knee->setDefault(6.0f);
            m_knee->setLabelFormat([](float v) {
                return QString::number(v, 'f', 1) + " dB";
            });
            col->addWidget(m_knee);

            col->addStretch();
            body->addLayout(col);
        }

        // Threshold mini-column: one tall slider view + numeric label.
        // The interactive canvas owns the real threshold handle; this
        // label just echoes the current value so the user can read the
        // exact number at a glance.
        {
            auto* col = new QVBoxLayout;
            col->setSpacing(2);
            auto* hdr = new QLabel("Thresh");
            hdr->setAlignment(Qt::AlignCenter);
            col->addWidget(hdr);
            m_thresholdLabel = new QLabel("-18.0 dB");
            m_thresholdLabel->setAlignment(Qt::AlignCenter);
            m_thresholdLabel->setStyleSheet(
                "QLabel { color: #e8e8e8; font-size: 11px; font-weight: bold; }");
            col->addWidget(m_thresholdLabel);

            m_inputMeter = new ClientCompMeter;
            m_inputMeter->setMode(ClientCompMeter::Mode::Level);
            m_inputMeter->setLabel("In");
            m_inputMeter->setMinimumWidth(20);
            col->addWidget(m_inputMeter, 1);
            body->addLayout(col);
        }

        // Canvas (center).
        m_canvas = new ClientCompEditorCanvas;
        body->addWidget(m_canvas, 1);

        // GR + Output meters.
        {
            auto* col = new QVBoxLayout;
            col->setSpacing(2);

            m_grMeter = new ClientCompMeter;
            m_grMeter->setMode(ClientCompMeter::Mode::GainReduction);
            m_grMeter->setLabel("GR");
            m_grMeter->setMinimumWidth(20);

            m_outputMeter = new ClientCompMeter;
            m_outputMeter->setMode(ClientCompMeter::Mode::Level);
            m_outputMeter->setLabel("Out");
            m_outputMeter->setMinimumWidth(20);

            col->addWidget(m_grMeter, 1);
            body->addLayout(col);

            auto* col2 = new QVBoxLayout;
            col2->setSpacing(2);
            col2->addWidget(m_outputMeter, 1);
            body->addLayout(col2);
        }

        // Limiter + Makeup column.
        {
            auto* col = new QVBoxLayout;
            col->setSpacing(6);

            m_limiterEnable = new QPushButton("LIMIT");
            m_limiterEnable->setCheckable(true);
            m_limiterEnable->setStyleSheet(kLimBtnStyle);
            m_limiterEnable->setFixedHeight(22);
            m_limiterEnable->setToolTip(
                "Brickwall peak limiter on the compressor output");
            col->addWidget(m_limiterEnable);

            m_ceiling = new ClientCompKnob;
            m_ceiling->setLabel("Ceiling");
            m_ceiling->setRange(-24.0f, 0.0f);
            m_ceiling->setDefault(-1.0f);
            m_ceiling->setLabelFormat([](float v) {
                return QString::number(v, 'f', 1) + " dB";
            });
            col->addWidget(m_ceiling);

            m_makeup = new ClientCompKnob;
            m_makeup->setLabel("Makeup");
            m_makeup->setRange(-12.0f, 24.0f);
            m_makeup->setDefault(0.0f);
            m_makeup->setLabelFormat([](float v) {
                return (v >= 0.0f ? "+" : "") + QString::number(v, 'f', 1) + " dB";
            });
            col->addWidget(m_makeup);
            col->addStretch();
            body->addLayout(col);
        }

        root->addLayout(body, 1);
    }

    // ── Signal wiring ───────────────────────────────────────────────
    if (m_audio && m_audio->clientCompTx()) {
        m_canvas->setComp(m_audio->clientCompTx());
    }

    connect(m_bypass, &QPushButton::toggled, this, [this](bool checked) {
        if (!m_audio) return;
        ClientComp* c = m_audio->clientCompTx();
        if (!c) return;
        // "Bypass checked" means the effect is bypassed → comp disabled.
        c->setEnabled(!checked);
        m_audio->saveClientCompSettings();
        emit bypassToggled(checked);
        if (m_canvas) m_canvas->update();
    });

    connect(m_chainOrder,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (!m_audio) return;
        m_audio->setTxChainOrder(
            idx == 1 ? AudioEngine::TxChainOrder::EqThenComp
                     : AudioEngine::TxChainOrder::CompThenEq);
    });

    connect(m_canvas, &ClientCompEditorCanvas::thresholdChanged,
            this, &ClientCompEditor::applyThreshold);
    connect(m_canvas, &ClientCompEditorCanvas::ratioChanged,
            this, &ClientCompEditor::applyRatio);

    connect(m_ratio,   &ClientCompKnob::valueChanged,
            this, &ClientCompEditor::applyRatio);
    connect(m_attack,  &ClientCompKnob::valueChanged,
            this, &ClientCompEditor::applyAttack);
    connect(m_release, &ClientCompKnob::valueChanged,
            this, &ClientCompEditor::applyRelease);
    connect(m_knee,    &ClientCompKnob::valueChanged,
            this, &ClientCompEditor::applyKnee);
    connect(m_makeup,  &ClientCompKnob::valueChanged,
            this, &ClientCompEditor::applyMakeup);
    connect(m_ceiling, &ClientCompKnob::valueChanged,
            this, &ClientCompEditor::applyLimiterCeiling);
    connect(m_limiterEnable, &QPushButton::toggled,
            this, &ClientCompEditor::applyLimiterEnabled);

    syncControlsFromEngine();

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(33);  // ~30 Hz
    connect(m_meterTimer, &QTimer::timeout,
            this, &ClientCompEditor::tickMeters);
}

ClientCompEditor::~ClientCompEditor() = default;

void ClientCompEditor::showForTx()
{
    restoreGeometryFromSettings();
    syncControlsFromEngine();
    if (m_meterTimer) m_meterTimer->start();
    show();
    raise();
    activateWindow();
}

void ClientCompEditor::syncControlsFromEngine()
{
    if (!m_audio) return;
    ClientComp* c = m_audio->clientCompTx();
    if (!c) return;
    QSignalBlocker bb(m_bypass);
    QSignalBlocker bco(m_chainOrder);
    QSignalBlocker br(m_ratio);
    QSignalBlocker ba(m_attack);
    QSignalBlocker brl(m_release);
    QSignalBlocker bk(m_knee);
    QSignalBlocker bm(m_makeup);
    QSignalBlocker bce(m_ceiling);
    QSignalBlocker ble(m_limiterEnable);

    m_bypass->setChecked(!c->isEnabled());
    m_chainOrder->setCurrentIndex(
        m_audio->txChainOrder() == AudioEngine::TxChainOrder::EqThenComp ? 1 : 0);
    m_ratio->setValue(c->ratio());
    m_attack->setValue(c->attackMs());
    m_release->setValue(c->releaseMs());
    m_knee->setValue(c->kneeDb());
    m_makeup->setValue(c->makeupDb());
    m_ceiling->setValue(c->limiterCeilingDb());
    m_limiterEnable->setChecked(c->limiterEnabled());
    m_thresholdLabel->setText(
        QString::number(c->thresholdDb(), 'f', 1) + " dB");
    if (m_canvas) m_canvas->update();
}

void ClientCompEditor::tickMeters()
{
    if (!m_audio) return;
    ClientComp* c = m_audio->clientCompTx();
    if (!c) return;
    if (m_inputMeter)  m_inputMeter ->setValueDb(c->inputPeakDb());
    if (m_grMeter)     m_grMeter    ->setValueDb(c->gainReductionDb());
    if (m_outputMeter) m_outputMeter->setValueDb(c->outputPeakDb());
}

void ClientCompEditor::applyThreshold(float db)
{
    if (!m_audio) return;
    ClientComp* c = m_audio->clientCompTx();
    if (!c) return;
    c->setThresholdDb(db);
    m_audio->saveClientCompSettings();
    if (m_thresholdLabel)
        m_thresholdLabel->setText(QString::number(db, 'f', 1) + " dB");
    if (m_canvas) m_canvas->update();
}

void ClientCompEditor::applyRatio(float ratio)
{
    if (!m_audio) return;
    ClientComp* c = m_audio->clientCompTx();
    if (!c) return;
    c->setRatio(ratio);
    m_audio->saveClientCompSettings();
    // Mirror to the knob if the change came from the canvas.
    if (m_ratio && std::fabs(m_ratio->value() - ratio) > 0.001f) {
        QSignalBlocker b(m_ratio);
        m_ratio->setValue(ratio);
    }
    if (m_canvas) m_canvas->update();
}

void ClientCompEditor::applyAttack(float ms)
{
    if (!m_audio) return;
    if (auto* c = m_audio->clientCompTx()) c->setAttackMs(ms);
    m_audio->saveClientCompSettings();
}

void ClientCompEditor::applyRelease(float ms)
{
    if (!m_audio) return;
    if (auto* c = m_audio->clientCompTx()) c->setReleaseMs(ms);
    m_audio->saveClientCompSettings();
}

void ClientCompEditor::applyKnee(float db)
{
    if (!m_audio) return;
    if (auto* c = m_audio->clientCompTx()) c->setKneeDb(db);
    m_audio->saveClientCompSettings();
    if (m_canvas) m_canvas->update();
}

void ClientCompEditor::applyMakeup(float db)
{
    if (!m_audio) return;
    if (auto* c = m_audio->clientCompTx()) c->setMakeupDb(db);
    m_audio->saveClientCompSettings();
    if (m_canvas) m_canvas->update();
}

void ClientCompEditor::applyLimiterEnabled(bool on)
{
    if (!m_audio) return;
    if (auto* c = m_audio->clientCompTx()) c->setLimiterEnabled(on);
    m_audio->saveClientCompSettings();
}

void ClientCompEditor::applyLimiterCeiling(float db)
{
    if (!m_audio) return;
    if (auto* c = m_audio->clientCompTx()) c->setLimiterCeilingDb(db);
    m_audio->saveClientCompSettings();
}

void ClientCompEditor::saveGeometryToSettings()
{
    auto& s = AppSettings::instance();
    s.setValue("ClientCompEditorGeometry", saveGeometry().toBase64());
}

void ClientCompEditor::restoreGeometryFromSettings()
{
    auto& s = AppSettings::instance();
    const QByteArray geom = QByteArray::fromBase64(
        s.value("ClientCompEditorGeometry", "").toByteArray());
    if (!geom.isEmpty()) {
        m_restoring = true;
        restoreGeometry(geom);
        m_restoring = false;
    }
}

void ClientCompEditor::closeEvent(QCloseEvent* ev)
{
    if (m_meterTimer) m_meterTimer->stop();
    saveGeometryToSettings();
    QWidget::closeEvent(ev);
}

void ClientCompEditor::moveEvent(QMoveEvent* ev)
{
    QWidget::moveEvent(ev);
    if (!m_restoring) saveGeometryToSettings();
}

void ClientCompEditor::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    if (!m_restoring) saveGeometryToSettings();
}

void ClientCompEditor::showEvent(QShowEvent* ev)
{
    QWidget::showEvent(ev);
    if (m_meterTimer) m_meterTimer->start();
}

void ClientCompEditor::hideEvent(QHideEvent* ev)
{
    QWidget::hideEvent(ev);
    if (m_meterTimer) m_meterTimer->stop();
}

} // namespace AetherSDR
