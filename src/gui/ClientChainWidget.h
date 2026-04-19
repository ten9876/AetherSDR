#pragma once

#include "core/AudioEngine.h"

#include <QVector>
#include <QWidget>

namespace AetherSDR {

// Visual TX DSP signal chain.  Paints a horizontal strip:
//
//   [MIC] ─▶ [GATE] ─▶ [EQ] ─▶ [DESS] ─▶ [COMP] ─▶ [TUBE] ─▶ [ENH] ─▶ [TX]
//            ◄────────── draggable / bypassable ──────────►
//
// Clicking a processor stage emits editRequested() so the parent can
// open the corresponding floating editor.  Dragging a stage between
// other stages reorders the chain (AudioEngine::setTxChainStages).
// Right-click on a stage toggles its bypass via the relevant DSP
// module (only Eq and Comp wired today; the unimplemented stages show
// as "coming soon" greyed-out boxes but still participate in ordering
// so users can set up their preferred chain ahead of time).
class ClientChainWidget : public QWidget {
    Q_OBJECT

public:
    explicit ClientChainWidget(QWidget* parent = nullptr);

    // Binds the widget to the audio engine so it can read stage
    // bypass state and publish reorder / bypass changes.  Null is
    // safe — the widget renders the chain grey until bound.
    void setAudioEngine(AudioEngine* engine);

signals:
    // Fired when the user clicks a processor stage.  MainWindow maps
    // the stage to the correct editor and opens it.  Endpoint
    // (MIC, TX) clicks don't emit.
    void editRequested(AudioEngine::TxChainStage stage);

    // Emitted after a successful reorder so the parent can refresh
    // any downstream UI.  AudioEngine already has the new order by
    // this point — the signal is informational.
    void chainReordered();

    // Emitted whenever the user toggles a stage's bypass from the
    // chain's right-click menu.  MainWindow subscribes to drive the
    // corresponding applet's tile visibility — bypassed stages don't
    // occupy space in the applet tray.
    void stageEnabledChanged(AudioEngine::TxChainStage stage, bool enabled);

protected:
    void paintEvent(QPaintEvent* ev) override;
    void mousePressEvent(QMouseEvent* ev) override;
    void mouseMoveEvent(QMouseEvent* ev) override;
    void mouseDoubleClickEvent(QMouseEvent* ev) override;
    void contextMenuEvent(QContextMenuEvent* ev) override;
    void dragEnterEvent(QDragEnterEvent* ev) override;
    void dragMoveEvent(QDragMoveEvent* ev) override;
    void dragLeaveEvent(QDragLeaveEvent* ev) override;
    void dropEvent(QDropEvent* ev) override;
    void leaveEvent(QEvent* ev) override;
    QSize sizeHint() const override;

private:
    // Layout.  Widget paints one row of box rects; the cached list
    // lets mouse-hit + drag-drop walk the same geometry the painter
    // laid down without recomputing.
    struct BoxRect {
        AudioEngine::TxChainStage stage;
        QRectF                    rect;
        bool                      isEndpoint;   // MIC / TX, non-interactive
    };
    QVector<BoxRect> m_boxes;

    void rebuildLayout();   // recompute m_boxes from the current chain
    int  hitTest(const QPointF& pos) const;           // returns index into m_boxes, or -1
    int  dropInsertIndex(const QPointF& pos) const;   // computes new-position index for drag-over

    bool isStageImplemented(AudioEngine::TxChainStage s) const;
    bool isStageBypassed(AudioEngine::TxChainStage s) const;

    AudioEngine* m_audio{nullptr};

    // Drag state — tracks the press point and the drag target once
    // the mouse has moved far enough to distinguish click from drag.
    QPoint m_pressPos;
    int    m_pressIndex{-1};
    int    m_dropIndex{-1};      // where the drag is currently hovering (drawn as an insertion line)
};

} // namespace AetherSDR
