#include "VfoWidget.h"
#include "ComboStyle.h"
#include "SliceColors.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

#include <QPainter>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QMenu>
#include <QDoubleSpinBox>
#include <QSignalBlocker>
#include <QDir>
#include <QFile>
#include <QPixmap>
#include <QEvent>
#include <QMouseEvent>
#include <cmath>

// Slider that resets to a default value on double-click.
class ResetSlider : public QSlider {
public:
    explicit ResetSlider(int resetVal, Qt::Orientation o, QWidget* parent = nullptr)
        : QSlider(o, parent), m_resetVal(resetVal) {}
protected:
    void mouseDoubleClickEvent(QMouseEvent*) override { setValue(m_resetVal); }
private:
    int m_resetVal;
};

// ResetSlider with a small center-mark dot painted on the groove.
class CenterMarkSlider : public ResetSlider {
public:
    explicit CenterMarkSlider(int resetVal, Qt::Orientation o, QWidget* parent = nullptr)
        : ResetSlider(resetVal, o, parent) {}
protected:
    void paintEvent(QPaintEvent* ev) override {
        ResetSlider::paintEvent(ev);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        int cx = width() / 2;
        int cy = height() / 2;
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#608090"));
        p.drawEllipse(QPointF(cx, cy), 2.5, 2.5);
    }
};

// Button that paints a solid left- or right-pointing triangle.
class TriBtn : public QPushButton {
public:
    enum Dir { Left, Right };
    explicit TriBtn(Dir dir, QWidget* parent = nullptr)
        : QPushButton(parent), m_dir(dir)
    {
        setFlat(false);
        setFixedSize(22, 22);
        setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #203040; "
            "border-radius: 3px; padding: 0; margin: 0; min-width: 0; min-height: 0; }"
            "QPushButton:hover { background: #203040; }"
            "QPushButton:pressed { background: #00b4d8; }");
    }
protected:
    void paintEvent(QPaintEvent* ev) override {
        QPushButton::paintEvent(ev);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xc8, 0xd8, 0xe8));
        const double cx = width() / 2.0, cy = height() / 2.0;
        if (m_dir == Left) {
            const QPointF tri[] = {{cx + 3.0, cy - 4.0}, {cx + 3.0, cy + 4.0}, {cx - 3.0, cy}};
            p.drawPolygon(tri, 3);
        } else {
            const QPointF tri[] = {{cx - 3.0, cy - 4.0}, {cx - 3.0, cy + 4.0}, {cx + 3.0, cy}};
            p.drawPolygon(tri, 3);
        }
    }
private:
    Dir m_dir;
};


namespace AetherSDR {

// ── Styles ────────────────────────────────────────────────────────────────────

// Background is painted manually in paintEvent for true alpha transparency.
static const QString kBgStyle =
    "QWidget#VfoWidgetRoot { background: transparent; border: none; }";

static const QString kFlatBtn =
    "QPushButton { background: transparent; border: none; "
    "font-size: 13px; font-weight: bold; padding: 0 6px; margin: 0; }";

static const QString kTabLblNormal =
    "QLabel { background: transparent; border: none; "
    "border-bottom: 2px solid transparent; "
    "color: #6888a0; font-size: 13px; font-weight: bold; padding: 3px 0; }";

static const QString kTabLblActive =
    "QLabel { background: transparent; border: none; "
    "border-bottom: 2px solid #00b4d8; "
    "color: #00b4d8; font-size: 13px; font-weight: bold; padding: 3px 0; }";

static const QString kDspToggle =
    "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; "
    "color: #c8d8e8; font-size: 13px; font-weight: bold; padding: 2px 4px; }"
    "QPushButton:checked { background: #1a6030; color: #ffffff; border: 1px solid #20a040; }"
    "QPushButton:hover { border: 1px solid #0090e0; }";

static const QString kModeBtn =
    "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px; "
    "color: #c8d8e8; font-size: 13px; font-weight: bold; padding: 3px; }"
    "QPushButton:checked { background: #0070c0; color: #ffffff; border: 1px solid #0090e0; }"
    "QPushButton:hover { border: 1px solid #0090e0; }";

static const QString kSliderStyle =
    "QSlider::groove:horizontal { background: #1a2a3a; height: 4px; border-radius: 2px; }"
    "QSlider::handle:horizontal { background: #c8d8e8; width: 12px; margin: -4px 0; border-radius: 6px; }";

static const QString kLabelStyle =
    "QLabel { background: transparent; border: none; color: #8aa8c0; font-size: 13px; }";

// ── Construction ──────────────────────────────────────────────────────────────

VfoWidget::VfoWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("VfoWidgetRoot");
    setFixedWidth(WIDGET_W);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_TranslucentBackground);
    setAutoFillBackground(false);
    setStyleSheet(kBgStyle);
    buildUI();
}

void VfoWidget::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(6, 2, 6, 0);
    root->setSpacing(2);

    // ── Header row: ANT1(rx) ANT1(tx) 3.8K  SPLIT TX ──────────────────────
    auto* hdr = new QHBoxLayout;
    hdr->setSpacing(2);
    hdr->setAlignment(Qt::AlignTop);

    m_rxAntBtn = new QPushButton("ANT1");
    m_rxAntBtn->setFlat(true);
    m_rxAntBtn->setStyleSheet(kFlatBtn + "QPushButton { color: #4488ff; }");
    connect(m_rxAntBtn, &QPushButton::clicked, this, [this] {
        if (!m_slice) return;
        QMenu menu(this);
        for (const QString& ant : m_antList) {
            auto* act = menu.addAction(ant);
            act->setCheckable(true);
            act->setChecked(ant == m_slice->rxAntenna());
        }
        if (auto* sel = menu.exec(m_rxAntBtn->mapToGlobal(QPoint(0, m_rxAntBtn->height()))))
            m_slice->setRxAntenna(sel->text());
    });
    hdr->addWidget(m_rxAntBtn);

    m_txAntBtn = new QPushButton("ANT1");
    m_txAntBtn->setFlat(true);
    m_txAntBtn->setStyleSheet(kFlatBtn + "QPushButton { color: #ff4444; }");
    connect(m_txAntBtn, &QPushButton::clicked, this, [this] {
        if (!m_slice) return;
        QMenu menu(this);
        for (const QString& ant : m_antList) {
            auto* act = menu.addAction(ant);
            act->setCheckable(true);
            act->setChecked(ant == m_slice->txAntenna());
        }
        if (auto* sel = menu.exec(m_txAntBtn->mapToGlobal(QPoint(0, m_txAntBtn->height()))))
            m_slice->setTxAntenna(sel->text());
    });
    hdr->addWidget(m_txAntBtn);

    m_filterWidthLbl = new QLabel("2.7K");
    m_filterWidthLbl->setStyleSheet("QLabel { background: transparent; border: none; "
                                     "color: #00c8ff; font-size: 13px; font-weight: bold; "
                                     "margin: 0; padding: 0; }");
    hdr->addWidget(m_filterWidthLbl);

    hdr->addStretch(1);

    m_splitBadge = new QLabel("SPLIT");
    m_splitBadge->setStyleSheet("QLabel { background: transparent; border: none; "
                                 "color: #ffb800; font-size: 12px; font-weight: bold; }");
    m_splitBadge->hide();
    hdr->addWidget(m_splitBadge);

    m_txBadge = new QPushButton("TX");
    m_txBadge->setFixedSize(28, 20);
    updateTxBadgeStyle(false);
    connect(m_txBadge, &QPushButton::clicked, this, [this] {
        if (m_slice && !m_slice->isTxSlice())
            m_slice->setTxSlice(true);
    });
    hdr->addWidget(m_txBadge);

    m_sliceBadge = new QLabel("A");
    m_sliceBadge->setFixedSize(20, 20);
    m_sliceBadge->setAlignment(Qt::AlignCenter);
    m_sliceBadge->setStyleSheet(
        "QLabel { background: #0070c0; color: #ffffff; "
        "border-radius: 3px; font-weight: bold; font-size: 11px; }");
    hdr->addWidget(m_sliceBadge);

    root->addLayout(hdr);

    // Close and lock buttons — floating outside the VFO overlay,
    // stacked vertically on the side opposite the marker.
    // Created as children of our parent (SpectrumWidget) so they
    // render outside our bounds. Positioned in updatePosition().
    auto* btnParent = parentWidget() ? parentWidget() : this;

    m_closeSliceBtn = new QPushButton("\xE2\x9C\x95", btnParent);  // ✕
    m_closeSliceBtn->setFixedSize(20, 20);
    m_closeSliceBtn->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,15); border: none; "
        "border-radius: 10px; color: #c8d8e8; font-size: 11px; padding: 0; }"
        "QPushButton:hover { background: rgba(204,32,32,180); color: #ffffff; }");
    connect(m_closeSliceBtn, &QPushButton::clicked, this, [this] {
        emit closeSliceRequested();
    });

    m_lockVfoBtn = new QPushButton("\xF0\x9F\x94\x93", btnParent);  // 🔓
    m_lockVfoBtn->setFixedSize(20, 20);
    m_lockVfoBtn->setCheckable(true);
    m_lockVfoBtn->setStyleSheet(
        "QPushButton { background: rgba(255,255,255,15); border: none; "
        "border-radius: 10px; font-size: 12px; padding: 0; }"
        "QPushButton:checked { background: rgba(255,100,100,80); }"
        "QPushButton:hover { background: rgba(255,255,255,40); }");
    connect(m_lockVfoBtn, &QPushButton::toggled, this, [this](bool locked) {
        m_lockVfoBtn->setText(locked ? "\xF0\x9F\x94\x92" : "\xF0\x9F\x94\x93");
        emit lockToggled(locked);
    });

    // ── Frequency row (right-aligned) ─────────────────────────────────────
    m_freqLabel = new QLabel("14.225.000");
    m_freqLabel->setStyleSheet("QLabel { background: transparent; border: none; "
                                "color: #c8d8e8; font-size: 24px; font-weight: bold; }");
    m_freqLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    root->addWidget(m_freqLabel);

    // ── S-meter + dBm row (75/25 split) ────────────────────────────────────
    // S-meter bar is painted in paintEvent; spacer reserves its space.
    // dBm label sits to the right.
    auto* meterRow = new QHBoxLayout;
    meterRow->setSpacing(4);

    auto* sMeterSpacer = new QWidget;
    sMeterSpacer->setFixedHeight(22);
    sMeterSpacer->setAttribute(Qt::WA_TranslucentBackground);
    sMeterSpacer->setStyleSheet("QWidget { background: transparent; }");
    meterRow->addWidget(sMeterSpacer, 3);  // 75%

    m_dbmLabel = new QLabel("-95 dBm");
    m_dbmLabel->setStyleSheet("QLabel { background: transparent; border: none; "
                               "color: #6888a0; font-size: 11px; }");
    m_dbmLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    meterRow->addWidget(m_dbmLabel, 1);    // 25%

    root->addLayout(meterRow);

    // ── Tab bar ────────────────────────────────────────────────────────────
    m_tabBar = new QWidget;
    m_tabBar->setAttribute(Qt::WA_TranslucentBackground);
    m_tabBar->setStyleSheet("QWidget { background: transparent; }");
    auto* tabLayout = new QHBoxLayout(m_tabBar);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->setSpacing(0);

    const QStringList tabLabels = {"\xF0\x9F\x94\x8A", "DSP", "USB", "X/RIT", "DAX"};
    for (int i = 0; i < tabLabels.size(); ++i) {
        if (i > 0) {
            auto* sep = new QLabel("|");
            sep->setStyleSheet("QLabel { background: transparent; border: none; "
                               "color: rgba(255, 255, 255, 192); font-size: 13px; padding: 0; }");
            sep->setFixedWidth(6);
            sep->setAlignment(Qt::AlignCenter);
            tabLayout->addWidget(sep);
        }
        auto* lbl = new QLabel(tabLabels[i]);
        lbl->setStyleSheet(kTabLblNormal);
        lbl->setFixedHeight(24);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setCursor(Qt::PointingHandCursor);
        lbl->installEventFilter(this);
        tabLayout->addWidget(lbl, 1);
        m_tabBtns.append(lbl);
    }
    root->addWidget(m_tabBar);

    // ── Tab content (stacked) ──────────────────────────────────────────────
    m_tabStack = new QStackedWidget;
    m_tabStack->hide();
    buildTabContent();
    root->addWidget(m_tabStack);

    adjustSize();
}

// ── Tab content ───────────────────────────────────────────────────────────────

void VfoWidget::buildTabContent()
{
    // Tab 0: Audio
    {
        auto* m_audioTab = new QWidget;
        auto* vb = new QVBoxLayout(m_audioTab);
        vb->setContentsMargins(2, 2, 2, 2);
        vb->setSpacing(2);

        // AF row: mute toggle (speaker emoji) + gain slider
        auto* gainRow = new QHBoxLayout;
        gainRow->setSpacing(3);
        m_muteBtn = new QPushButton(QString::fromUtf8("\xF0\x9F\x94\x8A")); // 🔊
        m_muteBtn->setCheckable(true);
        m_muteBtn->setFlat(true);
        m_muteBtn->setFixedSize(26, 20);
        m_muteBtn->setStyleSheet(kFlatBtn + "QPushButton { font-size: 14px; padding: 0; }");
        gainRow->addWidget(m_muteBtn);
        m_afGainSlider = new QSlider(Qt::Horizontal);
        m_afGainSlider->setRange(0, 100);
        m_afGainSlider->setStyleSheet(kSliderStyle);
        gainRow->addWidget(m_afGainSlider, 1);
        vb->addLayout(gainRow);

        // Pan row: L + slider (with center marker) + R
        auto* panRow = new QHBoxLayout;
        panRow->setSpacing(3);
        auto* panL = new QLabel("L");
        panL->setStyleSheet(kLabelStyle);
        panL->setFixedWidth(10);
        panL->setAlignment(Qt::AlignCenter);
        panRow->addWidget(panL);
        m_panSlider = new CenterMarkSlider(50, Qt::Horizontal);
        m_panSlider->setRange(0, 100);
        m_panSlider->setValue(50);
        m_panSlider->setStyleSheet(kSliderStyle);
        panRow->addWidget(m_panSlider, 1);
        auto* panR = new QLabel("R");
        panR->setStyleSheet(kLabelStyle);
        panR->setFixedWidth(10);
        panR->setAlignment(Qt::AlignCenter);
        panRow->addWidget(panR);
        vb->addLayout(panRow);

        connect(m_afGainSlider, &QSlider::valueChanged, this, [this](int v) {
            if (!m_updatingFromModel) {
                if (m_slice) m_slice->setAudioGain(v);
                emit afGainChanged(v);
            }
        });
        connect(m_panSlider, &QSlider::valueChanged, this, [this](int v) {
            if (!m_updatingFromModel && m_slice) m_slice->setAudioPan(v);
        });
        connect(m_muteBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_slice) m_slice->setAudioMute(on);
            m_muteBtn->setText(on ? QString::fromUtf8("\xF0\x9F\x94\x87")    // 🔇
                                  : QString::fromUtf8("\xF0\x9F\x94\x8A"));  // 🔊
        });

        // SQL row: toggle + slider
        auto* sqlRow = new QHBoxLayout;
        sqlRow->setSpacing(3);
        m_sqlBtn = new QPushButton("SQL");
        m_sqlBtn->setCheckable(true);
        m_sqlBtn->setFixedHeight(20);
        m_sqlBtn->setStyleSheet(kDspToggle);
        sqlRow->addWidget(m_sqlBtn);
        m_sqlSlider = new QSlider(Qt::Horizontal);
        m_sqlSlider->setRange(0, 100);
        m_sqlSlider->setValue(20);
        m_sqlSlider->setStyleSheet(kSliderStyle);
        sqlRow->addWidget(m_sqlSlider, 1);
        vb->addLayout(sqlRow);

        connect(m_sqlBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_slice)
                m_slice->setSquelch(on, m_sqlSlider->value());
        });
        connect(m_sqlSlider, &QSlider::valueChanged, this, [this](int v) {
            if (!m_updatingFromModel && m_slice)
                m_slice->setSquelch(m_sqlBtn->isChecked(), v);
        });

        // AGC-T row: mode combo + threshold slider
        auto* agcRow = new QHBoxLayout;
        agcRow->setSpacing(3);
        m_agcCmb = new QComboBox;
        m_agcCmb->addItems({"Off", "Slow", "Med", "Fast"});
        m_agcCmb->setFixedHeight(20);
        m_agcCmb->setFixedWidth(60);
        m_sqlBtn->setFixedWidth(60);  // match AGC combo width
        AetherSDR::applyComboStyle(m_agcCmb);
        agcRow->addWidget(m_agcCmb);
        m_agcTSlider = new QSlider(Qt::Horizontal);
        m_agcTSlider->setRange(0, 100);
        m_agcTSlider->setValue(65);
        m_agcTSlider->setStyleSheet(kSliderStyle);
        agcRow->addWidget(m_agcTSlider, 1);
        vb->addLayout(agcRow);

        connect(m_agcCmb, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            if (!m_updatingFromModel && m_slice) {
                // Map display text to protocol value
                QString mode = text.toLower();
                if (mode == "off") mode = "off";
                else if (mode == "slow") mode = "slow";
                else if (mode == "med") mode = "med";
                else if (mode == "fast") mode = "fast";
                m_slice->setAgcMode(mode);
            }
        });
        connect(m_agcTSlider, &QSlider::valueChanged, this, [this](int v) {
            if (!m_updatingFromModel && m_slice) m_slice->setAgcThreshold(v);
        });

        m_tabStack->addWidget(m_audioTab);
    }

    // Tab 1: DSP — 4-column grid + RTTY Mark/Shift (mode-dependent)
    {
        auto* dspTab = new QWidget;
        auto* dspVb = new QVBoxLayout(dspTab);
        dspVb->setContentsMargins(2, 2, 2, 2);
        dspVb->setSpacing(3);

        m_dspGrid = new QGridLayout;
        m_dspGrid->setSpacing(3);

        auto makeDsp = [&](const QString& text) {
            auto* b = new QPushButton(text);
            b->setCheckable(true);
            b->setFixedHeight(26);
            b->setStyleSheet(kDspToggle);
            return b;
        };

        m_nrBtn   = makeDsp("NR");
        m_nr2Btn  = makeDsp("NR2");
        m_nbBtn   = makeDsp("NB");
        m_anfBtn  = makeDsp("ANF");
        m_apfBtn  = makeDsp("APF");
        m_nrlBtn  = makeDsp("NRL");
        m_nrsBtn  = makeDsp("NRS");
        m_rnnBtn  = makeDsp("RNN");
        m_nrfBtn  = makeDsp("NRF");
        m_anflBtn = makeDsp("ANFL");
        m_anftBtn = makeDsp("ANFT");
        m_apfBtn->hide();  // only visible in CW mode

        // Initial layout: 10 buttons in 4/4/2 grid (APF hidden)
        m_dspGrid->addWidget(m_nrBtn,   0, 0);
        m_dspGrid->addWidget(m_nr2Btn,  0, 1);
        m_dspGrid->addWidget(m_nbBtn,   0, 2);
        m_dspGrid->addWidget(m_anfBtn,  0, 3);
        m_dspGrid->addWidget(m_nrlBtn,  1, 0);
        m_dspGrid->addWidget(m_nrsBtn,  1, 1);
        m_dspGrid->addWidget(m_rnnBtn,  1, 2);
        m_dspGrid->addWidget(m_nrfBtn,  1, 3);
        m_dspGrid->addWidget(m_anflBtn, 2, 0);
        m_dspGrid->addWidget(m_anftBtn, 2, 1);
        dspVb->addLayout(m_dspGrid);

        // APF level slider (hidden unless CW mode)
        {
            m_apfContainer = new QWidget;
            auto* apfVb = new QHBoxLayout(m_apfContainer);
            apfVb->setContentsMargins(0, 2, 0, 0);
            apfVb->setSpacing(3);

            auto* lbl = new QLabel("APF");
            lbl->setStyleSheet(kLabelStyle);
            lbl->setFixedWidth(26);
            apfVb->addWidget(lbl);
            m_apfSlider = new QSlider(Qt::Horizontal);
            m_apfSlider->setRange(0, 100);
            m_apfSlider->setValue(50);
            m_apfSlider->setStyleSheet(kSliderStyle);
            apfVb->addWidget(m_apfSlider, 1);
            m_apfValueLbl = new QLabel("50");
            m_apfValueLbl->setStyleSheet(kLabelStyle);
            m_apfValueLbl->setFixedWidth(20);
            m_apfValueLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            apfVb->addWidget(m_apfValueLbl);

            connect(m_apfSlider, &QSlider::valueChanged, this, [this](int v) {
                m_apfValueLbl->setText(QString::number(v));
                if (!m_updatingFromModel && m_slice) m_slice->setApfLevel(v);
            });

            m_apfContainer->hide();
            dspVb->addWidget(m_apfContainer);
        }

        // RTTY Mark/Shift controls (hidden unless RTTY mode)
        {
            static const QString kStepLabelStyle =
                "QLabel { font-size: 10px; background: #0a0a18; border: 1px solid #1e2e3e; "
                "border-radius: 3px; padding: 0px 2px; color: #c8d8e8; }";
            static const QString kDimLabel =
                "QLabel { background: transparent; border: none; "
                "color: #6888a0; font-size: 10px; font-weight: bold; }";

            m_rttyContainer = new QWidget;
            auto* rvb = new QVBoxLayout(m_rttyContainer);
            rvb->setContentsMargins(0, 2, 0, 0);
            rvb->setSpacing(2);

            // Row 3: labels "Mark" and "Shift" side by side, centered
            {
                auto* lblRow = new QHBoxLayout;
                lblRow->setContentsMargins(0, 0, 0, 0);
                lblRow->setSpacing(4);
                auto* markLbl = new QLabel("Mark");
                markLbl->setStyleSheet(kDimLabel);
                markLbl->setAlignment(Qt::AlignCenter);
                lblRow->addWidget(markLbl, 1);
                auto* shiftLbl = new QLabel("Shift");
                shiftLbl->setStyleSheet(kDimLabel);
                shiftLbl->setAlignment(Qt::AlignCenter);
                lblRow->addWidget(shiftLbl, 1);
                rvb->addLayout(lblRow);
            }

            // Row 4: both step selectors side by side
            {
                auto* selRow = new QHBoxLayout;
                selRow->setContentsMargins(0, 0, 0, 0);
                selRow->setSpacing(4);

                // Mark selector: ◀ 2125 ▶
                auto* markMinus = new TriBtn(TriBtn::Left);
                selRow->addWidget(markMinus);
                m_markLabel = new QLabel("2125");
                m_markLabel->setAlignment(Qt::AlignCenter);
                m_markLabel->setStyleSheet(kStepLabelStyle);
                selRow->addWidget(m_markLabel, 1);
                auto* markPlus = new TriBtn(TriBtn::Right);
                selRow->addWidget(markPlus);

                selRow->addSpacing(4);

                // Shift selector: ◀ 170 ▶
                auto* shiftMinus = new TriBtn(TriBtn::Left);
                selRow->addWidget(shiftMinus);
                m_shiftLabel = new QLabel("170");
                m_shiftLabel->setAlignment(Qt::AlignCenter);
                m_shiftLabel->setStyleSheet(kStepLabelStyle);
                selRow->addWidget(m_shiftLabel, 1);
                auto* shiftPlus = new TriBtn(TriBtn::Right);
                selRow->addWidget(shiftPlus);

                static constexpr int MARK_STEP = 25;
                static constexpr int SHIFT_STEP = 5;
                connect(markMinus, &QPushButton::clicked, this, [this] {
                    if (m_slice) m_slice->setRttyMark(m_slice->rttyMark() - MARK_STEP);
                });
                connect(markPlus, &QPushButton::clicked, this, [this] {
                    if (m_slice) m_slice->setRttyMark(m_slice->rttyMark() + MARK_STEP);
                });
                connect(shiftMinus, &QPushButton::clicked, this, [this] {
                    if (m_slice) m_slice->setRttyShift(m_slice->rttyShift() - SHIFT_STEP);
                });
                connect(shiftPlus, &QPushButton::clicked, this, [this] {
                    if (m_slice) m_slice->setRttyShift(m_slice->rttyShift() + SHIFT_STEP);
                });

                rvb->addLayout(selRow);
            }

            m_rttyContainer->hide();
            dspVb->addWidget(m_rttyContainer);
        }

        // DIG offset control (hidden unless DIGL/DIGU mode)
        {
            static const QString kStepLabelStyle2 =
                "QLabel { font-size: 10px; background: #0a0a18; border: 1px solid #1e2e3e; "
                "border-radius: 3px; padding: 0px 2px; color: #c8d8e8; }";
            static const QString kDimLabel2 =
                "QLabel { background: transparent; border: none; "
                "color: #6888a0; font-size: 10px; font-weight: bold; }";

            m_digContainer = new QWidget;
            auto* dvb = new QVBoxLayout(m_digContainer);
            dvb->setContentsMargins(0, 2, 0, 0);
            dvb->setSpacing(2);

            auto* lbl = new QLabel("Offset");
            lbl->setStyleSheet(kDimLabel2);
            lbl->setAlignment(Qt::AlignCenter);
            dvb->addWidget(lbl);

            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(0);
            auto* minus = new TriBtn(TriBtn::Left);
            row->addWidget(minus);
            m_digOffsetLabel = new QLabel("2210");
            m_digOffsetLabel->setAlignment(Qt::AlignCenter);
            m_digOffsetLabel->setStyleSheet(kStepLabelStyle2);
            row->addWidget(m_digOffsetLabel, 1);
            auto* plus = new TriBtn(TriBtn::Right);
            row->addWidget(plus);
            dvb->addLayout(row);

            static constexpr int DIG_STEP = 10;
            connect(minus, &QPushButton::clicked, this, [this] {
                if (!m_slice) return;
                if (m_slice->mode() == "DIGL")
                    m_slice->setDiglOffset(m_slice->diglOffset() - DIG_STEP);
                else
                    m_slice->setDiguOffset(m_slice->diguOffset() - DIG_STEP);
            });
            connect(plus, &QPushButton::clicked, this, [this] {
                if (!m_slice) return;
                if (m_slice->mode() == "DIGL")
                    m_slice->setDiglOffset(m_slice->diglOffset() + DIG_STEP);
                else
                    m_slice->setDiguOffset(m_slice->diguOffset() + DIG_STEP);
            });

            m_digContainer->hide();
            dspVb->addWidget(m_digContainer);
        }

        // FM OPT controls (hidden unless FM/NFM mode)
        {
            static const QString kDirBtn =
                "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px;"
                " color: #c8d8e8; font-size: 11px; font-weight: bold; padding: 2px 4px; }"
                "QPushButton:checked { background: #0070c0; color: #ffffff; border: 1px solid #0090e0; }"
                "QPushButton:hover { border: 1px solid #0090e0; }";
            static const QString kRevBtn =
                "QPushButton { background: #1a2a3a; border: 1px solid #304050; border-radius: 2px;"
                " color: #c8d8e8; font-size: 11px; font-weight: bold; padding: 2px 4px; }"
                "QPushButton:checked { background-color: #604000; color: #ffb800; border: 1px solid #906000; }"
                "QPushButton:hover { border: 1px solid #0090e0; }";

            m_fmContainer = new QWidget;
            auto* fvb = new QVBoxLayout(m_fmContainer);
            fvb->setContentsMargins(0, 0, 0, 0);
            fvb->setSpacing(2);

            // Tone mode + tone value on one row
            auto* toneRow = new QHBoxLayout;
            toneRow->setSpacing(2);
            m_fmToneModeCmb = new QComboBox;
            m_fmToneModeCmb->addItem("Off", QString("off"));
            m_fmToneModeCmb->addItem("CTCSS TX", QString("ctcss_tx"));
            AetherSDR::applyComboStyle(m_fmToneModeCmb);
            toneRow->addWidget(m_fmToneModeCmb, 1);

            // Tone value — simplified list of common CTCSS tones
            m_fmToneValueCmb = new QComboBox;
            const double tones[] = {67.0,71.9,74.4,77.0,79.7,82.5,85.4,88.5,91.5,94.8,
                97.4,100.0,103.5,107.2,110.9,114.8,118.8,123.0,127.3,131.8,
                136.5,141.3,146.2,151.4,156.7,162.2,167.9,173.8,179.9,186.2,
                192.8,203.5,206.5,210.7,218.1,225.7,229.1,233.6,241.8,250.3,254.1};
            for (double f : tones)
                m_fmToneValueCmb->addItem(QString::number(f, 'f', 1),
                                           QString::number(f, 'f', 1));
            AetherSDR::applyComboStyle(m_fmToneValueCmb);
            m_fmToneValueCmb->setEnabled(false);
            toneRow->addWidget(m_fmToneValueCmb, 1);
            fvb->addLayout(toneRow);

            connect(m_fmToneModeCmb, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int idx) {
                if (m_fmToneModeCmb->signalsBlocked()) return;
                const QString mode = m_fmToneModeCmb->itemData(idx).toString();
                if (m_slice) m_slice->setFmToneMode(mode);
                m_fmToneValueCmb->setEnabled(mode == "ctcss_tx");
            });
            connect(m_fmToneValueCmb, QOverload<int>::of(&QComboBox::currentIndexChanged),
                    this, [this](int idx) {
                if (m_fmToneValueCmb->signalsBlocked()) return;
                if (m_slice) m_slice->setFmToneValue(m_fmToneValueCmb->itemData(idx).toString());
            });

            // Offset row
            auto* offRow = new QHBoxLayout;
            offRow->setSpacing(4);
            auto* offLbl = new QLabel("Offset:");
            offLbl->setStyleSheet(kLabelStyle);
            offRow->addWidget(offLbl);
            m_fmOffsetSpin = new QDoubleSpinBox;
            m_fmOffsetSpin->setRange(0.0, 100.0);
            m_fmOffsetSpin->setDecimals(3);
            m_fmOffsetSpin->setSingleStep(0.1);
            m_fmOffsetSpin->setSuffix(" MHz");
            m_fmOffsetSpin->setStyleSheet(
                "QDoubleSpinBox { background: #0a0a18; border: 1px solid #1e2e3e; "
                "border-radius: 3px; color: #c8d8e8; font-size: 10px; padding: 1px 2px; }"
                "QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { width: 0; }");
            offRow->addWidget(m_fmOffsetSpin, 1);
            fvb->addLayout(offRow);

            connect(m_fmOffsetSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    this, [this](double val) {
                if (m_fmOffsetSpin->signalsBlocked()) return;
                if (!m_slice) return;
                m_slice->setFmRepeaterOffsetFreq(val);
                const QString& dir = m_slice->repeaterOffsetDir();
                if (dir == "up") m_slice->setTxOffsetFreq(val);
                else if (dir == "down") m_slice->setTxOffsetFreq(-val);
                else m_slice->setTxOffsetFreq(0);
            });

            // Direction: − | Simplex | + | REV
            auto* dirRow = new QHBoxLayout;
            dirRow->setSpacing(2);

            auto applyDir = [this](const QString& dir) {
                if (!m_slice) return;
                m_slice->setRepeaterOffsetDir(dir);
                double offset = m_slice->fmRepeaterOffsetFreq();
                if (dir == "up") m_slice->setTxOffsetFreq(offset);
                else if (dir == "down") m_slice->setTxOffsetFreq(-offset);
                else m_slice->setTxOffsetFreq(0);
                m_fmOffsetDown->setChecked(dir == "down");
                m_fmSimplexBtn->setChecked(dir == "simplex");
                m_fmOffsetUp->setChecked(dir == "up");
            };

            m_fmOffsetDown = new QPushButton(QString::fromUtf8("\xe2\x88\x92"));
            m_fmOffsetDown->setCheckable(true);
            m_fmOffsetDown->setStyleSheet(kDirBtn);
            connect(m_fmOffsetDown, &QPushButton::clicked, this, [applyDir] { applyDir("down"); });
            dirRow->addWidget(m_fmOffsetDown);

            m_fmSimplexBtn = new QPushButton("Simplex");
            m_fmSimplexBtn->setCheckable(true);
            m_fmSimplexBtn->setChecked(true);
            m_fmSimplexBtn->setStyleSheet(kDirBtn);
            connect(m_fmSimplexBtn, &QPushButton::clicked, this, [applyDir] { applyDir("simplex"); });
            dirRow->addWidget(m_fmSimplexBtn);

            m_fmOffsetUp = new QPushButton("+");
            m_fmOffsetUp->setCheckable(true);
            m_fmOffsetUp->setStyleSheet(kDirBtn);
            connect(m_fmOffsetUp, &QPushButton::clicked, this, [applyDir] { applyDir("up"); });
            dirRow->addWidget(m_fmOffsetUp);

            m_fmRevBtn = new QPushButton("REV");
            m_fmRevBtn->setCheckable(true);
            m_fmRevBtn->setStyleSheet(kRevBtn);
            connect(m_fmRevBtn, &QPushButton::toggled, this, [this](bool on) {
                if (m_fmRevBtn->signalsBlocked() || !m_slice) return;
                double offset = m_slice->fmRepeaterOffsetFreq();
                const QString& dir = m_slice->repeaterOffsetDir();
                if (dir == "up") m_slice->setTxOffsetFreq(on ? -offset : offset);
                else if (dir == "down") m_slice->setTxOffsetFreq(on ? offset : -offset);
            });
            dirRow->addWidget(m_fmRevBtn);

            fvb->addLayout(dirRow);

            m_fmContainer->hide();
            dspVb->addWidget(m_fmContainer);
        }

        connect(m_nrBtn,   &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel && m_slice) m_slice->setNr(on); });
        connect(m_nr2Btn,  &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel) emit nr2Toggled(on); });
        connect(m_nbBtn,   &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel && m_slice) m_slice->setNb(on); });
        connect(m_anfBtn,  &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel && m_slice) m_slice->setAnf(on); });
        connect(m_nrlBtn,  &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel && m_slice) m_slice->setNrl(on); });
        connect(m_nrsBtn,  &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel && m_slice) m_slice->setNrs(on); });
        connect(m_rnnBtn,  &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel && m_slice) m_slice->setRnn(on); });
        connect(m_nrfBtn,  &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel && m_slice) m_slice->setNrf(on); });
        connect(m_anflBtn, &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel && m_slice) m_slice->setAnfl(on); });
        connect(m_anftBtn, &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel && m_slice) m_slice->setAnft(on); });
        connect(m_apfBtn,  &QPushButton::toggled, this, [this](bool on) { if (!m_updatingFromModel && m_slice) m_slice->setApf(on); });

        m_tabStack->addWidget(dspTab);
    }

    // Tab 2: Mode — dropdown + filter preset grid
    {
        auto* modeTab = new QWidget;
        auto* vb = new QVBoxLayout(modeTab);
        vb->setContentsMargins(2, 2, 2, 2);
        vb->setSpacing(3);

        // Mode dropdown (same style as RxApplet)
        m_modeCombo = new QComboBox;
        m_modeCombo->setFixedHeight(26);
        // Default modes — replaced dynamically when slice connects and sends mode_list
        m_modeCombo->addItems({"USB", "LSB", "CW", "AM", "SAM", "FM",
                                "NFM", "DFM", "DIGU", "DIGL", "RTTY"});
        AetherSDR::applyComboStyle(m_modeCombo);
        m_modeCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
            if (m_modeCombo->signalsBlocked()) return;
            if (!m_updatingFromModel && m_slice)
                m_slice->setMode(m_modeCombo->currentText());
        });

        // Top row: dropdown + 3 quick-mode buttons (USB, CW, AM)
        // All 4 columns equal width using a grid row
        auto* modeRow = new QHBoxLayout;
        modeRow->setSpacing(2);
        modeRow->addWidget(m_modeCombo, 1);
        for (const char* mode : {"USB", "CW", "AM"}) {
            auto* btn = new QPushButton(mode);
            btn->setFixedHeight(26);
            btn->setStyleSheet(kModeBtn);
            connect(btn, &QPushButton::clicked, this, [this, m = QString(mode)] {
                if (m_slice) m_slice->setMode(m);
            });
            modeRow->addWidget(btn, 1);
        }
        vb->addLayout(modeRow);

        // Filter preset grid (4 columns, rebuilt on mode change)
        auto* filterContainer = new QWidget;
        m_filterGrid = new QGridLayout(filterContainer);
        m_filterGrid->setContentsMargins(0, 0, 0, 0);
        m_filterGrid->setSpacing(2);
        for (int c = 0; c < 4; ++c)
            m_filterGrid->setColumnStretch(c, 1);
        vb->addWidget(filterContainer);

        m_tabStack->addWidget(modeTab);
    }

    // Tab 3: X/RIT — toggle, zero, ◀, Hz label, ▶ (matching RxApplet)
    {
        static constexpr int RIT_STEP_HZ = 10;

        static const QString kAmberActive =
            "QPushButton:checked { background-color: #604000; color: #ffb800; "
            "border: 1px solid #906000; }";
        static const QString kRitBtnStyle =
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 2px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
            "padding: 1px 4px; }" + kAmberActive;
        static const QString kZeroBtnStyle =
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 2px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
            "padding: 1px 4px; }"
            "QPushButton:hover { background: #203040; }";
        static const QString kHzLabelStyle =
            "QLabel { font-size: 10px; background: #0a0a18; border: 1px solid #1e2e3e; "
            "border-radius: 3px; padding: 0px 2px; color: #c8d8e8; }";

        auto* ritTab = new QWidget;
        auto* vb = new QVBoxLayout(ritTab);
        vb->setContentsMargins(2, 2, 2, 2);
        vb->setSpacing(2);

        // RIT row: toggle | 0 | ◀ | +0 Hz | ▶
        {
            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(0);

            m_ritBtn = new QPushButton("RIT");
            m_ritBtn->setCheckable(true);
            m_ritBtn->setFixedHeight(22);
            m_ritBtn->setStyleSheet(kRitBtnStyle);
            row->addWidget(m_ritBtn);

            auto* zero = new QPushButton("0");
            zero->setFixedHeight(22);
            zero->setStyleSheet(kZeroBtnStyle);
            connect(zero, &QPushButton::clicked, this, [this] {
                if (m_slice) m_slice->setRit(m_ritBtn->isChecked(), 0);
            });
            row->addSpacing(2);
            row->addWidget(zero);
            row->addSpacing(2);

            auto* minus = new TriBtn(TriBtn::Left);
            row->addWidget(minus);

            m_ritLabel = new QLabel("+0 Hz");
            m_ritLabel->setAlignment(Qt::AlignCenter);
            m_ritLabel->setStyleSheet(kHzLabelStyle);
            row->addWidget(m_ritLabel, 1);

            auto* plus = new TriBtn(TriBtn::Right);
            row->addWidget(plus);

            connect(m_ritBtn, &QPushButton::toggled, this, [this](bool on) {
                if (!m_updatingFromModel && m_slice) m_slice->setRit(on, m_slice->ritFreq());
            });
            connect(minus, &QPushButton::clicked, this, [this] {
                if (m_slice) m_slice->setRit(m_ritBtn->isChecked(), m_slice->ritFreq() - RIT_STEP_HZ);
            });
            connect(plus, &QPushButton::clicked, this, [this] {
                if (m_slice) m_slice->setRit(m_ritBtn->isChecked(), m_slice->ritFreq() + RIT_STEP_HZ);
            });

            vb->addLayout(row);
        }

        // XIT row: toggle | 0 | ◀ | +0 Hz | ▶
        {
            auto* row = new QHBoxLayout;
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(0);

            m_xitBtn = new QPushButton("XIT");
            m_xitBtn->setCheckable(true);
            m_xitBtn->setFixedHeight(22);
            m_xitBtn->setStyleSheet(kRitBtnStyle);
            row->addWidget(m_xitBtn);

            auto* zero = new QPushButton("0");
            zero->setFixedHeight(22);
            zero->setStyleSheet(kZeroBtnStyle);
            connect(zero, &QPushButton::clicked, this, [this] {
                if (m_slice) m_slice->setXit(m_xitBtn->isChecked(), 0);
            });
            row->addSpacing(2);
            row->addWidget(zero);
            row->addSpacing(2);

            auto* minus = new TriBtn(TriBtn::Left);
            row->addWidget(minus);

            m_xitLabel = new QLabel("+0 Hz");
            m_xitLabel->setAlignment(Qt::AlignCenter);
            m_xitLabel->setStyleSheet(kHzLabelStyle);
            row->addWidget(m_xitLabel, 1);

            auto* plus = new TriBtn(TriBtn::Right);
            row->addWidget(plus);

            connect(m_xitBtn, &QPushButton::toggled, this, [this](bool on) {
                if (!m_updatingFromModel && m_slice) m_slice->setXit(on, m_slice->xitFreq());
            });
            connect(minus, &QPushButton::clicked, this, [this] {
                if (m_slice) m_slice->setXit(m_xitBtn->isChecked(), m_slice->xitFreq() - RIT_STEP_HZ);
            });
            connect(plus, &QPushButton::clicked, this, [this] {
                if (m_slice) m_slice->setXit(m_xitBtn->isChecked(), m_slice->xitFreq() + RIT_STEP_HZ);
            });

            vb->addLayout(row);
        }

        m_tabStack->addWidget(ritTab);
    }

    // Tab 4: DAX
    {
        auto* daxTab = new QWidget;
        auto* vb = new QVBoxLayout(daxTab);
        vb->setContentsMargins(2, 2, 2, 2);
        vb->setSpacing(2);

        auto* row = new QHBoxLayout;
        row->setSpacing(3);
        auto* lbl = new QLabel("DAX Ch");
        lbl->setStyleSheet(kLabelStyle);
        row->addWidget(lbl);
        m_daxCmb = new QComboBox;
        m_daxCmb->addItems({"Off", "1", "2", "3", "4"});
        AetherSDR::applyComboStyle(m_daxCmb);
        row->addWidget(m_daxCmb, 1);
        vb->addLayout(row);

        connect(m_daxCmb, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) {
            if (!m_updatingFromModel && m_slice)
                m_slice->setDaxChannel(idx);  // 0=Off, 1-4=channels
        });

        m_tabStack->addWidget(daxTab);
    }
}

// ── Tab switching ─────────────────────────────────────────────────────────────

void VfoWidget::showTab(int index)
{
    if (m_activeTab == index) {
        // Toggle off — collapse content
        m_tabStack->hide();
        m_tabBtns[m_activeTab]->setStyleSheet(kTabLblNormal);
        m_activeTab = -1;
    } else {
        if (m_activeTab >= 0)
            m_tabBtns[m_activeTab]->setStyleSheet(kTabLblNormal);
        m_activeTab = index;
        m_tabBtns[index]->setStyleSheet(kTabLblActive);
        m_tabStack->setCurrentIndex(index);
        m_tabStack->show();
    }
    adjustSize();
}

// ── Positioning ───────────────────────────────────────────────────────────────

void VfoWidget::updatePosition(int vfoX, int specTop)
{
    const int w = width();
    int x = vfoX - w;  // default: left of VFO marker
    bool onLeft = true;

    // Flip to right side if widget would be clipped on the left
    if (x < 0) {
        x = vfoX;
        onLeft = false;
    }

    move(x, specTop);

    // Position close/lock buttons stacked vertically on the side opposite the marker
    if (m_closeSliceBtn && m_lockVfoBtn) {
        const int btnSize = 20;
        const int gap = 2;
        int btnX;
        if (onLeft)
            btnX = x - btnSize - gap;  // left of VFO widget
        else
            btnX = x + w + gap;        // right of VFO widget

        m_closeSliceBtn->move(btnX, specTop);
        m_lockVfoBtn->move(btnX, specTop + btnSize + gap);
        m_closeSliceBtn->raise();
        m_lockVfoBtn->raise();
    }
}

// ── S-Meter bar (custom paint) ────────────────────────────────────────────────

void VfoWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // Semi-transparent white background with rounded corners
    p.setPen(QColor(255, 255, 255, 13));
    p.setBrush(QColor(255, 255, 255, 13));
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 3, 3);

    p.setRenderHint(QPainter::Antialiasing, false);

    // Bar rect: drawn in the S-meter row (75% left portion)
    const int barX = 6;
    const int barW = (width() - 12) * 3 / 4;  // 75% of widget width
    const int barY = m_dbmLabel->geometry().center().y() - 3;
    const int barH = 6;

    // Background
    p.fillRect(barX, barY, barW, barH, QColor(0x10, 0x18, 0x20));

    // S-meter scale: S0=-127, S9=-73 (6 dB per S-unit), S9+60=-13
    // S0–S9 occupies left 60%, S9–S9+60 occupies right 40%
    constexpr float S0_DBM  = -127.0f;
    constexpr float S9_DBM  = -73.0f;
    constexpr float S9P60   = -13.0f;
    const int s9X = barX + barW * 60 / 100;  // S9 boundary pixel

    // Signal fill — blue up to S9, red beyond
    float frac;
    if (m_signalDbm <= S0_DBM) {
        frac = 0.0f;
    } else if (m_signalDbm <= S9_DBM) {
        frac = (m_signalDbm - S0_DBM) / (S9_DBM - S0_DBM) * 0.6f;
    } else if (m_signalDbm <= S9P60) {
        frac = 0.6f + (m_signalDbm - S9_DBM) / (S9P60 - S9_DBM) * 0.4f;
    } else {
        frac = 1.0f;
    }
    int fillW = static_cast<int>(frac * barW);

    if (fillW > 0) {
        p.fillRect(barX, barY, fillW, barH, QColor(0x00, 0xc0, 0x40));
    }

    // ── Scale bar with tick marks below the S-meter ────────────────────────
    const int scaleY = barY + barH + 2;
    const int tickH  = 3;

    // Horizontal line: blue from start to S9, red from S9 to end
    p.setPen(QColor(0x30, 0x80, 0xff));
    p.drawLine(barX, scaleY, s9X, scaleY);
    p.setPen(QColor(0xd0, 0x20, 0x20));
    p.drawLine(s9X, scaleY, barX + barW, scaleY);

    p.setPen(QColor(0x30, 0x80, 0xff));  // blue for S1–S9

    // S-unit ticks: S1, S3, S5, S7, S9 — S1 at left edge, S9 at 60%
    for (int s = 1; s <= 9; s += 2) {
        float sf = static_cast<float>(s - 1) / 8.0f * 0.6f;
        int tx = barX + static_cast<int>(sf * barW);
        int h = (s == 9) ? tickH + 1 : tickH;
        p.drawLine(tx, scaleY, tx, scaleY + h);
    }

    p.setPen(QColor(0xd0, 0x20, 0x20));  // red for +20, +40

    // +20 tick
    {
        float sf = 0.6f + (20.0f / 60.0f) * 0.4f;
        int tx = barX + static_cast<int>(sf * barW);
        p.drawLine(tx, scaleY, tx, scaleY + tickH);
    }
    // +40 tick
    {
        float sf = 0.6f + (40.0f / 60.0f) * 0.4f;
        int tx = barX + static_cast<int>(sf * barW);
        p.drawLine(tx, scaleY, tx, scaleY + tickH);
    }

    // Scale labels
    QFont scaleFont = p.font();
    scaleFont.setPixelSize(7);
    scaleFont.setBold(true);
    p.setFont(scaleFont);

    const int lblY = scaleY + tickH + 7;

    // Blue labels: 1, 3, 5, 7, 9 — S1 at left edge, S9 at 60%
    p.setPen(QColor(0x30, 0x80, 0xff));
    for (int s : {1, 3, 5, 7, 9}) {
        float sf = static_cast<float>(s - 1) / 8.0f * 0.6f;
        int tx = barX + static_cast<int>(sf * barW);
        p.drawText(tx - 3, lblY, QString::number(s));
    }

    // Red labels: +20, +40
    p.setPen(QColor(0xd0, 0x20, 0x20));
    {
        float sf = 0.6f + (20.0f / 60.0f) * 0.4f;
        int tx = barX + static_cast<int>(sf * barW);
        p.drawText(tx - 6, lblY, "+20");
    }
    {
        float sf = 0.6f + (40.0f / 60.0f) * 0.4f;
        int tx = barX + static_cast<int>(sf * barW);
        p.drawText(tx - 6, lblY, "+40");
    }

}

// ── Signal level ──────────────────────────────────────────────────────────────

void VfoWidget::setSignalLevel(float dbm)
{
    m_signalDbm = dbm;
    m_dbmLabel->setText(QString("%1 dBm").arg(static_cast<int>(dbm)));
    update();  // repaint S-meter bar
}

// ── Slice connection ──────────────────────────────────────────────────────────

void VfoWidget::setSlice(SliceModel* slice)
{
    if (m_slice)
        m_slice->disconnect(this);
    m_slice = slice;
    if (!m_slice) return;

    // Frequency
    connect(m_slice, &SliceModel::frequencyChanged, this, [this](double) { updateFreqLabel(); });
    // Mode list (dynamic from radio)
    connect(m_slice, &SliceModel::modeListChanged, this, [this](const QStringList& modes) {
        QSignalBlocker sb(m_modeCombo);
        QString cur = m_modeCombo->currentText();
        m_modeCombo->clear();
        m_modeCombo->addItems(modes);
        int idx = m_modeCombo->findText(cur);
        if (idx >= 0) m_modeCombo->setCurrentIndex(idx);
    });
    // Populate now if already available
    if (!m_slice->modeList().isEmpty()) {
        QSignalBlocker sb(m_modeCombo);
        QString cur = m_modeCombo->currentText();
        m_modeCombo->clear();
        m_modeCombo->addItems(m_slice->modeList());
        int idx = m_modeCombo->findText(cur);
        if (idx >= 0) m_modeCombo->setCurrentIndex(idx);
    }
    // Mode
    connect(m_slice, &SliceModel::modeChanged, this, [this](const QString& mode) {
        m_tabBtns[2]->setText(mode);  // update mode tab label
        updateModeTab();
        // Show/hide mode-specific DSP controls
        // Categorize by mode family (supports future/unknown modes)
        bool isRtty = (mode == "RTTY");
        bool isCw   = (mode == "CW" || mode == "CWL");
        bool isDig  = (mode == "DIGL" || mode == "DIGU");
        bool isFm   = (mode == "FM" || mode == "NFM" || mode == "DFM");
        bool isFdv  = mode.startsWith("FDV");  // FDVU, FDVM, etc.
        // Swap DSP tab label to OPT for FM modes
        m_tabBtns[1]->setText(isFm ? "OPT" : "DSP");
        m_rttyContainer->setVisible(isRtty);
        m_apfContainer->setVisible(isCw);
        m_digContainer->setVisible(isDig && !isFdv);
        m_fmContainer->setVisible(isFm);
        if (isDig) {
            int off = (mode == "DIGL") ? m_slice->diglOffset() : m_slice->diguOffset();
            m_digOffsetLabel->setText(QString::number(off));
        }
        // CW: show APF, hide ANF/RNN/ANFL/ANFT
        // RTTY/DIG/FDV: hide ANF/ANFL/ANFT
        bool isVoice = !isRtty && !isCw && !isDig && !isFm && !isFdv;
        m_apfBtn->setVisible(isCw);
        m_anfBtn->setVisible(isVoice);
        m_rnnBtn->setVisible(!isCw && !isFm);
        m_anflBtn->setVisible(isVoice);
        m_anftBtn->setVisible(isVoice);
        // Hide all DSP buttons in FM mode
        m_nrBtn->setVisible(!isFm);
        m_nr2Btn->setVisible(!isFm);
        m_nbBtn->setVisible(!isFm);
        m_nrlBtn->setVisible(!isFm);
        m_nrsBtn->setVisible(!isFm);
        m_nrfBtn->setVisible(!isFm);
        relayoutDspGrid();
        if (m_tabStack->isVisible()) adjustSize();
    });
    // Filter
    connect(m_slice, &SliceModel::filterChanged, this, [this](int, int) {
        updateFilterLabel();
        updateFilterHighlight();
    });
    // Antennas
    connect(m_slice, &SliceModel::rxAntennaChanged, this, [this](const QString& ant) {
        m_updatingFromModel = true; m_rxAntBtn->setText(ant); m_updatingFromModel = false;
    });
    connect(m_slice, &SliceModel::txAntennaChanged, this, [this](const QString& ant) {
        m_updatingFromModel = true; m_txAntBtn->setText(ant); m_updatingFromModel = false;
    });
    // TX slice — toggle between red (active TX) and grey (clickable to set TX)
    connect(m_slice, &SliceModel::txSliceChanged, this, [this](bool tx) {
        updateTxBadgeStyle(tx);
    });
    // Audio
    connect(m_slice, &SliceModel::audioGainChanged, this, [this](float g) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_afGainSlider);
        m_afGainSlider->setValue(static_cast<int>(g));
        m_updatingFromModel = false;
    });
    connect(m_slice, &SliceModel::audioMuteChanged, this, [this](bool mute) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_muteBtn);
        m_muteBtn->setChecked(mute);
        m_muteBtn->setText(mute ? QString::fromUtf8("\xF0\x9F\x94\x87")
                                : QString::fromUtf8("\xF0\x9F\x94\x8A"));
        m_updatingFromModel = false;
    });
    // DSP toggles
    auto connectDsp = [this](auto signal, QPushButton* btn) {
        connect(m_slice, signal, this, [this, btn](bool on) {
            m_updatingFromModel = true;
            QSignalBlocker sb(btn);
            btn->setChecked(on);
            m_updatingFromModel = false;
        });
    };
    connectDsp(&SliceModel::nbChanged,  m_nbBtn);
    connectDsp(&SliceModel::nrChanged,  m_nrBtn);
    connectDsp(&SliceModel::anfChanged, m_anfBtn);
    connectDsp(&SliceModel::nrlChanged, m_nrlBtn);
    connectDsp(&SliceModel::nrsChanged, m_nrsBtn);
    connectDsp(&SliceModel::rnnChanged, m_rnnBtn);
    connectDsp(&SliceModel::nrfChanged, m_nrfBtn);
    connectDsp(&SliceModel::anflChanged, m_anflBtn);
    connectDsp(&SliceModel::anftChanged, m_anftBtn);
    connectDsp(&SliceModel::apfChanged, m_apfBtn);
    connect(m_slice, &SliceModel::apfLevelChanged, this, [this](int v) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_apfSlider);
        m_apfSlider->setValue(v);
        m_apfValueLbl->setText(QString::number(v));
        m_updatingFromModel = false;
    });
    // Squelch
    connect(m_slice, &SliceModel::squelchChanged, this, [this](bool on, int level) {
        m_updatingFromModel = true;
        QSignalBlocker b1(m_sqlBtn), b2(m_sqlSlider);
        m_sqlBtn->setChecked(on);
        m_sqlSlider->setValue(level);
        m_updatingFromModel = false;
    });
    // AGC
    connect(m_slice, &SliceModel::agcModeChanged, this, [this](const QString& mode) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_agcCmb);
        // Map protocol value to display text
        if (mode == "off") m_agcCmb->setCurrentText("Off");
        else if (mode == "slow") m_agcCmb->setCurrentText("Slow");
        else if (mode == "med") m_agcCmb->setCurrentText("Med");
        else if (mode == "fast") m_agcCmb->setCurrentText("Fast");
        m_updatingFromModel = false;
    });
    connect(m_slice, &SliceModel::agcThresholdChanged, this, [this](int v) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_agcTSlider);
        m_agcTSlider->setValue(v);
        m_updatingFromModel = false;
    });
    // RIT/XIT
    connect(m_slice, &SliceModel::ritChanged, this, [this](bool on, int hz) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_ritBtn);
        m_ritBtn->setChecked(on);
        m_ritLabel->setText(QString("%1%2 Hz").arg(hz >= 0 ? "+" : "").arg(hz));
        m_updatingFromModel = false;
    });
    connect(m_slice, &SliceModel::xitChanged, this, [this](bool on, int hz) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_xitBtn);
        m_xitBtn->setChecked(on);
        m_xitLabel->setText(QString("%1%2 Hz").arg(hz >= 0 ? "+" : "").arg(hz));
        m_updatingFromModel = false;
    });
    // FM controls
    connect(m_slice, &SliceModel::fmToneModeChanged, this, [this](const QString& mode) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_fmToneModeCmb);
        int idx = m_fmToneModeCmb->findData(mode);
        if (idx >= 0) m_fmToneModeCmb->setCurrentIndex(idx);
        m_fmToneValueCmb->setEnabled(mode == "ctcss_tx");
        m_updatingFromModel = false;
    });
    connect(m_slice, &SliceModel::fmToneValueChanged, this, [this](const QString& val) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_fmToneValueCmb);
        int idx = m_fmToneValueCmb->findData(val);
        if (idx >= 0) m_fmToneValueCmb->setCurrentIndex(idx);
        m_updatingFromModel = false;
    });
    connect(m_slice, &SliceModel::repeaterOffsetDirChanged, this, [this](const QString& dir) {
        m_updatingFromModel = true;
        QSignalBlocker b1(m_fmOffsetDown), b2(m_fmSimplexBtn), b3(m_fmOffsetUp);
        m_fmOffsetDown->setChecked(dir == "down");
        m_fmSimplexBtn->setChecked(dir == "simplex");
        m_fmOffsetUp->setChecked(dir == "up");
        m_updatingFromModel = false;
    });
    connect(m_slice, &SliceModel::fmRepeaterOffsetFreqChanged, this, [this](double mhz) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_fmOffsetSpin);
        m_fmOffsetSpin->setValue(mhz);
        m_updatingFromModel = false;
    });
    // DIG offset
    connect(m_slice, &SliceModel::diglOffsetChanged, this, [this](int hz) {
        if (m_slice && m_slice->mode() == "DIGL")
            m_digOffsetLabel->setText(QString::number(hz));
    });
    connect(m_slice, &SliceModel::diguOffsetChanged, this, [this](int hz) {
        if (m_slice && m_slice->mode() == "DIGU")
            m_digOffsetLabel->setText(QString::number(hz));
    });
    // RTTY Mark/Shift
    connect(m_slice, &SliceModel::rttyMarkChanged, this, [this](int hz) {
        m_markLabel->setText(QString::number(hz));
    });
    connect(m_slice, &SliceModel::rttyShiftChanged, this, [this](int hz) {
        m_shiftLabel->setText(QString::number(hz));
    });
    // DAX
    connect(m_slice, &SliceModel::daxChannelChanged, this, [this](int ch) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_daxCmb);
        m_daxCmb->setCurrentIndex(ch);
        m_updatingFromModel = false;
    });

    syncFromSlice();
}

void VfoWidget::updateTxBadgeStyle(bool isTx)
{
    if (isTx) {
        m_txBadge->setStyleSheet(
            "QPushButton { background: #cc0000; color: #ffffff; border: none; "
            "border-radius: 2px; font-size: 12px; font-weight: bold; padding: 0; }"
            "QPushButton:hover { background: #ff2222; }");
    } else {
        m_txBadge->setStyleSheet(
            "QPushButton { background: #404040; color: #808080; border: none; "
            "border-radius: 2px; font-size: 12px; font-weight: bold; padding: 0; }"
            "QPushButton:hover { background: #606060; color: #c0c0c0; }");
    }
}

void VfoWidget::syncFromSlice()
{
    if (!m_slice) return;
    m_updatingFromModel = true;

    m_rxAntBtn->setText(m_slice->rxAntenna());
    m_txAntBtn->setText(m_slice->txAntenna());
    updateTxBadgeStyle(m_slice->isTxSlice());
    {
        QSignalBlocker b(m_lockVfoBtn);
        m_lockVfoBtn->setChecked(m_slice->isLocked());
        m_lockVfoBtn->setText(m_slice->isLocked() ? "\xF0\x9F\x94\x92" : "\xF0\x9F\x94\x93");
    }
    const char letters[] = "ABCD";
    int id = m_slice->sliceId();
    m_sliceBadge->setText(QString(QChar(id >= 0 && id < 4 ? letters[id] : '?')));
    // Color-code the slice badge to match the spectrum overlay colors
    const char* badgeColor = (id >= 0 && id < kSliceColorCount)
        ? kSliceColors[id].hexActive : "#0070c0";
    m_sliceBadge->setStyleSheet(
        QString("QLabel { background: %1; color: #000000; "
                "border-radius: 3px; font-weight: bold; font-size: 11px; }").arg(badgeColor));
    updateFreqLabel();
    updateFilterLabel();

    // Mode tab
    m_tabBtns[2]->setText(m_slice->mode());
    updateModeTab();

    // Audio
    m_afGainSlider->setValue(static_cast<int>(m_slice->audioGain()));
    m_panSlider->setValue(m_slice->audioPan());
    {
        QSignalBlocker sb(m_muteBtn);
        bool muted = m_slice->audioMute();
        m_muteBtn->setChecked(muted);
        m_muteBtn->setText(muted ? QString::fromUtf8("\xF0\x9F\x94\x87")
                                 : QString::fromUtf8("\xF0\x9F\x94\x8A"));
    }
    {
        QSignalBlocker b1(m_sqlBtn), b2(m_sqlSlider);
        m_sqlBtn->setChecked(m_slice->squelchOn());
        m_sqlSlider->setValue(m_slice->squelchLevel());
    }
    {
        QSignalBlocker sb(m_agcCmb);
        const QString& mode = m_slice->agcMode();
        if (mode == "off") m_agcCmb->setCurrentText("Off");
        else if (mode == "slow") m_agcCmb->setCurrentText("Slow");
        else if (mode == "med") m_agcCmb->setCurrentText("Med");
        else if (mode == "fast") m_agcCmb->setCurrentText("Fast");
    }
    {
        QSignalBlocker sb(m_agcTSlider);
        m_agcTSlider->setValue(m_slice->agcThreshold());
    }

    // DSP
    auto syncDsp = [](QPushButton* btn, bool on) {
        QSignalBlocker sb(btn); btn->setChecked(on);
    };
    syncDsp(m_nbBtn,  m_slice->nbOn());
    syncDsp(m_nrBtn,  m_slice->nrOn());
    syncDsp(m_anfBtn, m_slice->anfOn());
    syncDsp(m_nrlBtn, m_slice->nrlOn());
    syncDsp(m_nrsBtn, m_slice->nrsOn());
    syncDsp(m_rnnBtn, m_slice->rnnOn());
    syncDsp(m_nrfBtn, m_slice->nrfOn());
    syncDsp(m_anflBtn, m_slice->anflOn());
    syncDsp(m_anftBtn, m_slice->anftOn());
    syncDsp(m_apfBtn, m_slice->apfOn());

    // RIT/XIT
    {
        QSignalBlocker sb1(m_ritBtn), sb2(m_xitBtn);
        m_ritBtn->setChecked(m_slice->ritOn());
        m_xitBtn->setChecked(m_slice->xitOn());
    }
    m_ritLabel->setText(QString("%1%2 Hz").arg(m_slice->ritFreq() >= 0 ? "+" : "").arg(m_slice->ritFreq()));
    m_xitLabel->setText(QString("%1%2 Hz").arg(m_slice->xitFreq() >= 0 ? "+" : "").arg(m_slice->xitFreq()));

    // RTTY
    bool isRtty = (m_slice->mode() == "RTTY");
    m_markLabel->setText(QString::number(m_slice->rttyMark()));
    m_shiftLabel->setText(QString::number(m_slice->rttyShift()));
    m_rttyContainer->setVisible(isRtty);
    bool isCw = (m_slice->mode() == "CW" || m_slice->mode() == "CWL");
    bool isDig = (m_slice->mode() == "DIGL" || m_slice->mode() == "DIGU");
    bool isFm = (m_slice->mode() == "FM" || m_slice->mode() == "NFM");
    m_tabBtns[1]->setText(isFm ? "OPT" : "DSP");
    m_apfBtn->setVisible(isCw);
    m_anfBtn->setVisible(!isRtty && !isCw && !isDig && !isFm);
    m_rnnBtn->setVisible(!isCw && !isFm);
    m_anflBtn->setVisible(!isRtty && !isCw && !isDig && !isFm);
    m_anftBtn->setVisible(!isRtty && !isCw && !isDig && !isFm);
    m_nrBtn->setVisible(!isFm);
    m_nr2Btn->setVisible(!isFm);
    m_nbBtn->setVisible(!isFm);
    m_nrlBtn->setVisible(!isFm);
    m_nrsBtn->setVisible(!isFm);
    m_nrfBtn->setVisible(!isFm);
    m_apfContainer->setVisible(isCw);
    m_digContainer->setVisible(isDig);
    m_fmContainer->setVisible(isFm);
    if (isFm) {
        QSignalBlocker b1(m_fmToneModeCmb), b2(m_fmToneValueCmb), b3(m_fmOffsetSpin);
        int tmIdx = m_fmToneModeCmb->findData(m_slice->fmToneMode());
        if (tmIdx >= 0) m_fmToneModeCmb->setCurrentIndex(tmIdx);
        m_fmToneValueCmb->setEnabled(m_slice->fmToneMode() == "ctcss_tx");
        int tvIdx = m_fmToneValueCmb->findData(m_slice->fmToneValue());
        if (tvIdx >= 0) m_fmToneValueCmb->setCurrentIndex(tvIdx);
        m_fmOffsetSpin->setValue(m_slice->fmRepeaterOffsetFreq());
        QSignalBlocker b4(m_fmOffsetDown), b5(m_fmSimplexBtn), b6(m_fmOffsetUp);
        const QString& dir = m_slice->repeaterOffsetDir();
        m_fmOffsetDown->setChecked(dir == "down");
        m_fmSimplexBtn->setChecked(dir == "simplex");
        m_fmOffsetUp->setChecked(dir == "up");
    }
    if (isDig) {
        int off = (m_slice->mode() == "DIGL") ? m_slice->diglOffset() : m_slice->diguOffset();
        m_digOffsetLabel->setText(QString::number(off));
    }
    relayoutDspGrid();

    // APF level
    {
        QSignalBlocker sb(m_apfSlider);
        m_apfSlider->setValue(m_slice->apfLevel());
        m_apfValueLbl->setText(QString::number(m_slice->apfLevel()));
    }

    // DAX
    {
        QSignalBlocker sb(m_daxCmb);
        m_daxCmb->setCurrentIndex(m_slice->daxChannel());
    }

    m_updatingFromModel = false;
}

void VfoWidget::updateFreqLabel()
{
    if (!m_slice) return;
    long long hz = static_cast<long long>(std::round(m_slice->frequency() * 1e6));
    int mhzPart = static_cast<int>(hz / 1000000);
    int khzPart = static_cast<int>((hz / 1000) % 1000);
    int hzPart  = static_cast<int>(hz % 1000);
    m_freqLabel->setText(QString("%1.%2.%3")
        .arg(mhzPart)
        .arg(khzPart, 3, 10, QChar('0'))
        .arg(hzPart, 3, 10, QChar('0')));
}

void VfoWidget::updateFilterLabel()
{
    if (!m_slice) return;
    int w = m_slice->filterHigh() - m_slice->filterLow();
    if (w >= 1000)
        m_filterWidthLbl->setText(QString("%1K").arg(w / 1000.0, 0, 'f', 1));
    else
        m_filterWidthLbl->setText(QString::number(w));
}

void VfoWidget::relayoutDspGrid()
{
    // Remove all widgets from the grid (without deleting them)
    QPushButton* all[] = {m_nrBtn, m_nr2Btn, m_nbBtn, m_anfBtn, m_apfBtn, m_nrlBtn,
                          m_nrsBtn, m_rnnBtn, m_nrfBtn, m_anflBtn, m_anftBtn};
    for (auto* btn : all)
        m_dspGrid->removeWidget(btn);

    // Re-add only non-hidden buttons in 4-column rows
    int col = 0, row = 0;
    for (auto* btn : all) {
        if (!btn->isHidden()) {
            m_dspGrid->addWidget(btn, row, col);
            if (++col >= 4) { col = 0; ++row; }
        }
    }
}

// ── Mode tab helpers ──────────────────────────────────────────────────────────

struct ModeFilterPresets {
    QVector<int> filterWidths;
};

static const ModeFilterPresets& filterPresetsFor(const QString& mode)
{
    // From docs/vfo_mode_filters.csv — 8 presets per mode, 4x2 grid
    static const ModeFilterPresets usb{{1800, 2100, 2400, 2700, 2900, 3300, 4000, 6000}};
    static const ModeFilterPresets am {{5600, 6000, 8000, 10000, 12000, 14000, 16000, 20000}};
    static const ModeFilterPresets cw {{50, 100, 250, 400, 500, 800, 1000, 3000}};
    static const ModeFilterPresets dig{{100, 300, 600, 1000, 1500, 2000, 3000, 6000}};
    static const ModeFilterPresets rtty{{250, 300, 350, 400, 500, 1000, 1500, 3000}};
    static const ModeFilterPresets dfm{{6000, 8000, 10000, 12000, 14000, 16000, 18000, 20000}};
    static const ModeFilterPresets fm{{}};

    if (mode == "USB" || mode == "LSB") return usb;
    if (mode == "AM" || mode == "SAM") return am;
    if (mode == "CW") return cw;
    if (mode == "DIGU" || mode == "DIGL") return dig;
    if (mode == "RTTY") return rtty;
    if (mode == "DFM") return dfm;
    if (mode == "FM" || mode == "NFM") return fm;
    return usb;
}

void VfoWidget::updateModeTab()
{
    if (!m_slice) return;
    const QString& cur = m_slice->mode();

    // Sync combo
    {
        QSignalBlocker sb(m_modeCombo);
        int idx = m_modeCombo->findText(cur);
        if (idx >= 0) m_modeCombo->setCurrentIndex(idx);
    }

    // Rebuild filter presets for new mode
    m_filterWidths = filterPresetsFor(cur).filterWidths;
    rebuildFilterButtons();
}

QString VfoWidget::formatFilterLabel(int hz)
{
    if (hz >= 1000) return QString("%1K").arg(hz / 1000.0, 0, 'f', (hz % 1000) ? 1 : 0);
    return QString::number(hz);
}

void VfoWidget::rebuildFilterButtons()
{
    for (auto* btn : m_filterBtns) delete btn;
    m_filterBtns.clear();

    for (int i = 0; i < m_filterWidths.size(); ++i) {
        const int w = m_filterWidths[i];
        auto* btn = new QPushButton(formatFilterLabel(w));
        btn->setCheckable(true);
        btn->setFixedHeight(26);
        btn->setStyleSheet(kModeBtn);
        connect(btn, &QPushButton::clicked, this, [this, w](bool) {
            applyFilterPreset(w);
        });
        m_filterBtns.append(btn);
        m_filterGrid->addWidget(btn, i / 4, i % 4);
    }
    updateFilterHighlight();
}

void VfoWidget::updateFilterHighlight()
{
    if (!m_slice) return;
    const int width = m_slice->filterHigh() - m_slice->filterLow();
    int bestIdx = -1, bestDist = INT_MAX;
    for (int i = 0; i < m_filterWidths.size(); ++i) {
        int dist = std::abs(width - m_filterWidths[i]);
        if (dist < bestDist) { bestDist = dist; bestIdx = i; }
    }
    if (bestIdx >= 0 && bestDist > m_filterWidths[bestIdx] / 10)
        bestIdx = -1;
    for (int i = 0; i < m_filterBtns.size(); ++i) {
        QSignalBlocker sb(m_filterBtns[i]);
        m_filterBtns[i]->setChecked(i == bestIdx);
    }
}

void VfoWidget::applyFilterPreset(int widthHz)
{
    if (!m_slice) return;
    int lo, hi;
    const QString& mode = m_slice->mode();

    if (mode == "LSB" || mode == "DIGL") {
        lo = -widthHz; hi = 0;
    } else if (mode == "CW") {
        int pitch = m_txModel ? m_txModel->cwPitch() : 600;
        lo = pitch - widthHz / 2;
        hi = pitch + widthHz / 2;
    } else if (mode == "CWL") {
        int pitch = m_txModel ? m_txModel->cwPitch() : 600;
        lo = -(pitch + widthHz / 2);
        hi = -(pitch - widthHz / 2);
    } else if (mode == "AM" || mode == "SAM" || mode == "DSB") {
        lo = -(widthHz / 2); hi = (widthHz / 2);
    } else {
        lo = 0; hi = widthHz;
    }
    m_slice->setFilterWidth(lo, hi);
}

void VfoWidget::setAntennaList(const QStringList& ants)
{
    m_antList = ants;
}

void VfoWidget::setTransmitModel(TransmitModel* txModel)
{
    m_txModel = txModel;
}

bool VfoWidget::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* lbl = qobject_cast<QLabel*>(obj);
        if (lbl) {
            int idx = m_tabBtns.indexOf(lbl);
            if (idx >= 0) {
                showTab(idx);
                return true;
            }
        }
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace AetherSDR
