#pragma once
#ifdef HAVE_WEBSOCKETS

#include <QString>

namespace AetherSDR {

class RadioModel;
class SliceModel;

// TCI protocol handler — text command parser and response generator.
// No I/O — receives a command string, returns the response.
// Reference: https://github.com/ExpertSDR3/TCI (protocol v2.0)
class TciProtocol {
public:
    explicit TciProtocol(RadioModel* model);

    // Process one TCI command (without trailing semicolon).
    // Returns response string (with trailing semicolon) or empty if no response.
    QString handleCommand(const QString& cmd);

    // Generate the init burst sent to newly connected clients.
    QString generateInitBurst();

    // After handleCommand(), if the command changed radio state,
    // this returns a notification to broadcast to other clients.
    // Returns empty if no broadcast needed.
    QString pendingNotification();

private:
    // Command handlers — return response string or empty
    QString cmdVfo(const QStringList& args, bool isSet);
    QString cmdModulation(const QStringList& args, bool isSet);
    QString cmdTrx(const QStringList& args, bool isSet);
    QString cmdTune(const QStringList& args, bool isSet);
    QString cmdDrive(const QStringList& args, bool isSet);
    QString cmdTuneDrive(const QStringList& args, bool isSet);
    QString cmdRitEnable(const QStringList& args, bool isSet);
    QString cmdXitEnable(const QStringList& args, bool isSet);
    QString cmdRitOffset(const QStringList& args, bool isSet);
    QString cmdXitOffset(const QStringList& args, bool isSet);
    QString cmdSplitEnable(const QStringList& args, bool isSet);
    QString cmdRxFilterBand(const QStringList& args, bool isSet);
    QString cmdCwMacrosSpeed(const QStringList& args, bool isSet);
    QString cmdCwMsg(const QStringList& args);
    QString cmdLock(const QStringList& args, bool isSet);
    QString cmdSqlEnable(const QStringList& args, bool isSet);
    QString cmdSqlLevel(const QStringList& args, bool isSet);
    QString cmdVolume(const QStringList& args, bool isSet);
    QString cmdMute(const QStringList& args, bool isSet);
    QString cmdAgcMode(const QStringList& args, bool isSet);
    QString cmdAgcGain(const QStringList& args, bool isSet);
    QString cmdAgcT(const QStringList& args, bool isSet);
    QString cmdRxNbEnable(const QStringList& args, bool isSet);
    QString cmdRxNrEnable(const QStringList& args, bool isSet);
    QString cmdRxAnfEnable(const QStringList& args, bool isSet);
    QString cmdRxApfEnable(const QStringList& args, bool isSet);
    // AetherSDR extensions (DVK record/play)
    QString cmdRxRecord(const QStringList& args, bool isSet);
    QString cmdRxPlay(const QStringList& args, bool isSet);
    QString cmdSpot(const QStringList& args);
    QString cmdSpotDelete(const QStringList& args);
    QString cmdSpotClear();
    QString cmdCwMacros(const QStringList& args);
    QString cmdCwMacrosStop();
    QString cmdStart();
    QString cmdStop();
    QString cmdTxEnable(const QStringList& args);
    QString cmdIqStart(const QStringList& args);
    QString cmdIqStop(const QStringList& args);
    QString cmdIqSampleRate(const QStringList& args, bool isSet);
    QString cmdKeyer(const QStringList& args);
    QString cmdCwKeyerSpeed(const QStringList& args, bool isSet);
    QString cmdCwMacrosDelay(const QStringList& args, bool isSet);
    QString cmdCwTerminal(const QStringList& args, bool isSet);
    QString cmdDds(const QStringList& args, bool isSet);
    QString cmdIf(const QStringList& args, bool isSet);
    QString cmdRxChannelEnable(const QStringList& args, bool isSet);
    QString cmdRxVolume(const QStringList& args, bool isSet);
    QString cmdRxMute(const QStringList& args, bool isSet);
    QString cmdRxBalance(const QStringList& args, bool isSet);
    QString cmdMonEnable(const QStringList& args, bool isSet);
    QString cmdMonVolume(const QStringList& args, bool isSet);
    QString cmdRxNbParam(const QStringList& args, bool isSet);
    QString cmdRxBinEnable(const QStringList& args, bool isSet);
    QString cmdRxAncEnable(const QStringList& args, bool isSet);
    QString cmdRxDseEnable(const QStringList& args, bool isSet);
    QString cmdRxNfEnable(const QStringList& args, bool isSet);
    QString cmdDiglOffset(const QStringList& args, bool isSet);
    QString cmdDiguOffset(const QStringList& args, bool isSet);
    QString cmdSetInFocus();
    QString cmdTxFrequency();

    // Helpers
    SliceModel* sliceForTrx(int trx) const;

public:
    // Mode conversion (public for TciServer broadcast use)
    static QString smartsdrToTci(const QString& mode);
    static QString tciToSmartSDR(const QString& mode);

private:

    RadioModel* m_model;
    QString     m_pendingNotification;
    bool        m_started{false};  // client sent START
};

} // namespace AetherSDR

#endif // HAVE_WEBSOCKETS
