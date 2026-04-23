#include "ClientReverbApplet.h"
#include "ClientCompKnob.h"
#include "core/AudioEngine.h"
#include "core/ClientReverb.h"

#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>

namespace AetherSDR {

ClientReverbApplet::ClientReverbApplet(QWidget* parent) : QWidget(parent)
{
    buildUI();
    hide();
}

void ClientReverbApplet::buildUI()
{
    setStyleSheet("QWidget { background: transparent; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    row->addStretch();

    auto makeKnob = [](const QString& label) {
        auto* k = new ClientCompKnob;
        k->setLabel(label);
        k->setCenterLabelMode(true);
        k->setFixedSize(38, 48);
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
        // Exponential 0.3 → 5.0 (~16.7x)
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

    m_preDly = makeKnob("Pre");
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
    outer->addLayout(row);

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

    // 30 Hz sync with the floating editor.
    m_syncTimer = new QTimer(this);
    m_syncTimer->setInterval(33);
    connect(m_syncTimer, &QTimer::timeout,
            this, &ClientReverbApplet::syncKnobsFromEngine);
}

void ClientReverbApplet::setAudioEngine(AudioEngine* engine)
{
    m_audio = engine;
    if (!m_audio) return;
    syncKnobsFromEngine();
    if (m_syncTimer) m_syncTimer->start();
}

void ClientReverbApplet::refreshEnableFromEngine()
{
    syncKnobsFromEngine();
}

void ClientReverbApplet::syncKnobsFromEngine()
{
    if (!m_audio || !m_audio->clientReverbTx()) return;
    ClientReverb* r = m_audio->clientReverbTx();
    if (m_size)    { QSignalBlocker b(m_size);    m_size->setValue(r->size()); }
    if (m_decay)   { QSignalBlocker b(m_decay);   m_decay->setValue(r->decayS()); }
    if (m_damping) { QSignalBlocker b(m_damping); m_damping->setValue(r->damping()); }
    if (m_preDly)  { QSignalBlocker b(m_preDly);  m_preDly->setValue(r->preDelayMs()); }
    if (m_mix)     { QSignalBlocker b(m_mix);     m_mix->setValue(r->mix()); }
}

} // namespace AetherSDR
