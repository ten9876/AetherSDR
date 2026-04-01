#ifdef HAVE_MIDI

#include "MidiControlManager.h"

#include <RtMidi.h>
#include <QDebug>
#include <cmath>

namespace AetherSDR {

// ── MidiBinding helpers ─────────────────────────────────────────────────────

QString MidiBinding::sourceDisplayName() const
{
    QString typeName;
    switch (msgType) {
    case CC:       typeName = QString("CC #%1").arg(number); break;
    case NoteOn:   typeName = QString("Note %1").arg(number); break;
    case NoteOff:  typeName = QString("NoteOff %1").arg(number); break;
    case PitchBend: typeName = "Pitch Bend"; break;
    }
    if (channel >= 0)
        return QString("Ch %1 %2").arg(channel + 1).arg(typeName);
    return typeName;
}

// ── MidiControlManager ──────────────────────────────────────────────────────

MidiControlManager::MidiControlManager(QObject* parent)
    : QObject(parent)
    , m_hotplugTimer(new QTimer(this))
{
    m_hotplugTimer->setInterval(5000);
    connect(m_hotplugTimer, &QTimer::timeout, this, [this] {
        // Hotplug: if disconnected and we have a saved port name, try to reconnect
        if (m_midiIn && m_midiIn->isPortOpen()) return;
        if (m_portName.isEmpty()) return;
        openPortByName(m_portName);
    });
}

MidiControlManager::~MidiControlManager()
{
    closePort();
}

// ── Device management ───────────────────────────────────────────────────────

QStringList MidiControlManager::availablePorts() const
{
    QStringList result;
    try {
        RtMidiIn probe;
        unsigned int n = probe.getPortCount();
        for (unsigned int i = 0; i < n; ++i)
            result.append(QString::fromStdString(probe.getPortName(i)));
    } catch (const RtMidiError& e) {
        qWarning() << "MidiControlManager: error enumerating ports:" << e.what();
    }
    return result;
}

bool MidiControlManager::openPort(int portIndex)
{
    closePort();
    try {
        m_midiIn = std::make_unique<RtMidiIn>();
        if (static_cast<unsigned>(portIndex) >= m_midiIn->getPortCount()) {
            qWarning() << "MidiControlManager: port index" << portIndex << "out of range";
            m_midiIn.reset();
            return false;
        }
        m_portName = QString::fromStdString(m_midiIn->getPortName(portIndex));
        m_midiIn->openPort(portIndex);
        m_midiIn->setCallback(&rtmidiCallback, this);
        m_midiIn->ignoreTypes(true, true, true); // ignore sysex, timing, active sensing
        qDebug() << "MidiControlManager: opened port" << m_portName;
        m_hotplugTimer->start();
        emit portOpened(m_portName);
        return true;
    } catch (const RtMidiError& e) {
        qWarning() << "MidiControlManager: open failed:" << e.what();
        m_midiIn.reset();
        emit portError(QString::fromStdString(e.getMessage()));
        return false;
    }
}

bool MidiControlManager::openPortByName(const QString& portName)
{
    try {
        RtMidiIn probe;
        unsigned int n = probe.getPortCount();
        for (unsigned int i = 0; i < n; ++i) {
            if (QString::fromStdString(probe.getPortName(i)).contains(portName, Qt::CaseInsensitive))
                return openPort(static_cast<int>(i));
        }
    } catch (const RtMidiError&) {}
    return false;
}

void MidiControlManager::closePort()
{
    m_hotplugTimer->stop();
    if (m_midiIn) {
        try {
            if (m_midiIn->isPortOpen())
                m_midiIn->closePort();
        } catch (...) {}
        m_midiIn.reset();
    }
    emit portClosed();
}

bool MidiControlManager::isOpen() const
{
    return m_midiIn && m_midiIn->isPortOpen();
}

// ── Parameter registry ──────────────────────────────────────────────────────

void MidiControlManager::registerParam(const MidiParam& param)
{
    m_paramIndex[param.id] = m_params.size();
    m_params.append(param);
}

const MidiParam* MidiControlManager::findParam(const QString& id) const
{
    auto it = m_paramIndex.find(id);
    if (it == m_paramIndex.end()) return nullptr;
    return &m_params[it.value()];
}

// ── Binding management ──────────────────────────────────────────────────────

void MidiControlManager::addBinding(const MidiBinding& binding)
{
    // Remove any existing binding for this param
    removeBinding(binding.paramId);
    m_bindings.append(binding);
    rebuildIndex();
}

void MidiControlManager::removeBinding(const QString& paramId)
{
    for (int i = m_bindings.size() - 1; i >= 0; --i) {
        if (m_bindings[i].paramId == paramId) {
            m_bindings.removeAt(i);
        }
    }
    rebuildIndex();
}

void MidiControlManager::clearBindings()
{
    m_bindings.clear();
    m_bindingIndex.clear();
}

void MidiControlManager::rebuildIndex()
{
    m_bindingIndex.clear();
    for (int i = 0; i < m_bindings.size(); ++i)
        m_bindingIndex[m_bindings[i].key()] = i;
}

// ── MIDI Learn ──────────────────────────────────────────────────────────────

void MidiControlManager::startLearn(const QString& paramId)
{
    m_learning = true;
    m_learnParamId = paramId;
    qDebug() << "MidiControlManager: learning for" << paramId;
}

void MidiControlManager::cancelLearn()
{
    m_learning = false;
    m_learnParamId.clear();
    emit learnCancelled();
}

// ── RtMidi callback → Qt main thread ────────────────────────────────────────

void MidiControlManager::rtmidiCallback(double /*deltatime*/,
                                         std::vector<unsigned char>* message,
                                         void* userData)
{
    if (!message || message->size() < 2) return;
    auto* self = static_cast<MidiControlManager*>(userData);
    int status = (*message)[0];
    int data1 = (*message)[1];
    int data2 = message->size() > 2 ? (*message)[2] : 0;

    // Bridge to Qt main thread
    QMetaObject::invokeMethod(self, [self, status, data1, data2]() {
        self->onMidiMessage(status, data1, data2);
    }, Qt::QueuedConnection);
}

void MidiControlManager::onMidiMessage(int status, int data1, int data2)
{
    int channel = status & 0x0F;
    int type = status & 0xF0;

    // Determine message type and normalized value
    MidiBinding::MsgType msgType;
    int number = data1;
    float normValue = 0.0f;

    if (type == 0xB0) {          // Control Change
        msgType = MidiBinding::CC;
        normValue = data2 / 127.0f;
    } else if (type == 0x90) {   // Note On
        msgType = MidiBinding::NoteOn;
        normValue = data2 > 0 ? 1.0f : 0.0f;
    } else if (type == 0x80) {   // Note Off
        msgType = MidiBinding::NoteOff;
        normValue = 0.0f;
    } else if (type == 0xE0) {   // Pitch Bend
        msgType = MidiBinding::PitchBend;
        number = -1;
        normValue = (data1 | (data2 << 7)) / 16383.0f;
    } else {
        return; // ignore other message types
    }

    emit midiActivity(channel, static_cast<int>(msgType), number,
                      static_cast<int>(normValue * 127));

    // MIDI Learn mode — capture this message as a binding
    if (m_learning) {
        if (msgType == MidiBinding::NoteOff) return; // ignore NoteOff during learn

        MidiBinding binding;
        binding.channel = channel;
        binding.msgType = msgType;
        binding.number = (msgType == MidiBinding::PitchBend) ? -1 : data1;
        binding.paramId = m_learnParamId;
        binding.inverted = false;

        addBinding(binding);
        m_learning = false;
        qDebug() << "MidiControlManager: learned" << binding.sourceDisplayName()
                 << "→" << m_learnParamId;
        emit learnCompleted(m_learnParamId, binding);
        m_learnParamId.clear();
        return;
    }

    // Normal dispatch — look up binding and route value
    quint32 key = MidiBinding{channel, msgType, number, {}, false}.key();
    auto it = m_bindingIndex.find(key);

    // Try wildcard channel (-1 → 0xFF in key)
    if (it == m_bindingIndex.end()) {
        quint32 wildKey = (0xFF << 16) | (msgType << 8) |
                          ((msgType == MidiBinding::PitchBend) ? 0xFF : (number & 0x7F));
        it = m_bindingIndex.find(wildKey);
    }

    // For Gate params (CW key): NoteOff must also match NoteOn bindings
    if (it == m_bindingIndex.end() && msgType == MidiBinding::NoteOff) {
        quint32 noteOnKey = MidiBinding{channel, MidiBinding::NoteOn, number, {}, false}.key();
        it = m_bindingIndex.find(noteOnKey);
        if (it == m_bindingIndex.end()) {
            quint32 wildNoteOnKey = (0xFF << 16) | (MidiBinding::NoteOn << 8) | (number & 0x7F);
            it = m_bindingIndex.find(wildNoteOnKey);
        }
    }

    if (it == m_bindingIndex.end()) return;

    const auto& binding = m_bindings[it.value()];
    float value = binding.inverted ? (1.0f - normValue) : normValue;

    // Scale to parameter range
    const MidiParam* param = findParam(binding.paramId);
    if (param) {
        float scaled = param->rangeMin + value * (param->rangeMax - param->rangeMin);

        // For toggles: use > 0.5 threshold; for triggers: only fire on > 0.5
        if (param->type == MidiParamType::Toggle) {
            // NoteOn = toggle (sentinel -1 tells MainWindow to flip current state)
            // CC = direct threshold
            if (msgType == MidiBinding::NoteOn) {
                scaled = -1.0f;  // sentinel: MainWindow reads getter and toggles (#502)
            } else {
                scaled = (value > 0.5f) ? 1.0f : 0.0f;
            }
        } else if (param->type == MidiParamType::Trigger) {
            if (value < 0.5f) return; // only fire on press, not release
            scaled = 1.0f;
        } else if (param->type == MidiParamType::Gate) {
            // Gate: NoteOn vel>0 = key down (1.0), NoteOff or vel=0 = key up (0.0)
            // No toggling — direct follow of key state
            scaled = normValue > 0.0f ? 1.0f : 0.0f;
        }

        // Don't call setter directly — may be on a worker thread while
        // setters access main-thread objects. Emit signal instead. (#502)
        emit paramAction(binding.paramId, scaled);
    }

    emit paramValueChanged(binding.paramId, value);
}

} // namespace AetherSDR

#endif // HAVE_MIDI
