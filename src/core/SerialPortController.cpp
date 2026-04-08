#include "SerialPortController.h"
#include "AppSettings.h"
#include "LogManager.h"

#ifdef HAVE_SERIALPORT
#include <QSerialPortInfo>
#endif

namespace AetherSDR {

SerialPortController::SerialPortController(QObject* parent)
    : QObject(parent)
{
#ifdef HAVE_SERIALPORT
    // Set this as parent so moveToThread() moves them with us.
    // Without this, m_port and m_pollTimer stay on the creating thread,
    // causing cross-thread QObject access that silently fails on macOS.
    m_port.setParent(this);
    m_pollTimer.setParent(this);
    connect(&m_pollTimer, &QTimer::timeout, this, &SerialPortController::pollInputPins);
#endif
}

SerialPortController::~SerialPortController()
{
    close();
}

bool SerialPortController::open(const QString& portName, int baudRate,
                                 int dataBits, int parity, int stopBits)
{
#ifdef HAVE_SERIALPORT
    if (m_port.isOpen()) close();

    m_port.setPortName(portName);
    m_port.setBaudRate(baudRate);
    m_port.setDataBits(static_cast<QSerialPort::DataBits>(dataBits));
    m_port.setParity(static_cast<QSerialPort::Parity>(parity));
    m_port.setStopBits(static_cast<QSerialPort::StopBits>(stopBits));
    m_port.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_port.open(QIODevice::ReadWrite)) {
        qCWarning(lcCat) << "SerialPortController: failed to open" << portName
                         << m_port.errorString();
        emit errorOccurred(m_port.errorString());
        return false;
    }

    // Start with all output lines deasserted
    m_port.setDataTerminalReady(!m_dtrActiveHigh);
    m_port.setRequestToSend(!m_rtsActiveHigh);

    // Reset input state
    m_lastCtsActive = false;
    m_lastDsrActive = false;
    m_debounceTimer.start();

    // Start polling if any input function is configured
    updatePolling();

    qCDebug(lcCat) << "SerialPortController: opened" << portName << "at" << baudRate;
    return true;
#else
    Q_UNUSED(portName); Q_UNUSED(baudRate);
    Q_UNUSED(dataBits); Q_UNUSED(parity); Q_UNUSED(stopBits);
    return false;
#endif
}

void SerialPortController::close()
{
#ifdef HAVE_SERIALPORT
    m_pollTimer.stop();
    if (m_port.isOpen()) {
        // Deassert all output lines before closing
        m_port.setDataTerminalReady(!m_dtrActiveHigh);
        m_port.setRequestToSend(!m_rtsActiveHigh);
        m_port.close();
        qCDebug(lcCat) << "SerialPortController: closed";
    }
#endif
}

bool SerialPortController::isOpen() const
{
#ifdef HAVE_SERIALPORT
    return m_port.isOpen();
#else
    return false;
#endif
}

QString SerialPortController::portName() const
{
#ifdef HAVE_SERIALPORT
    return m_port.portName();
#else
    return {};
#endif
}

void SerialPortController::setTransmitting(bool tx)
{
    applyPin(PinFunction::PTT, tx);
    applyPin(PinFunction::CwPTT, tx);
}

void SerialPortController::setCwKeyDown(bool down)
{
    applyPin(PinFunction::CwKey, down);
}

void SerialPortController::applyPin(PinFunction targetFn, bool active)
{
#ifdef HAVE_SERIALPORT
    if (!m_port.isOpen()) return;

    if (m_dtrFn == targetFn) {
        bool level = m_dtrActiveHigh ? active : !active;
        m_port.setDataTerminalReady(level);
    }
    if (m_rtsFn == targetFn) {
        bool level = m_rtsActiveHigh ? active : !active;
        m_port.setRequestToSend(level);
    }
#else
    Q_UNUSED(targetFn); Q_UNUSED(active);
#endif
}

void SerialPortController::updatePolling()
{
#ifdef HAVE_SERIALPORT
    bool needsPoll = (m_ctsFn != InputFunction::None || m_dsrFn != InputFunction::None);
    if (needsPoll && m_port.isOpen() && !m_pollTimer.isActive())
        m_pollTimer.start(POLL_INTERVAL_MS);
    else if (!needsPoll && m_pollTimer.isActive())
        m_pollTimer.stop();
#endif
}

#ifdef HAVE_SERIALPORT
void SerialPortController::pollInputPins()
{
    if (!m_port.isOpen()) return;

    auto pinState = m_port.pinoutSignals();
    bool cts = pinState.testFlag(QSerialPort::ClearToSendSignal);
    bool dsr = pinState.testFlag(QSerialPort::DataSetReadySignal);

    // Resolve active state per polarity
    bool ctsActive = m_ctsActiveHigh ? cts : !cts;
    bool dsrActive = m_dsrActiveHigh ? dsr : !dsr;

    // Debounce: ignore changes within DEBOUNCE_MS of the last change
    bool debounceOk = m_debounceTimer.elapsed() >= DEBOUNCE_MS;

    // ── PTT input ────────────────────────────────────────────────────────
    if (m_ctsFn == InputFunction::PttInput && ctsActive != m_lastCtsActive && debounceOk) {
        m_lastCtsActive = ctsActive;
        m_debounceTimer.restart();
        emit externalPttChanged(ctsActive);
    }
    if (m_dsrFn == InputFunction::PttInput && dsrActive != m_lastDsrActive && debounceOk) {
        m_lastDsrActive = dsrActive;
        m_debounceTimer.restart();
        emit externalPttChanged(dsrActive);
    }

    // ── CW straight key input ────────────────────────────────────────────
    // Either CTS or DSR can be a straight key — whichever is configured
    bool keyDown = false;
    bool hasKey = false;
    if (m_ctsFn == InputFunction::CwKeyInput) { keyDown = ctsActive; hasKey = true; }
    if (m_dsrFn == InputFunction::CwKeyInput) { keyDown = dsrActive; hasKey = true; }
    if (hasKey && keyDown != m_lastKeyDown) {
        m_lastKeyDown = keyDown;
        emit cwKeyChanged(keyDown);
    }

    // ── CW paddle (dit/dah) input ────────────────────────────────────────
    bool ditActive = false, dahActive = false;
    bool hasPaddle = false;
    if (m_ctsFn == InputFunction::CwDitInput) { ditActive = ctsActive; hasPaddle = true; }
    if (m_dsrFn == InputFunction::CwDitInput) { ditActive = dsrActive; hasPaddle = true; }
    if (m_ctsFn == InputFunction::CwDahInput) { dahActive = ctsActive; hasPaddle = true; }
    if (m_dsrFn == InputFunction::CwDahInput) { dahActive = dsrActive; hasPaddle = true; }

    if (m_paddleSwap) std::swap(ditActive, dahActive);

    if (hasPaddle && (ditActive != m_lastDitActive || dahActive != m_lastDahActive)) {
        m_lastDitActive = ditActive;
        m_lastDahActive = dahActive;
        emit cwPaddleChanged(ditActive, dahActive);
    }
}
#endif

void SerialPortController::loadSettings()
{
    auto& s = AppSettings::instance();
    QString port = s.value("SerialPortName", "").toString();

    auto strToFn = [](const QString& str) -> PinFunction {
        if (str == "PTT") return PinFunction::PTT;
        if (str == "CwKey") return PinFunction::CwKey;
        if (str == "CwPTT") return PinFunction::CwPTT;
        return PinFunction::None;
    };

    auto strToInputFn = [](const QString& str) -> InputFunction {
        if (str == "PttInput") return InputFunction::PttInput;
        if (str == "CwKeyInput") return InputFunction::CwKeyInput;
        if (str == "CwDitInput") return InputFunction::CwDitInput;
        if (str == "CwDahInput") return InputFunction::CwDahInput;
        return InputFunction::None;
    };

    m_dtrFn = strToFn(s.value("SerialDtrFunction", "None").toString());
    m_rtsFn = strToFn(s.value("SerialRtsFunction", "None").toString());
    m_dtrActiveHigh = s.value("SerialDtrPolarity", "ActiveHigh").toString() == "ActiveHigh";
    m_rtsActiveHigh = s.value("SerialRtsPolarity", "ActiveHigh").toString() == "ActiveHigh";

    m_ctsFn = strToInputFn(s.value("SerialCtsFunction", "None").toString());
    m_dsrFn = strToInputFn(s.value("SerialDsrFunction", "None").toString());
    m_ctsActiveHigh = s.value("SerialCtsPolarity", "ActiveLow").toString() == "ActiveHigh";
    m_dsrActiveHigh = s.value("SerialDsrPolarity", "ActiveLow").toString() == "ActiveHigh";
    m_paddleSwap = s.value("SerialPaddleSwap", "False").toString() == "True";

    if (!port.isEmpty() && s.value("SerialAutoOpen", "False").toString() == "True") {
        int baud = s.value("SerialBaudRate", "9600").toInt();
        int data = s.value("SerialDataBits", "8").toInt();
        int par  = s.value("SerialParity", "0").toInt();
        int stop = s.value("SerialStopBits", "1").toInt();
        open(port, baud, data, par, stop);
    }
}

void SerialPortController::saveSettings()
{
    auto& s = AppSettings::instance();

    auto fnToStr = [](PinFunction fn) -> QString {
        switch (fn) {
        case PinFunction::PTT:   return "PTT";
        case PinFunction::CwKey: return "CwKey";
        case PinFunction::CwPTT: return "CwPTT";
        default:                 return "None";
        }
    };

    auto inputFnToStr = [](InputFunction fn) -> QString {
        switch (fn) {
        case InputFunction::PttInput:   return "PttInput";
        case InputFunction::CwKeyInput: return "CwKeyInput";
        case InputFunction::CwDitInput: return "CwDitInput";
        case InputFunction::CwDahInput: return "CwDahInput";
        default:                        return "None";
        }
    };

    s.setValue("SerialPortName", portName());
    s.setValue("SerialDtrFunction", fnToStr(m_dtrFn));
    s.setValue("SerialRtsFunction", fnToStr(m_rtsFn));
    s.setValue("SerialDtrPolarity", m_dtrActiveHigh ? "ActiveHigh" : "ActiveLow");
    s.setValue("SerialRtsPolarity", m_rtsActiveHigh ? "ActiveHigh" : "ActiveLow");
    s.setValue("SerialCtsFunction", inputFnToStr(m_ctsFn));
    s.setValue("SerialDsrFunction", inputFnToStr(m_dsrFn));
    s.setValue("SerialCtsPolarity", m_ctsActiveHigh ? "ActiveHigh" : "ActiveLow");
    s.setValue("SerialDsrPolarity", m_dsrActiveHigh ? "ActiveHigh" : "ActiveLow");
    s.setValue("SerialPaddleSwap", m_paddleSwap ? "True" : "False");
    s.setValue("SerialAutoOpen", isOpen() ? "True" : "False");
    s.save();
}

} // namespace AetherSDR
