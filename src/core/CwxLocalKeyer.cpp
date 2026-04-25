#include "CwxLocalKeyer.h"

#include <QHash>

namespace AetherSDR {

namespace {

// Standard ITU Morse table.  Lowercase keys; we uppercase incoming text
// before lookup.  Punctuation mirrors what FlexRadio's CWX accepts on
// the wire; if we encounter an unknown character we just emit a word
// gap so the message stays time-aligned with the radio.
const QHash<QChar, QString>& morseTable()
{
    static const QHash<QChar, QString> t = {
        {'A', ".-"},    {'B', "-..."},  {'C', "-.-."},  {'D', "-.."},
        {'E', "."},     {'F', "..-."},  {'G', "--."},   {'H', "...."},
        {'I', ".."},    {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
        {'M', "--"},    {'N', "-."},    {'O', "---"},   {'P', ".--."},
        {'Q', "--.-"},  {'R', ".-."},   {'S', "..."},   {'T', "-"},
        {'U', "..-"},   {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
        {'Y', "-.--"},  {'Z', "--.."},
        {'0', "-----"}, {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
        {'4', "....-"}, {'5', "....."}, {'6', "-...."}, {'7', "--..."},
        {'8', "---.."}, {'9', "----."},
        {'.', ".-.-.-"},{',', "--..--"},{'?', "..--.."},{'\'',".----."},
        {'!', "-.-.--"},{'/', "-..-."}, {'(', "-.--."}, {')', "-.--.-"},
        {'&', ".-..."}, {':', "---..."},{';', "-.-.-."},{'=', "-...-"},
        {'+', ".-.-."}, {'-', "-....-"},{'_', "..--.-"},{'"', ".-..-."},
        {'$', "...-..-"},{'@', ".--.-."},
    };
    return t;
}

} // namespace

CwxLocalKeyer::CwxLocalKeyer(QObject* parent) : QObject(parent)
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    QObject::connect(&m_timer, &QTimer::timeout, this, &CwxLocalKeyer::onTick);
}

void CwxLocalKeyer::start(const QString& text, int wpm)
{
    if (text.isEmpty() || wpm <= 0) return;
    m_queue.enqueue({text, wpm});
    if (!m_running)
        scheduleNext();
}

void CwxLocalKeyer::stop()
{
    m_timer.stop();
    m_queue.clear();
    m_elements.clear();
    m_running = false;
    if (m_currentlyDown) {
        m_currentlyDown = false;
        emit keyStateChanged(false);
    }
}

void CwxLocalKeyer::encode(const QString& text, int wpm)
{
    // Standard CW timing: 1 unit = 1.2 / WPM seconds.  At 20 WPM →
    // 60 ms/unit; matches the FlexRadio keyer's output within ±1 ms.
    m_unitMs = qMax(1, static_cast<int>(1200.0 / wpm));

    const auto& tbl = morseTable();
    bool firstChar = true;
    for (QChar ch : text.toUpper()) {
        if (ch == ' ') {
            // Word gap = 7 units total.  Already have CharGap (3 units)
            // queued from the previous character; subtract that and emit
            // a 4-unit WordGap so total spacing reads as 7.
            if (!m_elements.isEmpty() && m_elements.back() == Element::CharGap)
                m_elements.removeLast();
            m_elements.enqueue(Element::WordGap);
            firstChar = true;
            continue;
        }
        const auto it = tbl.find(ch);
        if (it == tbl.end()) continue;
        if (!firstChar)
            m_elements.enqueue(Element::CharGap);
        firstChar = false;
        const QString& pattern = it.value();
        for (int i = 0; i < pattern.size(); ++i) {
            if (i > 0)
                m_elements.enqueue(Element::ElementGap);
            m_elements.enqueue(pattern[i] == '.' ? Element::Dit : Element::Dah);
        }
    }
}

void CwxLocalKeyer::scheduleNext()
{
    if (m_elements.isEmpty()) {
        if (m_queue.isEmpty()) {
            m_running = false;
            if (m_currentlyDown) {
                m_currentlyDown = false;
                emit keyStateChanged(false);
            }
            return;
        }
        const Pending p = m_queue.dequeue();
        encode(p.text, p.wpm);
        if (m_elements.isEmpty()) {
            scheduleNext();
            return;
        }
    }

    m_running = true;
    const Element e = m_elements.dequeue();
    int durationMs = m_unitMs;
    bool keyDownNext = false;
    switch (e) {
    case Element::Dit:        durationMs = m_unitMs;     keyDownNext = true;  break;
    case Element::Dah:        durationMs = m_unitMs * 3; keyDownNext = true;  break;
    case Element::ElementGap: durationMs = m_unitMs;     keyDownNext = false; break;
    case Element::CharGap:    durationMs = m_unitMs * 3; keyDownNext = false; break;
    case Element::WordGap:    durationMs = m_unitMs * 4; keyDownNext = false; break;
    }
    if (keyDownNext != m_currentlyDown) {
        m_currentlyDown = keyDownNext;
        emit keyStateChanged(keyDownNext);
    }
    m_timer.start(durationMs);
}

void CwxLocalKeyer::onTick()
{
    scheduleNext();
}

} // namespace AetherSDR
