#pragma once

#include "core/PanadapterStream.h"

#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace AetherSDR {

class RadioModel;
class AudioEngine;
class TimeSeriesGraphWidget;

struct NetworkDiagnosticsSample {
    qint64 timestampMs{0};
    int    rttMs{0};
    int    audioGapMs{0};
    int    audioJitterMs{0};
    double rxKbps{0.0};
    double txKbps{0.0};
    double audioKbps{0.0};
    double fftKbps{0.0};
    double waterfallKbps{0.0};
    double meterKbps{0.0};
    double daxKbps{0.0};
    double packetLossPct{0.0};
    double audioLossPct{0.0};
    double fftLossPct{0.0};
    double waterfallLossPct{0.0};
    double meterLossPct{0.0};
    double daxLossPct{0.0};
    double audioBufferMs{0.0};
    double underrunsPerSecond{0.0};
};

class NetworkDiagnosticsHistory : public QObject {
public:
    explicit NetworkDiagnosticsHistory(RadioModel* model, AudioEngine* audio, QObject* parent = nullptr);

    const QVector<NetworkDiagnosticsSample>& samples() const { return m_samples; }
    NetworkDiagnosticsSample latestSample() const;

private:
    void sampleNow();
    void pruneSamples(qint64 nowMs);

    RadioModel* m_model{nullptr};
    AudioEngine* m_audio{nullptr};
    QTimer m_sampleTimer;
    QVector<NetworkDiagnosticsSample> m_samples;
    qint64 m_lastRxBytes{0};
    qint64 m_lastTxBytes{0};
    qint64 m_lastSampleMs{0};
    quint64 m_lastAudioUnderrunCount{0};
    qint64 m_lastCatBytes[PanadapterStream::CatCount]{};
};

class NetworkDiagnosticsDialog : public QDialog {
    Q_OBJECT

public:
    explicit NetworkDiagnosticsDialog(RadioModel* model,
                                      AudioEngine* audio,
                                      NetworkDiagnosticsHistory* history,
                                      QWidget* parent = nullptr);

protected:
    // Frameless 8-axis resize: 4 edges + 4 corners.  Hovering the bare
    // margin around the content updates the cursor; pressing starts a
    // compositor-managed resize via QWindow::startSystemResize.
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void leaveEvent(QEvent* ev) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;

private:
    void refresh();
    void updateCharts();
    int selectedRangeSeconds() const;
    Qt::Edges edgesAt(const QPoint& pos) const;
    void updateResizeCursor(const QPoint& pos);

    QWidget* m_titleBar{nullptr};

    RadioModel* m_model;
    AudioEngine* m_audio;
    NetworkDiagnosticsHistory* m_history{nullptr};
    QTimer      m_refreshTimer;
    QComboBox*  m_rangeCombo{nullptr};

    QLabel* m_statusLabel;
    QLabel* m_targetIpLabel;
    QLabel* m_sourcePathLabel;
    QLabel* m_tcpEndpointLabel;
    QLabel* m_udpEndpointLabel;
    QLabel* m_udpSeenLabel;
    QLabel* m_rttLabel;
    QLabel* m_maxRttLabel;
    QLabel* m_rxRateLabel;
    QLabel* m_txRateLabel;
    QLabel* m_droppedLabel;

    // Per-category rate labels
    QLabel* m_audioRateLabel;
    QLabel* m_fftRateLabel;
    QLabel* m_wfRateLabel;
    QLabel* m_meterRateLabel;
    QLabel* m_daxRateLabel;

    // Per-category drop labels
    QLabel* m_audioDropLabel;
    QLabel* m_fftDropLabel;
    QLabel* m_wfDropLabel;
    QLabel* m_meterDropLabel;
    QLabel* m_daxDropLabel;
    QLabel* m_audioBufferLabel;
    QLabel* m_audioBufferPeakLabel;
    QLabel* m_audioUnderrunLabel;
    QLabel* m_audioUnderrunRateLabel;
    QLabel* m_audioPacketGapLabel;
    QLabel* m_audioPacketGapMaxLabel;
    QLabel* m_audioJitterLabel;
    QLabel* m_overviewStatusValue{nullptr};
    QLabel* m_overviewLatencyValue{nullptr};
    QLabel* m_overviewLossValue{nullptr};
    QLabel* m_overviewAudioValue{nullptr};

    TimeSeriesGraphWidget* m_overviewLatencyGraph{nullptr};
    TimeSeriesGraphWidget* m_overviewLossGraph{nullptr};
    TimeSeriesGraphWidget* m_overviewRatesGraph{nullptr};
    TimeSeriesGraphWidget* m_overviewAudioGraph{nullptr};
    TimeSeriesGraphWidget* m_latencyGraph{nullptr};
    TimeSeriesGraphWidget* m_ratesGraph{nullptr};
    TimeSeriesGraphWidget* m_lossGraph{nullptr};
    TimeSeriesGraphWidget* m_audioGraph{nullptr};
};

} // namespace AetherSDR
