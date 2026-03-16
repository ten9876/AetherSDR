#pragma once

#include <QString>
#include <QMap>

namespace AetherSDR {

class RadioModel;
class SliceModel;

// Pure protocol handler for Hamlib rigctld emulation.
// No I/O — receives a text line, returns the response string.
// Shared by both the TCP server and the PTY virtual serial port.
class RigctlProtocol {
public:
    explicit RigctlProtocol(RadioModel* model);

    // Process one command line (may contain ';'-separated batch commands).
    // Returns the complete response string to send back to the client.
    QString handleLine(const QString& line);

    // Which slice this protocol instance controls.
    void setSliceIndex(int idx) { m_sliceIndex = idx; }
    int  sliceIndex() const     { return m_sliceIndex; }

    // Extended response mode (prepend '+' to enable).
    void setExtendedMode(bool on) { m_extended = on; }
    bool extendedMode() const     { return m_extended; }

private:
    // Process a single command (short or long form).
    // Returns the response string.
    QString processCommand(const QString& cmd);

    // Individual command handlers
    QString cmdGetFreq();
    QString cmdSetFreq(const QString& arg);
    QString cmdGetMode();
    QString cmdSetMode(const QString& args);
    QString cmdGetVfo();
    QString cmdSetVfo(const QString& arg);
    QString cmdGetPtt();
    QString cmdSetPtt(const QString& arg);
    QString cmdGetInfo();
    QString cmdGetSplitVfo();
    QString cmdSetSplitVfo(const QString& args);
    QString cmdDumpState();

    // Helpers
    SliceModel* currentSlice() const;
    QString rprt(int code) const;

    // Mode conversion tables
    static QString smartsdrToHamlib(const QString& mode);
    static QString hamlibToSmartSDR(const QString& mode);
    static int     hamlibModeFlag(const QString& mode);

    RadioModel* m_model;
    int  m_sliceIndex{0};
    bool m_extended{false};
};

} // namespace AetherSDR
