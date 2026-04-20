#pragma once

#include <QWidget>

class QPushButton;

namespace AetherSDR {

class AudioEngine;
class ClientCompKnob;             // reused — generic rotary knob
class ClientDeEssCurveWidget;

// Floating editor for the client-side de-esser.  Three-column layout
// inspired by Ableton's Compressor (the de-esser preset), using our
// actual de-esser parameters:
//
//   ┌─ bypass ──────────────────── × ┐
//   │ ┌──────┐ ┌────────────┐ ┌────┐ │
//   │ │ FREQ │ │            │ │ AMT│ │
//   │ │      │ │  bandpass  │ │    │ │
//   │ │  Q   │ │  response  │ │ ATK│ │
//   │ │      │ │  + live    │ │    │ │
//   │ │ THR  │ │  ball      │ │ REL│ │
//   │ └──────┘ └────────────┘ └────┘ │
//   └────────────────────────────────┘
class ClientDeEssEditor : public QWidget {
    Q_OBJECT

public:
    explicit ClientDeEssEditor(AudioEngine* engine, QWidget* parent = nullptr);
    ~ClientDeEssEditor() override;

    void showForTx();

signals:
    void bypassToggled(bool bypassed);

protected:
    void closeEvent(QCloseEvent* ev) override;
    void moveEvent(QMoveEvent* ev) override;
    void resizeEvent(QResizeEvent* ev) override;
    void showEvent(QShowEvent* ev) override;
    void hideEvent(QHideEvent* ev) override;

private:
    void saveGeometryToSettings();
    void restoreGeometryFromSettings();
    void syncControlsFromEngine();

    void applyFrequency(float hz);
    void applyQ(float q);
    void applyThreshold(float db);
    void applyAmount(float db);
    void applyAttack(float ms);
    void applyRelease(float ms);

    AudioEngine*            m_audio{nullptr};
    ClientDeEssCurveWidget* m_curve{nullptr};
    ClientCompKnob*         m_freq{nullptr};
    ClientCompKnob*         m_q{nullptr};
    ClientCompKnob*         m_threshold{nullptr};
    ClientCompKnob*         m_amount{nullptr};
    ClientCompKnob*         m_attack{nullptr};
    ClientCompKnob*         m_release{nullptr};
    QPushButton*            m_bypass{nullptr};
    bool                    m_restoring{false};
};

} // namespace AetherSDR
