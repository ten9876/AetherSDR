#pragma once

#include <QByteArray>
#include <QWidget>

class QLabel;
class QFrame;
class GuardedComboBox;
class GuardedSlider;

namespace AetherSDR {

class WaveformWidget;

class WaveApplet : public QWidget {
    Q_OBJECT

public:
    explicit WaveApplet(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

public slots:
    void appendScopeSamples(const QByteArray& monoFloat32Pcm, int sampleRate, bool tx);
    void setTransmitting(bool tx);

private:
    void buildSettingsDrawer();
    void setSettingsExpanded(bool expanded);
    void updateZoomLabel();
    void updateRefreshLabel();

    WaveformWidget* m_waveform{nullptr};
    QFrame* m_settingsDrawer{nullptr};
    GuardedComboBox* m_viewCombo{nullptr};
    GuardedSlider* m_zoomSlider{nullptr};
    GuardedSlider* m_refreshSlider{nullptr};
    QLabel* m_zoomValue{nullptr};
    QLabel* m_refreshValue{nullptr};
};

} // namespace AetherSDR
