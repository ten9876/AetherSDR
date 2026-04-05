#include "SpectrumOverlayMenu.h"
#include "DspParamPopup.h"
#include "SpectrumWidget.h"
#include "GuardedSlider.h"
#include "ComboStyle.h"
#include "models/SliceModel.h"
#include "models/BandDefs.h"

#include <QPushButton>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QEvent>
#include <QFrame>
#include <QColorDialog>

namespace AetherSDR {

static constexpr int BTN_W = 60;
static constexpr int BTN_H = 22;

// Band button size (slightly smaller for the grid)
static constexpr int BAND_BTN_W = 48;
static constexpr int BAND_BTN_H = 26;

static const QString kPanelStyle =
    "QWidget { background: rgba(15, 15, 26, 220); "
    "border: 1px solid #304050; border-radius: 3px; }";

static const QString kLabelStyle =
    "QLabel { background: transparent; border: none; "
    "color: #8aa8c0; font-size: 10px; font-weight: bold; }";

static const QString kSliderStyle =
    "QSlider::groove:horizontal { background: #1a2a3a; height: 4px; "
    "border-radius: 2px; }"
    "QSlider::handle:horizontal { background: #c8d8e8; width: 10px; "
    "margin: -4px 0; border-radius: 5px; }";

static const QString kMenuBtnNormal =
    "QPushButton { background: rgba(20, 30, 45, 240); "
    "border: 1px solid rgba(255, 255, 255, 40); border-radius: 2px; "
    "color: #c8d8e8; font-size: 11px; font-weight: bold; }"
    "QPushButton:hover { background: rgba(0, 112, 192, 180); "
    "border: 1px solid #0090e0; }";

static const QString kMenuBtnActive =
    "QPushButton { background: rgba(0, 112, 192, 180); "
    "border: 1px solid #0090e0; border-radius: 2px; "
    "color: #ffffff; font-size: 11px; font-weight: bold; }";

static QPushButton* makeMenuBtn(const QString& text, QWidget* parent)
{
    auto* btn = new QPushButton(text, parent);
    btn->setFixedSize(BTN_W, BTN_H);
    btn->setStyleSheet(kMenuBtnNormal);
    return btn;
}

// Band button grid uses shared BandDefs + special entries.
// Index into this array for the grid layout below.
struct BandGridEntry {
    const char* label;
    const char* bandName;  // key for BandSettings (e.g. "20m")
    double freqMhz;
    const char* mode;
};

static constexpr BandGridEntry BAND_GRID[] = {
    {"160",  "160m",  1.900,  "LSB"},   // 0
    {"80",   "80m",   3.800,  "LSB"},   // 1
    {"60",   "60m",   5.357,  "USB"},   // 2
    {"40",   "40m",   7.200,  "LSB"},   // 3
    {"30",   "30m",  10.125,  "DIGU"},  // 4
    {"20",   "20m",  14.225,  "USB"},   // 5
    {"17",   "17m",  18.130,  "USB"},   // 6
    {"15",   "15m",  21.300,  "USB"},   // 7
    {"12",   "12m",  24.950,  "USB"},   // 8
    {"10",   "10m",  28.400,  "USB"},   // 9
    {"6",    "6m",   50.150,  "USB"},   // 10
    {"WWV",  "WWV",  10.000,  "AM"},    // 11
    {"GEN",  "GEN",   0.500,  "AM"},    // 12
    {"2200", "2200m", 0.1375, "CW"},    // 13
    {"630",  "630m",  0.475,  "CW"},    // 14
    {"XVTR", "",      0.0,    ""},      // 15
};

SpectrumOverlayMenu::SpectrumOverlayMenu(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);

    // Toggle button (arrow)
    m_toggleBtn = new QPushButton(this);
    m_toggleBtn->setFixedSize(BTN_W, BTN_H);
    m_toggleBtn->setStyleSheet(
        "QPushButton { background: rgba(20, 30, 45, 240); "
        "border: 1px solid rgba(255, 255, 255, 40); border-radius: 2px; "
        "color: #c8d8e8; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(0, 112, 192, 180); "
        "border: 1px solid #0090e0; }");
    connect(m_toggleBtn, &QPushButton::clicked, this, &SpectrumOverlayMenu::toggle);

    // Menu buttons — Band, ANT, DSP handled specially (sub-panels)
    struct BtnDef { QString text; int specialIdx; void (SpectrumOverlayMenu::*sig)(); };
    const BtnDef defs[] = {
        {"+RX",      -1, nullptr},   // 0 — handled separately (signal has panId arg)
        {"+TNF",     -1, &SpectrumOverlayMenu::addTnfClicked},  // 1
        {"Band",      0, nullptr},   // 2 — toggleBandPanel
        {"ANT",       1, nullptr},   // 3 — toggleAntPanel
        {"DSP",       2, nullptr},   // 4 — toggleDspPanel
        {"Display",   4, nullptr}, // 5 — toggleDisplayPanel
        {"DAX",       3, nullptr},   // 6 — toggleDaxPanel
    };

    for (const auto& def : defs) {
        auto* btn = makeMenuBtn(def.text, this);
        if (def.specialIdx == 0)
            connect(btn, &QPushButton::clicked, this, &SpectrumOverlayMenu::toggleBandPanel);
        else if (def.specialIdx == 1)
            connect(btn, &QPushButton::clicked, this, &SpectrumOverlayMenu::toggleAntPanel);
        else if (def.specialIdx == 2)
            connect(btn, &QPushButton::clicked, this, &SpectrumOverlayMenu::toggleDspPanel);
        else if (def.specialIdx == 3)
            connect(btn, &QPushButton::clicked, this, &SpectrumOverlayMenu::toggleDaxPanel);
        else if (def.specialIdx == 4)
            connect(btn, &QPushButton::clicked, this, &SpectrumOverlayMenu::toggleDisplayPanel);
        else if (def.text == "+RX")
            connect(btn, &QPushButton::clicked, this, [this]() { emit addRxClicked(m_panId); });
        else if (def.sig)
            connect(btn, &QPushButton::clicked, this, def.sig);
        m_menuBtns.append(btn);
    }

    // Menu button tooltips
    if (m_menuBtns.size() >= 7) {
        m_menuBtns[0]->setToolTip("Add a new receive slice on this panadapter.");
        m_menuBtns[1]->setToolTip("Add a tracking notch filter at the current frequency.");
        m_menuBtns[2]->setToolTip("Open band selector.");
        m_menuBtns[3]->setToolTip("Open antenna and RF gain controls.");
        m_menuBtns[4]->setToolTip("Open DSP noise reduction and filter controls.");
        m_menuBtns[5]->setToolTip("Open panadapter and waterfall display settings.");
        m_menuBtns[6]->setToolTip("Open DAX audio routing channel selector.");
    }

    buildBandPanel();
    buildAntPanel();
    buildDspPanel();
    buildDaxPanel();
    buildDisplayPanel();

    // Prevent mouse/wheel events from falling through panels to the spectrum
    for (auto* panel : {m_bandPanel, m_antPanel, m_dspPanel, m_daxPanel, m_displayPanel})
        if (panel) panel->installEventFilter(this);

    updateLayout();
}

void SpectrumOverlayMenu::raiseAll()
{
    raise();
    if (m_bandPanel)    m_bandPanel->raise();
    if (m_xvtrPanel)    m_xvtrPanel->raise();
    if (m_antPanel)     m_antPanel->raise();
    if (m_dspPanel)     m_dspPanel->raise();
    if (m_daxPanel)     m_daxPanel->raise();
    if (m_displayPanel) m_displayPanel->raise();
}

// ── Band sub-panel ────────────────────────────────────────────────────────────

void SpectrumOverlayMenu::buildBandPanel()
{
    m_bandPanel = new QWidget(parentWidget());
    m_bandPanel->setStyleSheet("QWidget { background: rgba(15, 15, 26, 220); "
                                "border: 1px solid #304050; border-radius: 3px; }");
    m_bandPanel->hide();

    auto* grid = new QGridLayout(m_bandPanel);
    grid->setContentsMargins(2, 2, 2, 2);
    grid->setSpacing(2);

    const QString bandBtnStyle =
        "QPushButton { background: rgba(30, 40, 55, 220); "
        "border: 1px solid #304050; border-radius: 3px; "
        "color: #c8d8e8; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(0, 112, 192, 180); "
        "border: 1px solid #0090e0; }";

    constexpr int layout[][3] = {
        {0, 1, 2},      // 160, 80, 60
        {3, 4, 5},      // 40, 30, 20
        {6, 7, 8},      // 17, 15, 12
        {9, 10, -1},    // 10, 6
        {11, 12, -1},   // WWV, GEN
        {13, 14, 15},   // 2200, 630, XVTR
    };

    for (int row = 0; row < 6; ++row) {
        for (int col = 0; col < 3; ++col) {
            int idx = layout[row][col];
            if (idx < 0) continue;

            auto* btn = new QPushButton(BAND_GRID[idx].label, m_bandPanel);
            btn->setFixedSize(BAND_BTN_W, BAND_BTN_H);
            btn->setStyleSheet(bandBtnStyle);

            QString bandName = QString::fromLatin1(BAND_GRID[idx].bandName);
            double freq = BAND_GRID[idx].freqMhz;
            QString mode = QString::fromLatin1(BAND_GRID[idx].mode);
            if (idx == 15) {
                // XVTR button → open Radio Setup XVTR tab (#571)
                connect(btn, &QPushButton::clicked, this, [this]() {
                    m_bandPanel->hide();
                    m_bandPanelVisible = false;
                    emit xvtrSetupRequested();
                });
            } else if (bandName.isEmpty()) {
                btn->setEnabled(false);
            } else {
                connect(btn, &QPushButton::clicked, this, [this, bandName, freq, mode]() {
                    emit bandSelected(bandName, freq, mode);
                });
            }

            grid->addWidget(btn, row, col);
        }
    }

    m_bandPanel->adjustSize();
}

// ── ANT sub-panel ─────────────────────────────────────────────────────────────

void SpectrumOverlayMenu::buildAntPanel()
{
    m_antPanel = new QWidget(parentWidget());
    m_antPanel->setStyleSheet(kPanelStyle);
    m_antPanel->hide();

    auto* vbox = new QVBoxLayout(m_antPanel);
    vbox->setContentsMargins(6, 6, 6, 6);
    vbox->setSpacing(4);

    constexpr int kLabelW = 48;
    constexpr int kValueW = 20;

    // RX ANT row
    auto* antRow = new QHBoxLayout;
    antRow->setSpacing(4);
    auto* antLabel = new QLabel("RX ANT:");
    antLabel->setStyleSheet(kLabelStyle);
    antLabel->setFixedWidth(kLabelW);
    antRow->addWidget(antLabel);
    m_rxAntCmb = new GuardedComboBox;
    AetherSDR::applyComboStyle(m_rxAntCmb);
    antRow->addWidget(m_rxAntCmb, 1);
    vbox->addLayout(antRow);

    connect(m_rxAntCmb, &QComboBox::currentTextChanged,
            this, [this](const QString& ant) {
        if (!m_updatingFromModel && m_slice && !ant.isEmpty())
            m_slice->setRxAntenna(ant);
    });

    // RF Gain row
    auto* gainRow = new QHBoxLayout;
    gainRow->setSpacing(4);
    auto* gainLabel = new QLabel("RF Gain:");
    gainLabel->setStyleSheet(kLabelStyle);
    gainLabel->setFixedWidth(kLabelW);
    gainRow->addWidget(gainLabel);
    m_rfGainSlider = new GuardedSlider(Qt::Horizontal);
    m_rfGainSlider->setRange(-8, 32);
    m_rfGainSlider->setSingleStep(8);
    m_rfGainSlider->setPageStep(8);
    m_rfGainSlider->setTickInterval(8);
    m_rfGainSlider->setTickPosition(QSlider::TicksBelow);
    m_rfGainSlider->setStyleSheet(kSliderStyle);
    gainRow->addWidget(m_rfGainSlider, 1);
    m_rfGainLabel = new QLabel("0");
    m_rfGainLabel->setStyleSheet(kLabelStyle);
    m_rfGainLabel->setFixedWidth(kValueW);
    m_rfGainLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    gainRow->addWidget(m_rfGainLabel);
    vbox->addLayout(gainRow);

    connect(m_rfGainSlider, &QSlider::valueChanged, this, [this](int v) {
        // Snap to nearest multiple of step size
        int step = m_rfGainSlider->singleStep();
        if (step < 1) step = 1;
        int snapped = qRound(static_cast<double>(v) / step) * step;
        if (snapped != v) {
            QSignalBlocker sb(m_rfGainSlider);
            m_rfGainSlider->setValue(snapped);
        }
        m_rfGainLabel->setText(QString::number(snapped));
        if (!m_updatingFromModel)
            emit rfGainChanged(snapped);
    });

    // WNB row: toggle button + level slider
    auto* wnbRow = new QHBoxLayout;
    wnbRow->setSpacing(4);
    m_wnbBtn = new QPushButton("WNB");
    m_wnbBtn->setCheckable(true);
    m_wnbBtn->setFixedSize(48, 22);
    m_wnbBtn->setStyleSheet(
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 2px; color: #c8d8e8; font-size: 11px; font-weight: bold; }"
        "QPushButton:checked { background: #0070c0; color: #ffffff; "
        "border: 1px solid #0090e0; }"
        "QPushButton:hover { border: 1px solid #0090e0; }");
    wnbRow->addWidget(m_wnbBtn);
    m_wnbSlider = new GuardedSlider(Qt::Horizontal);
    m_wnbSlider->setRange(0, 100);
    m_wnbSlider->setValue(50);
    m_wnbSlider->setStyleSheet(kSliderStyle);
    wnbRow->addWidget(m_wnbSlider, 1);
    m_wnbLabel = new QLabel("50");
    m_wnbLabel->setStyleSheet(kLabelStyle);
    m_wnbLabel->setFixedWidth(kValueW);
    m_wnbLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    wnbRow->addWidget(m_wnbLabel);
    vbox->addLayout(wnbRow);

    connect(m_wnbBtn, &QPushButton::toggled, this, &SpectrumOverlayMenu::wnbToggled);
    connect(m_wnbSlider, &QSlider::valueChanged, this, [this](int v) {
        m_wnbLabel->setText(QString::number(v));
        emit wnbLevelChanged(v);
    });

    // ANT panel tooltips
    m_rxAntCmb->setToolTip("Select the receive antenna port for this slice.");
    m_rfGainSlider->setToolTip("Adjusts receiver IF gain. Lower values reduce strong-signal overload.");
    m_wnbBtn->setToolTip("Wideband noise blanker \u2014 suppresses correlated impulse noise across the full panadapter bandwidth.");
    m_wnbSlider->setToolTip("Adjusts WNB threshold. Higher values blank more aggressively.");

    m_antPanel->setFixedWidth(180);
    m_antPanel->adjustSize();
}

void SpectrumOverlayMenu::setAntennaList(const QStringList& ants)
{
    m_antList = ants;
    QSignalBlocker sb(m_rxAntCmb);
    QString cur = m_rxAntCmb->currentText();
    m_rxAntCmb->clear();
    m_rxAntCmb->addItems(ants);
    if (ants.contains(cur))
        m_rxAntCmb->setCurrentText(cur);
}

void SpectrumOverlayMenu::setSlice(SliceModel* slice)
{
    if (m_slice)
        m_slice->disconnect(this);
    m_slice = slice;
    if (!m_slice) return;

    connect(m_slice, &SliceModel::rxAntennaChanged, this, [this](const QString& ant) {
        m_updatingFromModel = true;
        m_rxAntCmb->setCurrentText(ant);
        m_updatingFromModel = false;
    });

    // DSP toggle connections
    using S = SliceModel;
    struct DspToggleDef {
        void (S::*setter)(bool);
        void (S::*signal)(bool);
    };
    const DspToggleDef toggleDefs[] = {
        {&S::setNb,   &S::nbChanged},    // 0
        {&S::setNr,   &S::nrChanged},    // 1
        {nullptr,     nullptr},           // 2 — NR2 (client-side, wired separately)
        {&S::setAnf,  &S::anfChanged},   // 3
        {&S::setNrl,  &S::nrlChanged},   // 4
        {&S::setNrs,  &S::nrsChanged},   // 5
        {&S::setRnn,  &S::rnnChanged},   // 6
        {nullptr,     nullptr},           // 7 — RN2 (client-side, wired separately)
        {&S::setNrf,  &S::nrfChanged},   // 8
        {&S::setAnfl, &S::anflChanged},  // 9
        {&S::setAnft, &S::anftChanged},  // 10
        {nullptr,     nullptr},           // 11 — BNR (client-side, wired separately)
        {nullptr,     nullptr},           // 12 — NR4 (client-side, wired separately)
    };

    for (int i = 0; i < 12; ++i) {
        if (!toggleDefs[i].setter) continue;  // skip NR2/RN2
        auto* btn = m_dspRows[i].btn;
        auto setter = toggleDefs[i].setter;
        auto signal = toggleDefs[i].signal;

        connect(btn, &QPushButton::toggled, this, [this, setter](bool on) {
            if (!m_updatingFromModel && m_slice)
                (m_slice->*setter)(on);
        });
        connect(m_slice, signal, this, [this, i](bool on) {
            m_updatingFromModel = true;
            QSignalBlocker sb(m_dspRows[i].btn);
            m_dspRows[i].btn->setChecked(on);
            m_updatingFromModel = false;
        });
    }

    // DSP level connections (only for features with sliders)
    struct DspLevelDef {
        int row;
        void (S::*setter)(int);
        void (S::*signal)(int);
    };
    const DspLevelDef levelDefs[] = {
        {0, &S::setNbLevel,   &S::nbLevelChanged},
        {1, &S::setNrLevel,   &S::nrLevelChanged},
        // NR2 (index 2) has no level slider
        {3, &S::setAnfLevel,  &S::anfLevelChanged},
        {4, &S::setNrlLevel,  &S::nrlLevelChanged},
        {5, &S::setNrsLevel,  &S::nrsLevelChanged},
        // RN2 (index 7) has no level slider
        {8, &S::setNrfLevel,  &S::nrfLevelChanged},
        {9, &S::setAnflLevel, &S::anflLevelChanged},
    };

    for (const auto& ld : levelDefs) {
        auto* slider = m_dspRows[ld.row].slider;
        auto* lbl = m_dspRows[ld.row].valueLbl;
        auto setter = ld.setter;
        int row = ld.row;

        connect(slider, &QSlider::valueChanged, this, [this, setter, lbl](int v) {
            lbl->setText(QString::number(v));
            if (!m_updatingFromModel && m_slice)
                (m_slice->*setter)(v);
        });
        connect(m_slice, ld.signal, this, [this, row](int v) {
            m_updatingFromModel = true;
            QSignalBlocker sb(m_dspRows[row].slider);
            m_dspRows[row].slider->setValue(v);
            m_dspRows[row].valueLbl->setText(QString::number(v));
            m_updatingFromModel = false;
        });
    }

    // NR2 (client-side, index 2) — emit signal for MainWindow to handle
    connect(m_dspRows[2].btn, &QPushButton::toggled, this, [this](bool on) {
        if (!m_updatingFromModel)
            emit nr2Toggled(on);
    });
    // NR2 right-click → parameter popup
    m_dspRows[2].btn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_dspRows[2].btn, &QPushButton::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        emit nr2RightClicked(m_dspRows[2].btn->mapToGlobal(pos));
    });

    // RN2 (client-side, index 7) — emit signal for MainWindow to handle
    connect(m_dspRows[7].btn, &QPushButton::toggled, this, [this](bool on) {
        if (!m_updatingFromModel)
            emit rn2Toggled(on);
    });

    // BNR (client-side, index 11) — emit signal for MainWindow to handle
    connect(m_dspRows[11].btn, &QPushButton::toggled, this, [this](bool on) {
        if (!m_updatingFromModel)
            emit bnrToggled(on);
    });
    if (m_dspRows[11].slider) {
        m_dspRows[11].slider->setRange(0, 100);
        m_dspRows[11].slider->setValue(100);  // default: max denoising
        if (m_dspRows[11].valueLbl)
            m_dspRows[11].valueLbl->setText("100");
        connect(m_dspRows[11].slider, &QSlider::valueChanged, this, [this](int v) {
            if (m_dspRows[11].valueLbl)
                m_dspRows[11].valueLbl->setText(QString::number(v));
            if (!m_updatingFromModel)
                emit bnrIntensityChanged(v / 100.0f);
        });
    }
#ifndef HAVE_BNR
    m_dspRows[11].btn->setVisible(false);
    if (m_dspRows[11].slider) m_dspRows[11].slider->setVisible(false);
    if (m_dspRows[11].valueLbl) m_dspRows[11].valueLbl->setVisible(false);
#endif

    // NR4 (client-side, index 12) — emit signal for MainWindow to handle
    connect(m_dspRows[12].btn, &QPushButton::toggled, this, [this](bool on) {
        if (!m_updatingFromModel)
            emit nr4Toggled(on);
    });
    // NR4 right-click → parameter popup
    m_dspRows[12].btn->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_dspRows[12].btn, &QPushButton::customContextMenuRequested,
            this, [this](const QPoint& pos) {
        emit nr4RightClicked(m_dspRows[12].btn->mapToGlobal(pos));
    });
#ifndef HAVE_SPECBLEACH
    m_dspRows[12].btn->setVisible(false);
#endif

    // DAX
    connect(m_slice, &SliceModel::daxChannelChanged, this, [this](int ch) {
        m_updatingFromModel = true;
        QSignalBlocker sb(m_daxCmb);
        m_daxCmb->setCurrentIndex(ch);
        m_updatingFromModel = false;
    });

    syncAntPanel();
    syncDspPanel();
    syncDaxPanel();
}

void SpectrumOverlayMenu::syncAntPanel()
{
    if (!m_slice) return;
    m_updatingFromModel = true;
    m_rxAntCmb->setCurrentText(m_slice->rxAntenna());
    m_updatingFromModel = false;
}

// ── DSP sub-panel ─────────────────────────────────────────────────────────────

void SpectrumOverlayMenu::buildDspPanel()
{
    m_dspPanel = new QWidget(parentWidget());
    m_dspPanel->setStyleSheet(kPanelStyle);
    m_dspPanel->hide();

    auto* vbox = new QVBoxLayout(m_dspPanel);
    vbox->setContentsMargins(6, 6, 6, 6);
    vbox->setSpacing(3);

    const QString dspBtnStyle =
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 2px; color: #c8d8e8; font-size: 10px; font-weight: bold; "
        "padding: 1px 2px; }"
        "QPushButton:checked { background: #1a6030; color: #ffffff; "
        "border: 1px solid #20a040; }"
        "QPushButton:hover { border: 1px solid #0090e0; }";

    // DSP feature definitions
    struct DspDef {
        const char* label;
        bool hasLevel;
    };
    const DspDef defs[] = {
        {"NB",   true},   // 0
        {"NR",   true},   // 1
        {"NR2",  false},  // 2  — client-side spectral NR
        {"ANF",  true},   // 3
        {"NRL",  true},   // 4
        {"NRS",  true},   // 5
        {"RNN",  false},  // 6
        {"RN2",  false},  // 7  — client-side RNNoise
        {"NRF",  true},   // 8
        {"ANFL", true},   // 9
        {"ANFT", false},  // 10
        {"BNR",  true},   // 11 — client-side NVIDIA NIM (intensity slider)
        {"NR4",  false},  // 12 — client-side spectral bleach (libspecbleach)
    };

    // First pass: create all buttons and collect toggle-only (no slider) indices
    QVector<int> toggleOnlyIndices;
    constexpr int N = sizeof(defs) / sizeof(defs[0]);
    for (int i = 0; i < N; ++i) {
        DspRow dspRow;
        auto* btn = new QPushButton(defs[i].label);
        btn->setCheckable(true);
        btn->setFixedSize(40, 20);
        btn->setStyleSheet(dspBtnStyle);
        dspRow.btn = btn;
        m_dspRows.append(dspRow);
        if (!defs[i].hasLevel)
            toggleOnlyIndices.append(i);
    }

    // Toggle-only buttons in a compact flow row at the top
    if (!toggleOnlyIndices.isEmpty()) {
        auto* toggleRow = new QHBoxLayout;
        toggleRow->setSpacing(4);
        for (int idx : toggleOnlyIndices) {
            m_dspRows[idx].btn->setMinimumSize(0, 20);
            m_dspRows[idx].btn->setMaximumHeight(20);
            m_dspRows[idx].btn->setFixedWidth(QWIDGETSIZE_MAX);  // undo fixed 40px
            toggleRow->addWidget(m_dspRows[idx].btn, 1);
        }
        vbox->addLayout(toggleRow);
    }

    // Slider rows for buttons with level controls
    for (int i = 0; i < N; ++i) {
        if (!defs[i].hasLevel) continue;

        auto* row = new QHBoxLayout;
        row->setSpacing(4);
        row->addWidget(m_dspRows[i].btn);

        auto* slider = new GuardedSlider(Qt::Horizontal);
        slider->setRange(0, 100);
        slider->setValue(50);
        slider->setStyleSheet(kSliderStyle);
        row->addWidget(slider, 1);

        auto* lbl = new QLabel("50");
        lbl->setStyleSheet(kLabelStyle);
        lbl->setFixedWidth(22);
        lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(lbl);

        slider->installEventFilter(this);
        m_dspRows[i].slider = slider;
        m_dspRows[i].valueLbl = lbl;

        vbox->addLayout(row);
    }

    // DSP button tooltips
    m_dspRows[0].btn->setToolTip("Noise blanker \u2014 detects and removes fast impulse noise from sparks and switching sources.");
    m_dspRows[1].btn->setToolTip("Radio-side noise reduction \u2014 attenuates uncorrelated background noise.");
    m_dspRows[2].btn->setToolTip("Client-side spectral noise reduction (Ephraim-Malah MMSE). Right-click for NR2 settings.");
    m_dspRows[3].btn->setToolTip("Auto notch filter \u2014 detects and cancels persistent unwanted tones.");
    m_dspRows[4].btn->setToolTip("Leaky LMS adaptive filter \u2014 preserves correlated signals while removing uncorrelated noise. Best for daily SSB/CW.");
    m_dspRows[5].btn->setToolTip("Spectral subtraction with voice activity detection \u2014 cuts noise most aggressively between words.");
    m_dspRows[6].btn->setToolTip("Deep-learning recurrent neural network \u2014 separates speech from complex noise. Best at low SNR.");
    m_dspRows[7].btn->setToolTip("Client-side RNNoise neural noise suppression. Effective for broadband noise on voice modes.");
    m_dspRows[8].btn->setToolTip("Spectral subtraction filter \u2014 computes speech/noise probability per frequency bin to remove steady noise.");
    m_dspRows[9].btn->setToolTip("Leaky LMS notch filter \u2014 removes steady tones such as power-line hum or carriers.");
    m_dspRows[10].btn->setToolTip("FFT-based notch filter \u2014 removes up to five persistent tones from transformers or power supplies.");
    m_dspRows[11].btn->setToolTip("NVIDIA GPU-accelerated neural audio denoising. Requires NVIDIA RTX 4000+ with Docker.");
    m_dspRows[12].btn->setToolTip("Client-side spectral bleach noise reduction (libspecbleach). Right-click for NR4 settings.");

    // DSP slider tooltips
    if (m_dspRows[0].slider) m_dspRows[0].slider->setToolTip("Adjusts blanking depth. Higher values suppress more impulse noise but may clip desired signals.");
    if (m_dspRows[1].slider) m_dspRows[1].slider->setToolTip("Adjusts noise reduction depth. Higher values remove more noise but may affect signal clarity.");
    if (m_dspRows[3].slider) m_dspRows[3].slider->setToolTip("Adjusts notch depth. Higher values attenuate tones more aggressively.");
    if (m_dspRows[4].slider) m_dspRows[4].slider->setToolTip("Adjusts NRL filter strength.");
    if (m_dspRows[5].slider) m_dspRows[5].slider->setToolTip("Adjusts NRS suppression depth.");
    if (m_dspRows[8].slider) m_dspRows[8].slider->setToolTip("Adjusts NRF filter strength.");
    if (m_dspRows[9].slider) m_dspRows[9].slider->setToolTip("Adjusts ANFL notch depth.");
    if (m_dspRows[11].slider) m_dspRows[11].slider->setToolTip("Adjusts BNR denoising intensity.");

    m_dspPanel->setFixedWidth(200);
    m_dspPanel->adjustSize();

    // Wiring is done in setSlice() since we need the slice model
}

void SpectrumOverlayMenu::syncDspPanel()
{
    if (!m_slice) return;
    m_updatingFromModel = true;

    // Toggle states
    m_dspRows[0].btn->setChecked(m_slice->nbOn());
    m_dspRows[1].btn->setChecked(m_slice->nrOn());
    // NR2 (index 2) is client-side — synced via AudioEngine, not slice
    m_dspRows[3].btn->setChecked(m_slice->anfOn());
    m_dspRows[4].btn->setChecked(m_slice->nrlOn());
    m_dspRows[5].btn->setChecked(m_slice->nrsOn());
    m_dspRows[6].btn->setChecked(m_slice->rnnOn());
    // RN2 (index 7) is client-side — synced via AudioEngine, not slice
    m_dspRows[8].btn->setChecked(m_slice->nrfOn());
    m_dspRows[9].btn->setChecked(m_slice->anflOn());
    m_dspRows[10].btn->setChecked(m_slice->anftOn());

    // Level values (only for features that have sliders)
    auto syncSlider = [](DspRow& r, int v) {
        if (r.slider) { r.slider->setValue(v); r.valueLbl->setText(QString::number(v)); }
    };
    syncSlider(m_dspRows[0], m_slice->nbLevel());
    syncSlider(m_dspRows[1], m_slice->nrLevel());
    syncSlider(m_dspRows[3], m_slice->anfLevel());
    syncSlider(m_dspRows[4], m_slice->nrlLevel());
    syncSlider(m_dspRows[5], m_slice->nrsLevel());
    syncSlider(m_dspRows[8], m_slice->nrfLevel());
    syncSlider(m_dspRows[9], m_slice->anflLevel());

    m_updatingFromModel = false;
}

void SpectrumOverlayMenu::toggleDspPanel()
{
    bool wasVisible = m_dspPanelVisible;
    hideAllSubPanels();
    if (!wasVisible) {
        syncDspPanel();
        m_dspPanelVisible = true;
        // Center vertically on the DSP button (index 4)
        int btnCenterY = m_menuBtns[4]->y() + m_menuBtns[4]->height() / 2;
        // Top-align DSP panel with the button menu
        int panelY = y();
        m_dspPanel->move(x() + width(), std::max(0, panelY));
        m_dspPanel->raise();
        m_dspPanel->show();
        m_menuBtns[4]->setStyleSheet(kMenuBtnActive);
    }
}

// ── DAX sub-panel ─────────────────────────────────────────────────────────────

void SpectrumOverlayMenu::buildDaxPanel()
{
    m_daxPanel = new QWidget(parentWidget());
    m_daxPanel->setStyleSheet(kPanelStyle);
    m_daxPanel->hide();

    auto* vb = new QVBoxLayout(m_daxPanel);
    vb->setContentsMargins(6, 6, 6, 6);
    vb->setSpacing(4);

    auto* row = new QHBoxLayout;
    row->setSpacing(4);
    auto* lbl = new QLabel("DAX Ch");
    lbl->setStyleSheet(kLabelStyle);
    row->addWidget(lbl);
    m_daxCmb = new GuardedComboBox;
    m_daxCmb->addItems({"Off", "1", "2", "3", "4"});
    AetherSDR::applyComboStyle(m_daxCmb);
    row->addWidget(m_daxCmb, 1);
    vb->addLayout(row);

    connect(m_daxCmb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (!m_updatingFromModel && m_slice)
            m_slice->setDaxChannel(idx);
    });

    auto* iqRow = new QHBoxLayout;
    iqRow->setSpacing(4);
    auto* iqLbl = new QLabel("IQ Ch");
    iqLbl->setStyleSheet(kLabelStyle);
    iqRow->addWidget(iqLbl);
    m_daxIqCmb = new GuardedComboBox;
    m_daxIqCmb->addItems({"None", "1", "2", "3", "4"});
    AetherSDR::applyComboStyle(m_daxIqCmb);
    iqRow->addWidget(m_daxIqCmb, 1);
    vb->addLayout(iqRow);

    connect(m_daxIqCmb, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        if (!m_updatingFromModel)
            emit daxIqChannelChanged(idx);
    });

    m_daxPanel->setFixedWidth(140);
    m_daxPanel->adjustSize();
}

void SpectrumOverlayMenu::syncDaxPanel()
{
    if (!m_slice) return;
    m_updatingFromModel = true;
    QSignalBlocker sb(m_daxCmb);
    m_daxCmb->setCurrentIndex(m_slice->daxChannel());
    m_updatingFromModel = false;
}

void SpectrumOverlayMenu::toggleDaxPanel()
{
    bool wasVisible = m_daxPanelVisible;
    hideAllSubPanels();
    if (!wasVisible) {
        syncDaxPanel();
        m_daxPanelVisible = true;
        int btnCenterY = m_menuBtns[6]->y() + m_menuBtns[6]->height() / 2;
        int panelY = y() + btnCenterY - m_daxPanel->sizeHint().height() / 2;
        m_daxPanel->move(x() + width(), std::max(0, panelY));
        m_daxPanel->raise();
        m_daxPanel->show();
        m_menuBtns[6]->setStyleSheet(kMenuBtnActive);
    }
}

// ── Sub-panel toggle helpers ──────────────────────────────────────────────────

void SpectrumOverlayMenu::hideAllSubPanels()
{
    if (m_bandPanelVisible)    { m_bandPanelVisible = false;    m_bandPanel->hide(); }
    if (m_xvtrPanelVisible)    { m_xvtrPanelVisible = false;    if (m_xvtrPanel) m_xvtrPanel->hide(); }
    if (m_antPanelVisible)     { m_antPanelVisible = false;     m_antPanel->hide(); }
    if (m_dspPanelVisible)     { m_dspPanelVisible = false;     m_dspPanel->hide(); }
    if (m_daxPanelVisible)     { m_daxPanelVisible = false;     m_daxPanel->hide(); }
    if (m_displayPanelVisible) { m_displayPanelVisible = false; m_displayPanel->hide(); }
    m_menuBtns[2]->setStyleSheet(kMenuBtnNormal);  // Band
    m_menuBtns[3]->setStyleSheet(kMenuBtnNormal);  // ANT
    m_menuBtns[4]->setStyleSheet(kMenuBtnNormal);  // DSP
    m_menuBtns[5]->setStyleSheet(kMenuBtnNormal);  // Display
    m_menuBtns[6]->setStyleSheet(kMenuBtnNormal);  // DAX
}

void SpectrumOverlayMenu::toggleBandPanel()
{
    bool wasVisible = m_bandPanelVisible;
    hideAllSubPanels();
    if (!wasVisible) {
        m_bandPanelVisible = true;
        m_bandPanel->move(x() + width(), y());
        m_bandPanel->raise();
        m_bandPanel->show();
        m_menuBtns[2]->setStyleSheet(kMenuBtnActive);
    }
}

void SpectrumOverlayMenu::toggleAntPanel()
{
    bool wasVisible = m_antPanelVisible;
    hideAllSubPanels();
    if (!wasVisible) {
        syncAntPanel();
        m_antPanelVisible = true;
        // Center vertically on the ANT button (index 3)
        int antBtnCenterY = m_menuBtns[3]->y() + m_menuBtns[3]->height() / 2;
        int panelY = y() + antBtnCenterY - m_antPanel->sizeHint().height() / 2;
        m_antPanel->move(x() + width(), std::max(0, panelY));
        m_antPanel->raise();
        m_antPanel->show();
        m_menuBtns[3]->setStyleSheet(kMenuBtnActive);
    }
}

// ── Main menu toggle and layout ───────────────────────────────────────────────

void SpectrumOverlayMenu::toggle()
{
    m_expanded = !m_expanded;
    if (!m_expanded)
        hideAllSubPanels();
    updateLayout();
}

void SpectrumOverlayMenu::updateLayout()
{
    constexpr int pad = 2;
    constexpr int gap = 2;

    m_toggleBtn->setText(m_expanded ? QStringLiteral("\u2190") : QStringLiteral("\u2192"));
    m_toggleBtn->move(pad, pad);

    int y = pad + BTN_H + gap;
    for (auto* btn : m_menuBtns) {
        btn->setVisible(m_expanded);
        if (m_expanded) {
            btn->move(pad, y);
            y += BTN_H + gap;
        }
    }

    int totalH = m_expanded ? (pad + BTN_H + gap + m_menuBtns.size() * (BTN_H + gap))
                            : (pad + BTN_H + pad);
    setFixedSize(pad + BTN_W + pad, totalH);
}

// ── Display sub-panel ─────────────────────────────────────────────────────────

void SpectrumOverlayMenu::buildDisplayPanel()
{
    m_displayPanel = new QWidget(parentWidget());
    m_displayPanel->setStyleSheet("QWidget { background: rgba(15, 15, 26, 220); "
                                   "border: 1px solid #304050; border-radius: 3px; }");
    m_displayPanel->hide();

    auto* grid = new QGridLayout(m_displayPanel);
    grid->setContentsMargins(8, 6, 8, 6);
    grid->setSpacing(4);
    grid->setColumnStretch(1, 1);

    auto labelStyle = QStringLiteral("QLabel { color: #8090a0; font-size: 10px; border: none; }");
    auto valStyle   = QStringLiteral("QLabel { color: #c8d8e8; font-size: 10px; border: none;"
                                      " min-width: 24px; }");
    auto sliderStyle = QStringLiteral(
        "QSlider { border: none; }"
        "QSlider::groove:horizontal { height: 4px; background: #203040; border-radius: 2px; }"
        "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -3px 0;"
        " background: #00b4d8; border-radius: 5px; }");
    auto btnStyle = QStringLiteral(
        "QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #205070;"
        " border-radius: 3px; font-size: 10px; font-weight: bold; padding: 2px 6px; }"
        "QPushButton:hover { background: #204060; }"
        "QPushButton:checked { background: #006040; color: #00ff88; border-color: #00a060; }");

    int row = 0;
    // Grid columns: 0=label, 1=button (optional), 2=slider, 3=value
    grid->setColumnMinimumWidth(1, 22);  // button column

    // Helper: label col 0, slider col 2, value col 3
    auto makeRow = [&](const QString& text, int lo, int hi, int def,
                       QSlider*& slider, QLabel*& valLbl) {
        auto* lbl = new QLabel(text);
        lbl->setStyleSheet(labelStyle);
        grid->addWidget(lbl, row, 0);

        slider = new GuardedSlider(Qt::Horizontal);
        slider->setRange(lo, hi);
        slider->setValue(def);
        slider->setStyleSheet(sliderStyle);
        grid->addWidget(slider, row, 2);

        valLbl = new QLabel(QString::number(def));
        valLbl->setStyleSheet(valStyle);
        valLbl->setFixedWidth(28);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(valLbl, row, 3);
        ++row;
    };

    // Helper: label col 0, button col 1, slider col 2, value col 3
    auto makeRowWithBtn = [&](const QString& text, int lo, int hi, int def,
                              QSlider*& slider, QLabel*& valLbl,
                              QPushButton*& btn, const QString& btnText,
                              int btnW = 36) {
        auto* lbl = new QLabel(text);
        lbl->setStyleSheet(labelStyle);
        grid->addWidget(lbl, row, 0);

        btn = new QPushButton(btnText);
        btn->setCheckable(true);
        btn->setFixedSize(btnW, 18);
        btn->setStyleSheet(btnStyle);
        grid->addWidget(btn, row, 1);

        slider = new GuardedSlider(Qt::Horizontal);
        slider->setRange(lo, hi);
        slider->setValue(def);
        slider->setStyleSheet(sliderStyle);
        grid->addWidget(slider, row, 2);

        valLbl = new QLabel(QString::number(def));
        valLbl->setStyleSheet(valStyle);
        valLbl->setFixedWidth(28);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(valLbl, row, 3);
        ++row;
    };

    // ── FFT section ───────────────────────────────────────────────────────
    makeRow("AVG:", 0, 100, 0, m_avgSlider, m_avgLabel);
    connect(m_avgSlider, &QSlider::valueChanged, this, [this](int v) {
        m_avgLabel->setText(QString::number(v));
        emit fftAverageChanged(v);
    });

    makeRow("FPS:", 5, 30, 25, m_fpsSlider, m_fpsLabel);
    connect(m_fpsSlider, &QSlider::valueChanged, this, [this](int v) {
        m_fpsLabel->setText(QString::number(v));
        emit fftFpsChanged(v);
    });

    // Fill row: color picker button in col 1, slider in col 2
    {
        auto* lbl = new QLabel("Fill:");
        lbl->setStyleSheet(labelStyle);
        grid->addWidget(lbl, row, 0);

        m_fillColorBtn = new QPushButton;
        m_fillColorBtn->setFixedSize(18, 18);
        m_fillColorBtn->setStyleSheet(
            QString("QPushButton { background: %1; border: 1px solid #506070;"
                    " border-radius: 2px; }")
                .arg(m_fillColor.name()));
        m_fillColorBtn->setToolTip("Choose fill color");
        grid->addWidget(m_fillColorBtn, row, 1);

        m_fillSlider = new GuardedSlider(Qt::Horizontal);
        m_fillSlider->setRange(0, 100);
        m_fillSlider->setValue(70);
        m_fillSlider->setStyleSheet(sliderStyle);
        grid->addWidget(m_fillSlider, row, 2);

        m_fillLabel = new QLabel("70");
        m_fillLabel->setStyleSheet(valStyle);
        m_fillLabel->setFixedWidth(28);
        m_fillLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(m_fillLabel, row, 3);
        ++row;

        connect(m_fillSlider, &QSlider::valueChanged, this, [this](int v) {
            m_fillLabel->setText(QString::number(v));
            emit fftFillAlphaChanged(v / 100.0f);
        });
        connect(m_fillColorBtn, &QPushButton::clicked, this, [this] {
            QColor c = QColorDialog::getColor(m_fillColor, this, "FFT Fill Color",
                                               QColorDialog::DontUseNativeDialog);
            if (c.isValid()) {
                m_fillColor = c;
                m_fillColorBtn->setStyleSheet(
                    QString("QPushButton { background: %1; border: 1px solid #506070;"
                            " border-radius: 2px; }")
                        .arg(c.name()));
                emit fftFillColorChanged(c);
            }
        });
    }

    // Heat Map toggle
    {
        auto* lbl = new QLabel("Heat Map:");
        lbl->setStyleSheet(labelStyle);
        grid->addWidget(lbl, row, 0, 1, 2);

        m_heatMapBtn = new QPushButton("Off");
        m_heatMapBtn->setCheckable(true);
        m_heatMapBtn->setFixedSize(36, 18);
        m_heatMapBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; color: #8090a0; border: 1px solid #304050;"
            " border-radius: 3px; font-size: 10px; font-weight: bold; }"
            "QPushButton:checked { background: #006040; color: #00ff88; border: 1px solid #00a060; }");
        grid->addWidget(m_heatMapBtn, row, 2, 1, 2);
        ++row;

        connect(m_heatMapBtn, &QPushButton::toggled, this, [this](bool on) {
            m_heatMapBtn->setText(on ? "On" : "Off");
            emit fftHeatMapChanged(on);
        });
    }

    // Weighted Average: label spans col 0-1, button in col 2
    {
        auto* waLbl = new QLabel("Weighted Average:");
        waLbl->setStyleSheet(labelStyle);
        grid->addWidget(waLbl, row, 0, 1, 2);  // span label + button cols
        m_weightedAvgBtn = new QPushButton("Off");
        m_weightedAvgBtn->setCheckable(true);
        m_weightedAvgBtn->setChecked(false);
        m_weightedAvgBtn->setFixedSize(36, 18);
        m_weightedAvgBtn->setStyleSheet(btnStyle);
        grid->addWidget(m_weightedAvgBtn, row, 3, Qt::AlignRight);
        connect(m_weightedAvgBtn, &QPushButton::toggled, this, [this](bool on) {
            m_weightedAvgBtn->setText(on ? "On" : "Off");
            emit fftWeightedAverageChanged(on);
        });
        ++row;
    }

    // ── Separator ─────────────────────────────────────────────────────────
    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("QFrame { color: #304050; border: none; }");
    sep->setFixedHeight(2);
    grid->addWidget(sep, row, 0, 1, 4);
    ++row;

    // ── Waterfall section ─────────────────────────────────────────────────
    // Color scheme selector
    {
        auto* lbl = new QLabel("Scheme:");
        lbl->setStyleSheet(labelStyle);
        grid->addWidget(lbl, row, 0, 1, 2);
        m_colorSchemeCmb = new QComboBox;
        m_colorSchemeCmb->setFixedHeight(18);
        m_colorSchemeCmb->setStyleSheet(comboStyleSheet());
        for (int i = 0; i < static_cast<int>(WfColorScheme::Count); ++i)
            m_colorSchemeCmb->addItem(wfSchemeName(static_cast<WfColorScheme>(i)));
        grid->addWidget(m_colorSchemeCmb, row, 2, 1, 2);
        connect(m_colorSchemeCmb, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int idx) { emit wfColorSchemeChanged(idx); });
        ++row;
    }

    makeRow("Gain:", 0, 100, 50, m_gainSlider, m_gainLabel);
    connect(m_gainSlider, &QSlider::valueChanged, this, [this](int v) {
        m_gainLabel->setText(QString::number(v));
        emit wfColorGainChanged(v);
    });

    // Black + Auto: Auto button in col 1
    makeRowWithBtn("Black:", 0, 100, 15, m_blackSlider, m_blackLabel,
                   m_autoBlackBtn, "Auto");
    m_autoBlackBtn->setChecked(true);
    connect(m_blackSlider, &QSlider::valueChanged, this, [this](int v) {
        m_blackLabel->setText(QString::number(v));
        emit wfBlackLevelChanged(v);
    });
    connect(m_autoBlackBtn, &QPushButton::toggled, this, [this](bool on) {
        emit wfAutoBlackChanged(on);
        if (!on)
            emit wfBlackLevelChanged(m_blackSlider->value());
    });

    makeRow("Rate:", 1, 30, 15, m_rateSlider, m_rateLabel);
    connect(m_rateSlider, &QSlider::valueChanged, this, [this](int v) {
        m_rateLabel->setText(QString::number(v));
        emit wfLineDurationChanged(v + 70);  // map 1-30 → 71-100
    });

    // NB Waterfall Blanker (#277) — uses same grid layout as other controls
    makeRow("NB Blank:", 5, 95, 15, m_wfBlankerThreshSlider, m_wfBlankerThreshLabel);
    m_wfBlankerBtn = new QPushButton("Off");
    m_wfBlankerBtn->setCheckable(true);
    m_wfBlankerBtn->setChecked(false);
    m_wfBlankerBtn->setFixedSize(36, 18);
    m_wfBlankerBtn->setStyleSheet(btnStyle);
    m_wfBlankerBtn->setToolTip("Suppress impulse noise stripes in waterfall");
    // Replace the label cell with the toggle button
    grid->addWidget(m_wfBlankerBtn, row - 1, 1);

    connect(m_wfBlankerBtn, &QPushButton::toggled, this, [this](bool on) {
        m_wfBlankerBtn->setText(on ? "On" : "Off");
        emit wfBlankerEnabledChanged(on);
    });
    connect(m_wfBlankerThreshSlider, &QSlider::valueChanged, this, [this](int v) {
        float t = 1.0f + v / 100.0f;
        m_wfBlankerThreshLabel->setText(QString::number(t, 'f', 2));
        emit wfBlankerThresholdChanged(t);
    });

    // Cursor frequency label toggle (#456)
    {
        auto* lbl = new QLabel("Cursor Freq:");
        lbl->setStyleSheet(labelStyle);
        grid->addWidget(lbl, row, 0, 1, 2);
        m_cursorFreqBtn = new QPushButton("Off");
        m_cursorFreqBtn->setCheckable(true);
        m_cursorFreqBtn->setChecked(false);
        m_cursorFreqBtn->setFixedSize(36, 18);
        m_cursorFreqBtn->setStyleSheet(btnStyle);
        grid->addWidget(m_cursorFreqBtn, row, 3, Qt::AlignRight);
        connect(m_cursorFreqBtn, &QPushButton::toggled, this, [this](bool on) {
            m_cursorFreqBtn->setText(on ? "On" : "Off");
            emit cursorFreqToggled(on);
        });
        ++row;
    }

    // Background image (#611)
    {
        auto* lbl = new QLabel("Background:");
        lbl->setStyleSheet(labelStyle);
        grid->addWidget(lbl, row, 0, 1, 2);
        auto* bgBtn = new QPushButton("Choose...");
        bgBtn->setFixedSize(60, 18);
        bgBtn->setStyleSheet(btnStyle);
        connect(bgBtn, &QPushButton::clicked, this, [this] {
            emit backgroundImageRequested();
        });
        grid->addWidget(bgBtn, row, 2);
        auto* clearBtn = new QPushButton("Clear");
        clearBtn->setFixedSize(42, 18);
        clearBtn->setStyleSheet(btnStyle);
        connect(clearBtn, &QPushButton::clicked, this, [this] {
            emit backgroundImageCleared();
        });
        grid->addWidget(clearBtn, row, 3);
        ++row;

        // Opacity slider
        auto* opacLbl = new QLabel("Bg Opacity:");
        opacLbl->setStyleSheet(labelStyle);
        grid->addWidget(opacLbl, row, 0, 1, 2);
        m_bgOpacitySlider = new GuardedSlider(Qt::Horizontal);
        m_bgOpacitySlider->setRange(0, 100);
        m_bgOpacitySlider->setValue(80);
        m_bgOpacitySlider->setStyleSheet(sliderStyle);
        grid->addWidget(m_bgOpacitySlider, row, 2);
        m_bgOpacityLabel = new QLabel("80");
        m_bgOpacityLabel->setStyleSheet(valStyle);
        m_bgOpacityLabel->setFixedWidth(28);
        m_bgOpacityLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(m_bgOpacityLabel, row, 3);
        connect(m_bgOpacitySlider, &QSlider::valueChanged, this, [this](int v) {
            m_bgOpacityLabel->setText(QString::number(v));
            emit backgroundOpacityChanged(v);
        });
        ++row;
    }

    // Display panel tooltips
    m_avgSlider->setToolTip("FFT frame averaging. Higher values smooth the spectrum trace but reduce time resolution.");
    m_fpsSlider->setToolTip("FFT refresh rate in frames per second.");
    m_fillSlider->setToolTip("Opacity of the spectrum fill area below the trace.");
    if (m_heatMapBtn) m_heatMapBtn->setToolTip("Colors the spectrum trace by signal strength instead of a single color.");
    if (m_weightedAvgBtn) m_weightedAvgBtn->setToolTip("Weights recent FFT frames more heavily for faster response to signal changes.");
    m_gainSlider->setToolTip("Waterfall color gain. Higher values brighten weak signals.");
    m_blackSlider->setToolTip("Waterfall black level. Increase to darken the noise floor.");
    if (m_autoBlackBtn) m_autoBlackBtn->setToolTip("Automatically adjusts the waterfall black level to match the current noise floor.");
    m_rateSlider->setToolTip("Waterfall line duration. Lower values scroll faster.");
    if (m_wfBlankerThreshSlider) m_wfBlankerThreshSlider->setToolTip("Waterfall noise blanking threshold. Higher values blank more aggressively.");
    if (m_colorSchemeCmb) m_colorSchemeCmb->setToolTip("Selects the waterfall color palette.");
    if (m_cursorFreqBtn) m_cursorFreqBtn->setToolTip("Shows the frequency at the mouse cursor position on the panadapter.");
    if (m_bgOpacitySlider) m_bgOpacitySlider->setToolTip("Opacity of the background image overlay.");
    if (m_floorEnableBtn) m_floorEnableBtn->setToolTip("Shows a noise floor reference line on the spectrum display.");
    if (m_floorSlider) m_floorSlider->setToolTip("Vertical position of the noise floor reference line.");

    m_displayPanel->adjustSize();
}

void SpectrumOverlayMenu::syncDisplaySettings(int avg, int fps, int fillPct,
                                               bool weightedAvg, const QColor& fillColor,
                                               int gain, int black, bool autoBlack, int rate,
                                               int floorPos, bool floorEnable,
                                               bool heatMap, int colorScheme)
{
    if (!m_avgSlider) return;  // panel not built yet

    QSignalBlocker b1(m_avgSlider), b2(m_fpsSlider), b3(m_fillSlider),
                   b4(m_weightedAvgBtn), b5(m_gainSlider), b6(m_blackSlider),
                   b7(m_autoBlackBtn), b8(m_rateSlider);

    m_avgSlider->setValue(avg);
    m_avgLabel->setText(QString::number(avg));
    m_fpsSlider->setValue(fps);
    m_fpsLabel->setText(QString::number(fps));
    m_fillSlider->setValue(fillPct);
    m_fillLabel->setText(QString::number(fillPct));
    m_weightedAvgBtn->setChecked(weightedAvg);
    m_weightedAvgBtn->setText(weightedAvg ? "On" : "Off");
    m_fillColor = fillColor;
    m_fillColorBtn->setStyleSheet(
        QString("QPushButton { background: %1; border: 1px solid #506070;"
                " border-radius: 2px; }").arg(fillColor.name()));
    m_gainSlider->setValue(gain);
    m_gainLabel->setText(QString::number(gain));
    m_blackSlider->setValue(black);
    m_blackLabel->setText(QString::number(black));
    m_autoBlackBtn->setChecked(autoBlack);
    int rateSliderVal = std::clamp(rate - 70, 1, 30);  // line_duration 71-100 → slider 1-30
    m_rateSlider->setValue(rateSliderVal);
    m_rateLabel->setText(QString::number(rateSliderVal));

    if (m_floorSlider) {
        QSignalBlocker bf(m_floorSlider), be(m_floorEnableBtn);
        m_floorSlider->setValue(floorPos);
        m_floorLabel->setText(QString::number(floorPos));
        m_floorEnableBtn->setChecked(floorEnable);
        m_floorEnableBtn->setText(floorEnable ? "On" : "Off");
        m_floorSlider->setEnabled(floorEnable);
    }
    if (m_heatMapBtn) {
        QSignalBlocker bh(m_heatMapBtn);
        m_heatMapBtn->setChecked(heatMap);
        m_heatMapBtn->setText(heatMap ? "On" : "Off");
    }
    if (m_colorSchemeCmb) {
        QSignalBlocker bc(m_colorSchemeCmb);
        m_colorSchemeCmb->setCurrentIndex(colorScheme);
    }
}

void SpectrumOverlayMenu::toggleDisplayPanel()
{
    bool wasVisible = m_displayPanelVisible;
    hideAllSubPanels();
    if (!wasVisible) {
        m_displayPanelVisible = true;
        int menuBottom = y() + height();
        int panelH = m_displayPanel->sizeHint().height();
        int panelY = menuBottom - panelH;
        m_displayPanel->move(x() + width(), std::max(0, panelY));
        m_displayPanel->raise();
        m_displayPanel->show();
        m_menuBtns[5]->setStyleSheet(kMenuBtnActive);
    }
}

void SpectrumOverlayMenu::setWnbState(bool on, int level)
{
    QSignalBlocker b1(m_wnbBtn), b2(m_wnbSlider);
    m_wnbBtn->setChecked(on);
    m_wnbSlider->setValue(level);
    m_wnbLabel->setText(QString::number(level));
}

void SpectrumOverlayMenu::setRfGain(int gain)
{
    QSignalBlocker b(m_rfGainSlider);
    m_rfGainSlider->setValue(gain);
    m_rfGainLabel->setText(QString::number(gain));
}

void SpectrumOverlayMenu::setRfGainRange(int low, int high, int step)
{
    if (!m_rfGainSlider) return;
    QSignalBlocker b(m_rfGainSlider);
    m_rfGainSlider->setRange(low, high);
    m_rfGainSlider->setSingleStep(step);
    m_rfGainSlider->setPageStep(step);
    m_rfGainSlider->setTickInterval(step);
}

void SpectrumOverlayMenu::setXvtrBands(const QVector<XvtrBand>& bands)
{
    // Remove old XVTR band buttons from main band panel
    for (auto* btn : m_xvtrBandBtns)
        btn->deleteLater();
    m_xvtrBandBtns.clear();

    // Rebuild the main band panel to insert XVTR bands between
    // HF bands and utility buttons (WWV/GEN/2200/630/XVTR). (#571)
    if (m_bandPanel) {
        // Delete existing band panel and rebuild
        m_bandPanel->deleteLater();
        m_bandPanel = nullptr;
    }

    m_bandPanel = new QWidget(parentWidget());
    m_bandPanel->setStyleSheet("QWidget { background: rgba(15, 15, 26, 220); "
                                "border: 1px solid #304050; border-radius: 3px; }");
    m_bandPanel->hide();

    auto* grid = new QGridLayout(m_bandPanel);
    grid->setContentsMargins(2, 2, 2, 2);
    grid->setSpacing(2);

    const QString bandBtnStyle =
        "QPushButton { background: rgba(30, 40, 55, 220); "
        "border: 1px solid #304050; border-radius: 3px; "
        "color: #c8d8e8; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(0, 112, 192, 180); "
        "border: 1px solid #0090e0; }";

    const QString xvtrBtnStyle =
        "QPushButton { background: rgba(30, 40, 55, 220); "
        "border: 1px solid #304050; border-radius: 3px; "
        "color: #00d0ff; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(0, 112, 192, 180); "
        "border: 1px solid #0090e0; }";

    // HF bands (indices 0-10)
    constexpr int hfLayout[][3] = {
        {0, 1, 2},      // 160, 80, 60
        {3, 4, 5},      // 40, 30, 20
        {6, 7, 8},      // 17, 15, 12
        {9, 10, -1},    // 10, 6
    };

    int row = 0;
    for (int r = 0; r < 4; ++r) {
        for (int col = 0; col < 3; ++col) {
            int idx = hfLayout[r][col];
            if (idx < 0) continue;
            auto* btn = new QPushButton(BAND_GRID[idx].label, m_bandPanel);
            btn->setFixedSize(BAND_BTN_W, BAND_BTN_H);
            btn->setStyleSheet(bandBtnStyle);
            QString bandName = QString::fromLatin1(BAND_GRID[idx].bandName);
            double freq = BAND_GRID[idx].freqMhz;
            QString mode = QString::fromLatin1(BAND_GRID[idx].mode);
            connect(btn, &QPushButton::clicked, this, [this, bandName, freq, mode]() {
                emit bandSelected(bandName, freq, mode);
            });
            grid->addWidget(btn, row, col);
        }
        ++row;
    }

    // XVTR bands (inserted between HF and utility)
    m_xvtrBandBtns.clear();
    for (int i = 0; i < bands.size(); ++i) {
        auto* btn = new QPushButton(bands[i].name, m_bandPanel);
        btn->setFixedSize(BAND_BTN_W, BAND_BTN_H);
        btn->setStyleSheet(xvtrBtnStyle);
        const double freq = bands[i].rfFreqMhz;
        const QString name = bands[i].name;
        connect(btn, &QPushButton::clicked, this, [this, name, freq]() {
            emit bandSelected(name, freq, "USB");
            m_bandPanel->hide();
            m_bandPanelVisible = false;
        });
        grid->addWidget(btn, row + i / 3, i % 3);
        m_xvtrBandBtns.append(btn);
    }
    if (!bands.isEmpty())
        row += (bands.size() + 2) / 3;  // advance past XVTR rows

    // Utility buttons: WWV, GEN, 2200, 630, XVTR config
    constexpr int utilLayout[][3] = {
        {11, 12, -1},   // WWV, GEN
        {13, 14, 15},   // 2200, 630, XVTR
    };
    for (int r = 0; r < 2; ++r) {
        for (int col = 0; col < 3; ++col) {
            int idx = utilLayout[r][col];
            if (idx < 0) continue;
            auto* btn = new QPushButton(BAND_GRID[idx].label, m_bandPanel);
            btn->setFixedSize(BAND_BTN_W, BAND_BTN_H);
            btn->setStyleSheet(bandBtnStyle);
            QString bandName = QString::fromLatin1(BAND_GRID[idx].bandName);
            double freq = BAND_GRID[idx].freqMhz;
            QString mode = QString::fromLatin1(BAND_GRID[idx].mode);
            if (idx == 15) {
                connect(btn, &QPushButton::clicked, this, [this]() {
                    m_bandPanel->hide();
                    m_bandPanelVisible = false;
                    emit xvtrSetupRequested();
                });
            } else if (bandName.isEmpty()) {
                btn->setEnabled(false);
            } else {
                connect(btn, &QPushButton::clicked, this, [this, bandName, freq, mode]() {
                    emit bandSelected(bandName, freq, mode);
                });
            }
            grid->addWidget(btn, row, col);
        }
        ++row;
    }

    m_bandPanel->adjustSize();

    // Rebuild the XVTR sub-panel (kept as fallback)
    if (m_xvtrPanel) {
        m_xvtrPanel->deleteLater();
        m_xvtrPanel = nullptr;
    }

    m_xvtrPanel = new QWidget(parentWidget());
    m_xvtrPanel->setStyleSheet("QWidget { background: rgba(15, 15, 26, 220); "
                                "border: 1px solid #304050; border-radius: 3px; }");
    m_xvtrPanel->hide();
    m_xvtrPanel->installEventFilter(this);

    auto* xvGrid = new QGridLayout(m_xvtrPanel);
    xvGrid->setContentsMargins(2, 2, 2, 2);
    xvGrid->setSpacing(2);

    const QString btnStyle =
        "QPushButton { background: rgba(30, 40, 55, 220); "
        "border: 1px solid #304050; border-radius: 3px; "
        "color: #c8d8e8; font-size: 11px; font-weight: bold; }"
        "QPushButton:hover { background: rgba(0, 112, 192, 180); "
        "border: 1px solid #0090e0; }";

    static constexpr int XVTR_COLS = 2;
    static constexpr int XVTR_MIN_ROWS = 4;
    int totalSlots = qMax(XVTR_MIN_ROWS * XVTR_COLS,
                          (bands.size() + 1 + XVTR_COLS - 1) / XVTR_COLS * XVTR_COLS);

    const QString disabledStyle = btnStyle +
        "QPushButton:disabled { background: rgba(15, 15, 26, 180); "
        "color: #252535; border: 1px solid #1a1a2a; }";

    // Fill grid: configured bands first, then empty slots, HF button last
    int slot = 0;
    for (const auto& xvtr : bands) {
        auto* btn = new QPushButton(xvtr.name, m_xvtrPanel);
        btn->setFixedSize(BAND_BTN_W, BAND_BTN_H);
        btn->setStyleSheet(btnStyle);

        const double freq = xvtr.rfFreqMhz;
        const QString name = xvtr.name;
        connect(btn, &QPushButton::clicked, this, [this, name, freq]() {
            emit bandSelected(name, freq, "FM");
            m_xvtrPanel->hide();
            m_xvtrPanelVisible = false;
        });

        xvGrid->addWidget(btn, slot / XVTR_COLS, slot % XVTR_COLS);
        ++slot;
    }

    // Fill remaining slots (except last) with blank disabled buttons
    while (slot < totalSlots - 1) {
        auto* blank = new QPushButton("", m_xvtrPanel);
        blank->setFixedSize(BAND_BTN_W, BAND_BTN_H);
        blank->setStyleSheet(disabledStyle);
        blank->setEnabled(false);
        xvGrid->addWidget(blank, slot / XVTR_COLS, slot % XVTR_COLS);
        ++slot;
    }

    // HF button in last slot (bottom-right)
    auto* hfBtn = new QPushButton("HF", m_xvtrPanel);
    hfBtn->setFixedSize(BAND_BTN_W, BAND_BTN_H);
    hfBtn->setStyleSheet(btnStyle);
    connect(hfBtn, &QPushButton::clicked, this, [this]() {
        m_xvtrPanel->hide();
        m_xvtrPanelVisible = false;
        m_bandPanel->move(m_xvtrPanel->pos());
        m_bandPanel->show();
        m_bandPanel->raise();
        m_bandPanelVisible = true;
    });
    xvGrid->addWidget(hfBtn, slot / XVTR_COLS, slot % XVTR_COLS);

    m_xvtrPanel->adjustSize();
}

QPushButton* SpectrumOverlayMenu::dspNr2Button() const
{
    return m_dspRows.size() > 2 ? m_dspRows[2].btn : nullptr;
}

QPushButton* SpectrumOverlayMenu::dspRn2Button() const
{
    return m_dspRows.size() > 7 ? m_dspRows[7].btn : nullptr;
}

QPushButton* SpectrumOverlayMenu::dspBnrButton() const
{
    return m_dspRows.size() > 11 ? m_dspRows[11].btn : nullptr;
}

QPushButton* SpectrumOverlayMenu::dspNr4Button() const
{
    return m_dspRows.size() > 12 ? m_dspRows[12].btn : nullptr;
}

bool SpectrumOverlayMenu::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonDblClick) {
        if (auto* slider = qobject_cast<QSlider*>(obj)) {
            slider->setValue(50);
            return true;
        }
    }
    // Consume mouse/wheel events on sub-panels so they don't reach the spectrum
    if (obj == m_bandPanel || obj == m_antPanel || obj == m_dspPanel
        || obj == m_daxPanel || obj == m_displayPanel) {
        if (event->type() == QEvent::Wheel
            || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease) {
            return true;  // consumed
        }
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace AetherSDR
