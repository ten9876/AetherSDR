#include "ClientReverbEditor.h"
#include "ClientCompKnob.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/ClientReverb.h"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QMoveEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace AetherSDR {

namespace {

constexpr const char* kWindowStyle =
    "QWidget { background: #08121d; color: #d7e7f2; }"
    "QLabel  { background: transparent; color: #8aa8c0; font-size: 11px; }";

} // namespace

ClientReverbEditor::ClientReverbEditor(AudioEngine* engine, QWidget* parent)
    : QWidget(parent, Qt::Window)
    , m_audio(engine)
{
    setWindowTitle(QString::fromUtf8("Client Reverb"));
    setStyleSheet(kWindowStyle);
    resize(480, 180);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 14, 16, 14);
    root->setSpacing(8);

    auto* row = new QHBoxLayout;
    row->setSpacing(12);
    row->addStretch();

    auto makeKnob = [](const QString& label) {
        auto* k = new ClientCompKnob;
        k->setLabel(label);
        k->setCenterLabelMode(true);
        k->setFixedSize(76, 76);
        return k;
    };

    m_size = makeKnob("Size");
    m_size->setRange(0.0f, 1.0f);
    m_size->setDefault(0.5f);
    m_size->setValueFromNorm([](float n) { return n; });
    m_size->setNormFromValue([](float v) { return v; });
    m_size->setLabelFormat([](float v) {
        return QString::number(static_cast<int>(v * 100.0f + 0.5f)) + " %";
    });
    row->addWidget(m_size);

    m_decay = makeKnob("Decay");
    m_decay->setRange(0.3f, 5.0f);
    m_decay->setDefault(1.2f);
    m_decay->setValueFromNorm([](float n) {
        return 0.3f * std::pow(5.0f / 0.3f, n);
    });
    m_decay->setNormFromValue([](float v) {
        if (v <= 0.3f) return 0.0f;
        return std::log(v / 0.3f) / std::log(5.0f / 0.3f);
    });
    m_decay->setLabelFormat([](float v) {
        return QString::number(v, 'f', 2) + " s";
    });
    row->addWidget(m_decay);

    m_damping = makeKnob("Damp");
    m_damping->setRange(0.0f, 1.0f);
    m_damping->setDefault(0.5f);
    m_damping->setValueFromNorm([](float n) { return n; });
    m_damping->setNormFromValue([](float v) { return v; });
    m_damping->setLabelFormat([](float v) {
        return QString::number(static_cast<int>(v * 100.0f + 0.5f)) + " %";
    });
    row->addWidget(m_damping);

    m_preDly = makeKnob("PreDly");
    m_preDly->setRange(0.0f, 100.0f);
    m_preDly->setDefault(20.0f);
    m_preDly->setValueFromNorm([](float n) { return n * 100.0f; });
    m_preDly->setNormFromValue([](float v) { return v / 100.0f; });
    m_preDly->setLabelFormat([](float v) {
        return QString::number(v, 'f', 0) + " ms";
    });
    row->addWidget(m_preDly);

    m_mix = makeKnob("Mix");
    m_mix->setRange(0.0f, 1.0f);
    m_mix->setDefault(0.15f);
    m_mix->setValueFromNorm([](float n) { return n; });
    m_mix->setNormFromValue([](float v) { return v; });
    m_mix->setLabelFormat([](float v) {
        return QString::number(static_cast<int>(v * 100.0f + 0.5f)) + " %";
    });
    row->addWidget(m_mix);

    row->addStretch();
    root->addLayout(row);

    auto wire = [this](ClientCompKnob* k, auto setter) {
        connect(k, &ClientCompKnob::valueChanged, this, [this, setter](float v) {
            if (!m_audio || !m_audio->clientReverbTx()) return;
            (m_audio->clientReverbTx()->*setter)(v);
            m_audio->saveClientReverbSettings();
        });
    };
    wire(m_size,    &ClientReverb::setSize);
    wire(m_decay,   &ClientReverb::setDecayS);
    wire(m_damping, &ClientReverb::setDamping);
    wire(m_preDly,  &ClientReverb::setPreDelayMs);
    wire(m_mix,     &ClientReverb::setMix);

    syncControlsFromEngine();

    m_syncTimer = new QTimer(this);
    m_syncTimer->setInterval(33);
    connect(m_syncTimer, &QTimer::timeout,
            this, &ClientReverbEditor::syncControlsFromEngine);
}

ClientReverbEditor::~ClientReverbEditor() = default;

void ClientReverbEditor::showForTx()
{
    syncControlsFromEngine();
    restoreGeometryFromSettings();
    show();
    raise();
    activateWindow();
    if (m_syncTimer) m_syncTimer->start();
}

void ClientReverbEditor::syncControlsFromEngine()
{
    if (!m_audio || !m_audio->clientReverbTx()) return;
    ClientReverb* r = m_audio->clientReverbTx();
    m_restoring = true;
    if (m_size)    { QSignalBlocker b(m_size);    m_size->setValue(r->size()); }
    if (m_decay)   { QSignalBlocker b(m_decay);   m_decay->setValue(r->decayS()); }
    if (m_damping) { QSignalBlocker b(m_damping); m_damping->setValue(r->damping()); }
    if (m_preDly)  { QSignalBlocker b(m_preDly);  m_preDly->setValue(r->preDelayMs()); }
    if (m_mix)     { QSignalBlocker b(m_mix);     m_mix->setValue(r->mix()); }
    m_restoring = false;
}

void ClientReverbEditor::saveGeometryToSettings()
{
    auto& s = AppSettings::instance();
    s.setValue("ClientReverbEditorGeometry", saveGeometry().toBase64());
}

void ClientReverbEditor::restoreGeometryFromSettings()
{
    auto& s = AppSettings::instance();
    const QByteArray geom = QByteArray::fromBase64(
        s.value("ClientReverbEditorGeometry", "").toByteArray());
    if (!geom.isEmpty()) {
        m_restoring = true;
        restoreGeometry(geom);
        m_restoring = false;
    }
}

void ClientReverbEditor::closeEvent(QCloseEvent* ev)
{
    if (m_syncTimer) m_syncTimer->stop();
    saveGeometryToSettings();
    QWidget::closeEvent(ev);
}

void ClientReverbEditor::moveEvent(QMoveEvent* ev)
{
    QWidget::moveEvent(ev);
    if (!m_restoring) saveGeometryToSettings();
}

void ClientReverbEditor::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    if (!m_restoring) saveGeometryToSettings();
}

void ClientReverbEditor::showEvent(QShowEvent* ev)
{
    QWidget::showEvent(ev);
    if (m_syncTimer) m_syncTimer->start();
}

void ClientReverbEditor::hideEvent(QHideEvent* ev)
{
    QWidget::hideEvent(ev);
    if (m_syncTimer) m_syncTimer->stop();
}

} // namespace AetherSDR
