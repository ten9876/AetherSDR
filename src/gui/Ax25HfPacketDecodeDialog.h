#pragma once

#include "PersistentDialog.h"
#include "core/tnc/AetherAx25LibmodemShim.h"

#include <QMetaObject>
#include <QPointer>

class QCheckBox;
class QLabel;
class QPushButton;
class QRadioButton;
class QTextEdit;
class QTimer;

namespace AetherSDR {

class AudioEngine;
class PacketActivityWidget;
class SliceModel;

class Ax25HfPacketDecodeDialog : public PersistentDialog {
    Q_OBJECT

public:
    explicit Ax25HfPacketDecodeDialog(AudioEngine* audio,
                                      SliceModel* initialSlice = nullptr,
                                      QWidget* parent = nullptr);
    ~Ax25HfPacketDecodeDialog() override;

    void setAttachedSlice(SliceModel* slice);

private:
    Ax25TonePolarity selectedTonePolarity() const;
    void setModemProfile(Ax25ModemProfile profile, bool persist);
    void setTonePolarity(Ax25TonePolarity polarity, bool persist);
    void setDecodeEnabled(bool enabled);
    void handleRxAudio(const QByteArray& monoFloat32Pcm, int sampleRate);
    void startAudioCapture();
    void finishAudioCapture(bool save);
    void appendFrame(const Ax25DecodedFrame& frame);
    void updateDiagnostics(const Ax25DecoderDiagnostics& diagnostics);
    void updateHeartbeat();
    void refreshStatus();
    void setDiagnosticsDebugEnabled(bool enabled, bool persist);
    void logAttachedSliceState(const QString& reason);
    void appendSystemLine(const QString& text);
    void appendDiagnosticsLine(const Ax25DecoderDiagnostics& diagnostics);
    QString formatTerminalLine(const Ax25DecodedFrame& frame) const;

    AudioEngine* m_audio{nullptr};
    AetherAx25LibmodemShim* m_shim{nullptr};
    QRadioButton* m_hf300Profile{nullptr};
    QRadioButton* m_vhf1200Profile{nullptr};
    QCheckBox* m_enableDecode{nullptr};
    QRadioButton* m_polarityNormal{nullptr};
    QRadioButton* m_polarityReverse{nullptr};
    QTextEdit* m_log{nullptr};
    QLabel* m_modemStatusDot{nullptr};
    QLabel* m_modemStatusValue{nullptr};
    QLabel* m_gainStageDot{nullptr};
    QLabel* m_gainStageValue{nullptr};
    QLabel* m_packetActivityTitle{nullptr};
    PacketActivityWidget* m_packetActivity{nullptr};
    QPushButton* m_clearButton{nullptr};
    QPushButton* m_captureButton{nullptr};
    QTimer* m_heartbeatTimer{nullptr};
    QPointer<SliceModel> m_attachedSlice;
    QMetaObject::Connection m_sliceSquelchConnection;
    QMetaObject::Connection m_sliceModeConnection;
    int m_attachedSliceId{-1};
    int m_frameCount{0};
    QDateTime m_enabledUtc;
    QDateTime m_lastDecodeUtc;
    QDateTime m_lastDiagnosticsUtc;
    QDateTime m_lastNoAudioNoticeUtc;
    Ax25DecoderDiagnostics m_lastDiagnostics;
    quint64 m_lastActivityHdlc{0};
    quint64 m_lastActivityAccepted{0};
    QByteArray m_capturePcm;
    int m_captureSampleRate{0};
    qsizetype m_captureTargetBytes{0};
    bool m_captureActive{false};
    bool m_diagnosticsDebugEnabled{false};
};

} // namespace AetherSDR
