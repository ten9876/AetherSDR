#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTimer;

namespace AetherSDR {

class AudioEngine;
class ClientCompKnob;

// "Final Output Stage" panel — the very last tile in the channel
// strip, sitting after PUDU / Reverb.  Houses:
//   - A horizontal level meter showing the audio AS THE RADIO WILL
//     RECEIVE IT (i.e. tapped at the post-final-limiter point in
//     AudioEngine, after every chain stage AND after PC mic gain).
//   - An Enable toggle + Ceiling knob for the dedicated final-stage
//     brickwall limiter (ClientFinalLimiter), which sits at the tail
//     of the chain to ensure no sample escapes louder than the
//     configured ceiling.
//   - A "LIMIT" indicator that glows amber while the limiter is
//     actively clamping.
class StripFinalOutputPanel : public QWidget {
    Q_OBJECT

public:
    explicit StripFinalOutputPanel(AudioEngine* engine,
                                   QWidget* parent = nullptr);
    ~StripFinalOutputPanel() override;

    void showForTx();
    void syncControlsFromEngine();

private:
    void applyEnable(bool on);
    void applyCeiling(float db);
    void applyTrim(float db);
    void applyDcBlock(bool on);
    void applyTestToneEnabled(bool on);
    void applyTestToneFreq(float hz);
    void applyTestToneLevel(float db);
    void showToneEditor();
    void tickMeters();

    AudioEngine*    m_audio{nullptr};
    QPushButton*    m_enable{nullptr};
    QPushButton*    m_dcBtn{nullptr};
    QPushButton*    m_toneBtn{nullptr};
    ClientCompKnob* m_trim{nullptr};
    QWidget*        m_meter{nullptr};
    QLabel*         m_pkValue{nullptr};
    QLabel*         m_rmsValue{nullptr};
    QLabel*         m_grValue{nullptr};
    QLabel*         m_crestValue{nullptr};
    QWidget*        m_ovrLed{nullptr};
    QLabel*         m_limitLed{nullptr};
    QLabel*         m_activityLbl{nullptr};
    QTimer*         m_meterTimer{nullptr};

    float    m_inPeakDb{-120.0f};
    float    m_outPeakDb{-120.0f};
    float    m_outRmsDb{-120.0f};
    float    m_grDb{0.0f};
    bool     m_active{false};
    quint64  m_lastClipCount{0};
    qint64   m_ovrLatchUntilMs{0};
    bool     m_limitFlashOn{false};   // toggled each tick while active
};

} // namespace AetherSDR
