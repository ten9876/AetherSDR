#include "PhoneCwApplet.h"
#include "GuardedSlider.h"
#include "ComboStyle.h"
#include "HGauge.h"
#include "models/TransmitModel.h"
#include "core/AppSettings.h"

#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QPainter>
#include <QDir>
#include <QStandardPaths>

namespace AetherSDR {

// ── Triangle button (same as RxApplet) ──────────────────────────────────────

class CwTriBtn : public QPushButton {
public:
    enum Dir { Left, Right };
    explicit CwTriBtn(Dir dir, QWidget* parent = nullptr)
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
        p.setBrush(isDown() ? QColor(0, 0, 0) : QColor(0xc8, 0xd8, 0xe8));
        p.setPen(Qt::NoPen);
        const int cx = width() / 2, cy = height() / 2;
        QPolygon tri;
        if (m_dir == Left)
            tri << QPoint(cx - 5, cy) << QPoint(cx + 4, cy - 5) << QPoint(cx + 4, cy + 5);
        else
            tri << QPoint(cx + 5, cy) << QPoint(cx - 4, cy - 5) << QPoint(cx - 4, cy + 5);
        p.drawPolygon(tri);
    }
private:
    Dir m_dir;
};



// ── Style constants ──────────────────────────────────────────────────────────

static constexpr const char* kSliderStyle =
    "QSlider::groove:horizontal { height: 4px; background: #203040; border-radius: 2px; }"
    "QSlider::handle:horizontal { width: 10px; height: 10px; margin: -3px 0;"
    "background: #00b4d8; border-radius: 5px; }";

static const QString kBlueActive =
    "QPushButton:checked { background-color: #0070c0; color: #ffffff; "
    "border: 1px solid #0090e0; }";

static const QString kGreenActive =
    "QPushButton:checked { background-color: #006040; color: #00ff88; "
    "border: 1px solid #00a060; }";

static constexpr const char* kButtonBase =
    "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
    "border-radius: 3px; color: #c8d8e8; font-size: 10px; font-weight: bold; }"
    "QPushButton:hover { background: #204060; }";

static const QString kStepBtnStyle =
    "QPushButton { background-color: #1a2a3a; color: #c8d8e8; "
    "border: 1px solid #205070; border-radius: 2px; font-size: 11px; "
    "font-weight: bold; padding: 0px; }";

static constexpr const char* kLabelStyle =
    "QLabel { color: #c8d8e8; font-size: 10px; }";

static constexpr const char* kDimLabelStyle =
    "QLabel { color: #8090a0; font-size: 10px; }";

static constexpr const char* kInsetValueStyle =
    "QLabel { font-size: 10px; background: #0a0a18; border: 1px solid #1e2e3e; "
    "border-radius: 3px; padding: 1px 2px; color: #c8d8e8; }";


// ── PhoneCwApplet ────────────────────────────────────────────────────────────

PhoneCwApplet::PhoneCwApplet(QWidget* parent)
    : QWidget(parent)
{
    hide();
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // Stacked widget holding Phone (index 0) and CW (index 1) panels
    m_stack = new QStackedWidget;
    buildPhonePanel();
    buildCwPanel();
    m_stack->addWidget(m_phonePanel);
    m_stack->addWidget(m_cwPanel);
    m_stack->setCurrentIndex(0);  // Phone by default

    outer->addWidget(m_stack);
}

// ── Phone sub-panel (existing P/CW controls) ────────────────────────────────

void PhoneCwApplet::buildPhonePanel()
{
    m_phonePanel = new QWidget;
    auto* vbox = new QVBoxLayout(m_phonePanel);
    vbox->setContentsMargins(4, 2, 4, 2);
    vbox->setSpacing(2);

    // ── Level gauge (mic peak, dBFS: -40 to +10) ────────────────────────
    m_levelGauge = new HGauge(-40.0f, 10.0f, 0.0f, "Level", "dB",
        {{-40, "-40dB"}, {-30, "-30"}, {-20, "-20"}, {-10, "-10"}, {0, "0"}, {5, "+5"}, {10, "+10"}},
        nullptr, -10.0f);
    m_levelGauge->setAccessibleName("Microphone level gauge");
    m_levelGauge->setAccessibleDescription("Microphone input level in dBFS");
    vbox->addWidget(m_levelGauge);

    // ── Compression gauge (dB: -25 to 0, fills right-to-left) ───────────
    m_compGauge = new HGauge(-25.0f, 0.0f, 1.0f, "Compression", "",
        {{-25, "-25dB"}, {-20, "-20"}, {-15, "-15"}, {-10, "-10"}, {-5, "-5"}, {0, "0"}});
    m_compGauge->setReversed(true);
    m_compGauge->setAccessibleName("Compression gauge");
    m_compGauge->setAccessibleDescription("Speech compression amount in dB");
    vbox->addWidget(m_compGauge);
    vbox->addSpacing(4);

    // ── Mic profile dropdown ─────────────────────────────────────────────
    m_micProfileCombo = new GuardedComboBox;
    m_micProfileCombo->setFixedHeight(22);
    m_micProfileCombo->setAccessibleName("Microphone profile");
    m_micProfileCombo->setAccessibleDescription("Select microphone processing profile");
    AetherSDR::applyComboStyle(m_micProfileCombo);
    connect(m_micProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        if (!m_updatingFromModel && m_model) {
            QString name = m_micProfileCombo->currentText();
            if (!name.isEmpty())
                m_model->loadMicProfile(name);
        }
    });
    vbox->addWidget(m_micProfileCombo);

    // ── Mic source + level slider + +ACC ─────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        m_micSourceCombo = new GuardedComboBox;
        m_micSourceCombo->setFixedWidth(55);
        m_micSourceCombo->setFixedHeight(22);
        m_micSourceCombo->setAccessibleName("Microphone source");
        m_micSourceCombo->setAccessibleDescription("Select microphone input source");
        AetherSDR::applyComboStyle(m_micSourceCombo);
        m_micSourceCombo->addItems({"MIC", "BAL", "LINE", "ACC", "PC"});
        connect(m_micSourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this](int) {
            if (!m_updatingFromModel && m_model) {
                m_model->setMicSelection(m_micSourceCombo->currentText());
            }
        });
        row->addWidget(m_micSourceCombo);

        m_micLevelSlider = new GuardedSlider(Qt::Horizontal);
        m_micLevelSlider->setRange(0, 100);
        m_micLevelSlider->setStyleSheet(kSliderStyle);
        m_micLevelSlider->setAccessibleName("Microphone gain");
        m_micLevelSlider->setAccessibleDescription("Microphone input level, 0 to 100");
        row->addWidget(m_micLevelSlider, 1);

        m_micLevelLabel = new QLabel("50");
        m_micLevelLabel->setStyleSheet(kLabelStyle);
        m_micLevelLabel->setFixedWidth(22);
        m_micLevelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_micLevelLabel);

        m_accBtn = new QPushButton("+ACC");
        m_accBtn->setCheckable(true);
        m_accBtn->setFixedHeight(22);
        m_accBtn->setFixedWidth(48);
        m_accBtn->setAccessibleName("Accessory mic input");
        m_accBtn->setAccessibleDescription("Enable accessory microphone input");
        m_accBtn->setStyleSheet(QString(kButtonBase) + kGreenActive);
        row->addWidget(m_accBtn);

        connect(m_micLevelSlider, &QSlider::valueChanged, this, [this](int v) {
            m_micLevelLabel->setText(QString::number(v));
            if (!m_updatingFromModel && m_model)
                m_model->setMicLevel(v);
            emit micLevelChanged(v);
        });

        connect(m_accBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setMicAcc(on);
        });

        vbox->addLayout(row);
    }

    // ── PROC + NOR/DX/DX+ slider + DAX ──────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        m_procBtn = new QPushButton("PROC");
        m_procBtn->setCheckable(true);
        m_procBtn->setFixedHeight(22);
        m_procBtn->setFixedWidth(48);
        m_procBtn->setAccessibleName("Speech processor");
        m_procBtn->setAccessibleDescription("Toggle speech processor for compression");
        m_procBtn->setStyleSheet(QString(kButtonBase) + kGreenActive);
        row->addWidget(m_procBtn);

        // 3-position slider with NOR / DX / DX+ labels
        auto* procGroup = new QWidget;
        auto* procVbox = new QVBoxLayout(procGroup);
        procVbox->setContentsMargins(0, 0, 0, 0);
        procVbox->setSpacing(0);

        auto* labelsRow = new QHBoxLayout;
        labelsRow->setContentsMargins(0, 0, 0, 0);
        auto* norLbl = new QLabel("NOR");
        auto* dxLbl = new QLabel("DX");
        auto* dxPlusLbl = new QLabel("DX+");
        const QString tickLabelStyle = "QLabel { color: #c8d8e8; font-size: 8px; }";
        norLbl->setStyleSheet(tickLabelStyle);
        dxLbl->setStyleSheet(tickLabelStyle);
        dxPlusLbl->setStyleSheet(tickLabelStyle);
        norLbl->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
        dxLbl->setAlignment(Qt::AlignCenter | Qt::AlignBottom);
        dxPlusLbl->setAlignment(Qt::AlignRight | Qt::AlignBottom);
        labelsRow->addWidget(norLbl);
        labelsRow->addWidget(dxLbl);
        labelsRow->addWidget(dxPlusLbl);
        procVbox->addLayout(labelsRow);

        m_procSlider = new GuardedSlider(Qt::Horizontal);
        m_procSlider->setRange(0, 2);
        m_procSlider->setTickInterval(1);
        m_procSlider->setTickPosition(QSlider::NoTicks);
        m_procSlider->setPageStep(1);
        m_procSlider->setFixedHeight(14);
        m_procSlider->setAccessibleName("Processor level");
        m_procSlider->setAccessibleDescription("Speech processor level: Normal, DX, or DX+");
        m_procSlider->setStyleSheet(kSliderStyle);
        procVbox->addWidget(m_procSlider);

        row->addWidget(procGroup, 1);

        m_daxBtn = new QPushButton("DAX");
        m_daxBtn->setCheckable(true);
        m_daxBtn->setFixedHeight(22);
        m_daxBtn->setFixedWidth(48);
        m_daxBtn->setAccessibleName("DAX digital audio");
        m_daxBtn->setAccessibleDescription("Toggle DAX digital audio exchange");
        m_daxBtn->setStyleSheet(QString(kButtonBase) + kBlueActive);
        row->addWidget(m_daxBtn);

        connect(m_procBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setSpeechProcessorEnable(on);
        });

        connect(m_procSlider, &QSlider::valueChanged, this, [this](int pos) {
            if (!m_updatingFromModel && m_model) {
                static constexpr int kLevels[] = {0, 1, 2};
                m_model->setSpeechProcessorLevel(kLevels[qBound(0, pos, 2)]);
            }
        });

        connect(m_daxBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setDax(on);
        });

        vbox->addLayout(row);
    }

    // ── MON button + monitor volume slider ───────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        m_monBtn = new QPushButton("MON");
        m_monBtn->setCheckable(true);
        m_monBtn->setFixedHeight(22);
        m_monBtn->setFixedWidth(48);
        m_monBtn->setAccessibleName("TX monitor");
        m_monBtn->setAccessibleDescription("Toggle sidetone monitor of transmitted audio");
        m_monBtn->setStyleSheet(QString(kButtonBase) + kGreenActive);
        row->addWidget(m_monBtn);

        m_monSlider = new GuardedSlider(Qt::Horizontal);
        m_monSlider->setRange(0, 100);
        m_monSlider->setStyleSheet(kSliderStyle);
        m_monSlider->setAccessibleName("Monitor volume");
        m_monSlider->setAccessibleDescription("TX sidetone monitor volume");
        row->addWidget(m_monSlider, 1);

        m_monLabel = new QLabel("50");
        m_monLabel->setStyleSheet(kLabelStyle);
        m_monLabel->setFixedWidth(22);
        m_monLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_monLabel);

        connect(m_monBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setSbMonitor(on);
        });

        connect(m_monSlider, &QSlider::valueChanged, this, [this](int v) {
            m_monLabel->setText(QString::number(v));
            if (!m_updatingFromModel && m_model)
                m_model->setMonGainSb(v);
        });

        vbox->addLayout(row);
    }
}

// ── CW sub-panel ─────────────────────────────────────────────────────────────

void PhoneCwApplet::buildCwPanel()
{
    m_cwPanel = new QWidget;
    auto* vbox = new QVBoxLayout(m_cwPanel);
    vbox->setContentsMargins(4, 2, 4, 6);
    vbox->setSpacing(4);

    // ── ALC gauge (0–100) ────────────────────────────────────────────────
    m_alcGauge = new HGauge(0.0f, 100.0f, 80.0f, "ALC", "",
        {{0, "0"}, {25, "25"}, {50, "50"}, {75, "75"}, {100, "100"}});
    m_alcGauge->setAccessibleName("ALC gauge");
    m_alcGauge->setAccessibleDescription("Automatic level control meter");
    vbox->addWidget(m_alcGauge);
    vbox->addSpacing(2);

    // All left-side labels/buttons share the same width so sliders align.
    static constexpr int kLeftColW = 70;
    static constexpr int kValueW   = 36;
    static constexpr int kGap      = 4;   // pad between label/button and slider

    // ── Delay: label + slider + inset value ─────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        auto* lbl = new QLabel("Delay:");
        lbl->setStyleSheet("QLabel { color: #8090a0; font-size: 11px; }");
        lbl->setFixedWidth(kLeftColW);
        row->addWidget(lbl);

        row->addSpacing(kGap);

        m_delaySlider = new GuardedSlider(Qt::Horizontal);
        m_delaySlider->setRange(0, 2000);
        m_delaySlider->setSingleStep(10);
        m_delaySlider->setPageStep(100);
        m_delaySlider->setAccessibleName("CW delay");
        m_delaySlider->setAccessibleDescription("CW break-in delay in milliseconds");
        m_delaySlider->setStyleSheet(kSliderStyle);
        row->addWidget(m_delaySlider, 1);

        m_delayLabel = new QLabel("500");
        m_delayLabel->setStyleSheet(kInsetValueStyle);
        m_delayLabel->setFixedWidth(kValueW);
        m_delayLabel->setAlignment(Qt::AlignCenter);
        row->addWidget(m_delayLabel);

        connect(m_delaySlider, &QSlider::valueChanged, this, [this](int v) {
            m_delayLabel->setText(QString::number(v));
            if (!m_updatingFromModel && m_model)
                m_model->setCwDelay(v);
        });

        vbox->addLayout(row);
    }

    // ── Speed: label + slider + inset value ─────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        auto* lbl = new QLabel("Speed:");
        lbl->setStyleSheet("QLabel { color: #8090a0; font-size: 11px; }");
        lbl->setFixedWidth(kLeftColW);
        row->addWidget(lbl);

        row->addSpacing(kGap);

        m_speedSlider = new GuardedSlider(Qt::Horizontal);
        m_speedSlider->setRange(5, 100);
        m_speedSlider->setAccessibleName("CW speed");
        m_speedSlider->setAccessibleDescription("CW keying speed in words per minute");
        m_speedSlider->setStyleSheet(kSliderStyle);
        row->addWidget(m_speedSlider, 1);

        m_speedLabel = new QLabel("20");
        m_speedLabel->setStyleSheet(kInsetValueStyle);
        m_speedLabel->setFixedWidth(kValueW);
        m_speedLabel->setAlignment(Qt::AlignCenter);
        row->addWidget(m_speedLabel);

        connect(m_speedSlider, &QSlider::valueChanged, this, [this](int v) {
            m_speedLabel->setText(QString::number(v));
            if (!m_updatingFromModel && m_model)
                m_model->setCwSpeed(v);
        });

        vbox->addLayout(row);
    }

    // ── Sidetone: toggle button + slider + inset value ──────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        m_sidetoneBtn = new QPushButton("Sidetone");
        m_sidetoneBtn->setCheckable(true);
        m_sidetoneBtn->setFixedHeight(22);
        m_sidetoneBtn->setFixedWidth(kLeftColW);
        m_sidetoneBtn->setAccessibleName("CW sidetone");
        m_sidetoneBtn->setAccessibleDescription("Toggle CW sidetone monitor");
        m_sidetoneBtn->setStyleSheet(QString(kButtonBase) + kGreenActive);
        row->addWidget(m_sidetoneBtn);

        row->addSpacing(kGap);

        m_sidetoneSlider = new GuardedSlider(Qt::Horizontal);
        m_sidetoneSlider->setRange(0, 100);
        m_sidetoneSlider->setAccessibleName("Sidetone volume");
        m_sidetoneSlider->setAccessibleDescription("CW sidetone monitor volume");
        m_sidetoneSlider->setStyleSheet(kSliderStyle);
        row->addWidget(m_sidetoneSlider, 1);

        m_sidetoneLabel = new QLabel("50");
        m_sidetoneLabel->setStyleSheet(kInsetValueStyle);
        m_sidetoneLabel->setFixedWidth(kValueW);
        m_sidetoneLabel->setAlignment(Qt::AlignCenter);
        row->addWidget(m_sidetoneLabel);

        connect(m_sidetoneBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setCwSidetone(on);
        });

        connect(m_sidetoneSlider, &QSlider::valueChanged, this, [this](int v) {
            m_sidetoneLabel->setText(QString::number(v));
            if (!m_updatingFromModel && m_model)
                m_model->setMonGainCw(v);
        });

        vbox->addLayout(row);
    }

    // ── L / R pan slider ─────────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        // L/R labels sit close to the slider ends, not in the wide left column
        auto* lLbl = new QLabel("L");
        lLbl->setStyleSheet(kDimLabelStyle);
        row->addWidget(lLbl);

        m_cwPanSlider = new GuardedSlider(Qt::Horizontal);
        m_cwPanSlider->setRange(0, 100);
        m_cwPanSlider->setValue(50);
        m_cwPanSlider->setAccessibleName("CW audio pan");
        m_cwPanSlider->setAccessibleDescription("CW monitor audio pan, left to right");
        m_cwPanSlider->setStyleSheet(kSliderStyle);
        row->addWidget(m_cwPanSlider, 1);

        auto* rLbl = new QLabel("R");
        rLbl->setStyleSheet(kDimLabelStyle);
        row->addWidget(rLbl);

        // CW pan is mon_pan_cw — we'd need to add this to TransmitModel.
        // For now, wire it if/when the field is available.

        vbox->addLayout(row);
    }

    // ── Bottom row: Breakin, Iambic, Pitch stepper ──────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        m_breakinBtn = new QPushButton("Breakin");
        m_breakinBtn->setCheckable(true);
        m_breakinBtn->setFixedHeight(22);
        m_breakinBtn->setAccessibleName("CW break-in");
        m_breakinBtn->setAccessibleDescription("Toggle full break-in QSK mode");
        m_breakinBtn->setStyleSheet(QString(kButtonBase) + kGreenActive);
        row->addWidget(m_breakinBtn);

        m_iambicBtn = new QPushButton("Iambic");
        m_iambicBtn->setCheckable(true);
        m_iambicBtn->setFixedHeight(22);
        m_iambicBtn->setAccessibleName("Iambic keyer");
        m_iambicBtn->setAccessibleDescription("Toggle iambic paddle keyer mode");
        m_iambicBtn->setStyleSheet(QString(kButtonBase) + kBlueActive);
        row->addWidget(m_iambicBtn);

        row->addStretch();

        // Pitch: label + < value > stepper (inset display matching RIT/XIT style)
        auto* pitchLbl = new QLabel("Pitch:");
        pitchLbl->setStyleSheet("QLabel { color: #8090a0; font-size: 11px; }");
        row->addWidget(pitchLbl);

        m_pitchDown = new CwTriBtn(CwTriBtn::Left);
        m_pitchDown->setAccessibleName("CW pitch down");
        row->addWidget(m_pitchDown);

        m_pitchLabel = new QLabel("600");
        m_pitchLabel->setAlignment(Qt::AlignCenter);
        m_pitchLabel->setFixedWidth(48);
        m_pitchLabel->setAccessibleName("CW pitch frequency");
        m_pitchLabel->setStyleSheet(
            "QLabel { font-size: 11px; background: #0a0a18; border: 1px solid #1e2e3e; "
            "border-radius: 3px; padding: 1px 3px; color: #c8d8e8; }");
        row->addWidget(m_pitchLabel);

        m_pitchUp = new CwTriBtn(CwTriBtn::Right);
        m_pitchUp->setAccessibleName("CW pitch up");
        row->addWidget(m_pitchUp);

        connect(m_breakinBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setCwBreakIn(on);
        });

        connect(m_iambicBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_updatingFromModel && m_model)
                m_model->setCwIambic(on);
        });

        // Pitch steps by 10 Hz (matching SmartSDR).
        // Read current value from the label so rapid clicks accumulate
        // correctly without waiting for the radio roundtrip.
        connect(m_pitchDown, &QPushButton::clicked, this, [this]() {
            if (!m_model) return;
            int hz = qBound(100, m_pitchLabel->text().toInt() - 10, 6000);
            m_model->setCwPitch(hz);
            m_pitchLabel->setText(QString::number(hz));
        });
        connect(m_pitchUp, &QPushButton::clicked, this, [this]() {
            if (!m_model) return;
            int hz = qBound(100, m_pitchLabel->text().toInt() + 10, 6000);
            m_model->setCwPitch(hz);
            m_pitchLabel->setText(QString::number(hz));
        });

        vbox->addLayout(row);
    }
}

// ── Mode switching ───────────────────────────────────────────────────────────

void PhoneCwApplet::setMode(const QString& mode)
{
    // CWL is not a separate slice mode on fw v1.4.0.0 — only "CW" appears.
    bool isCw = (mode == "CW");
    m_stack->setCurrentIndex(isCw ? 1 : 0);
}

// ── Model binding ────────────────────────────────────────────────────────────

void PhoneCwApplet::setTransmitModel(TransmitModel* model)
{
    m_model = model;
    if (!m_model) return;

    // Phone signals
    connect(m_model, &TransmitModel::micStateChanged,
            this, &PhoneCwApplet::syncPhoneFromModel);

    connect(m_model, &TransmitModel::micProfileListChanged, this, [this]() {
        m_updatingFromModel = true;
        const QSignalBlocker blocker(m_micProfileCombo);
        const QString current = m_micProfileCombo->currentText();
        m_micProfileCombo->clear();
        m_micProfileCombo->addItems(m_model->micProfileList());
        int idx = m_micProfileCombo->findText(m_model->activeMicProfile());
        if (idx >= 0) m_micProfileCombo->setCurrentIndex(idx);
        else if (!current.isEmpty()) {
            idx = m_micProfileCombo->findText(current);
            if (idx >= 0) m_micProfileCombo->setCurrentIndex(idx);
        }
        m_updatingFromModel = false;
    });

    connect(m_model, &TransmitModel::micInputListChanged, this, [this]() {
        m_updatingFromModel = true;
        const QSignalBlocker blocker(m_micSourceCombo);
        const QString current = m_model->micSelection();
        m_micSourceCombo->clear();
        m_micSourceCombo->addItems(m_model->micInputList());
        int idx = m_micSourceCombo->findText(current);
        if (idx >= 0) m_micSourceCombo->setCurrentIndex(idx);
        m_updatingFromModel = false;
    });

    // CW signals — phoneStateChanged covers CW field updates too
    connect(m_model, &TransmitModel::phoneStateChanged,
            this, &PhoneCwApplet::syncCwFromModel);

    syncPhoneFromModel();
    syncCwFromModel();
}

// ── Phone sync ───────────────────────────────────────────────────────────────

void PhoneCwApplet::syncPhoneFromModel()
{
    if (!m_model) return;
    m_updatingFromModel = true;

    {
        const QSignalBlocker blocker(m_micSourceCombo);
        int idx = m_micSourceCombo->findText(m_model->micSelection());
        if (idx >= 0) m_micSourceCombo->setCurrentIndex(idx);
    }

    // PC mic gain is client-authoritative (radio always returns mic_level=0 for PC)
    if (m_model->micSelection() == "PC") {
        int pcGain = AppSettings::instance().value("PcMicGain", 100).toInt();
        m_micLevelSlider->setValue(pcGain);
        m_micLevelLabel->setText(QString::number(pcGain));
    } else {
        m_micLevelSlider->setValue(m_model->micLevel());
        m_micLevelLabel->setText(QString::number(m_model->micLevel()));
    }
    m_accBtn->setChecked(m_model->micAcc());
    m_procBtn->setChecked(m_model->speechProcessorEnable());

    {
        int level = m_model->speechProcessorLevel();
        int pos = qBound(0, level, 2);
        m_procSlider->setValue(pos);
    }

    { const QSignalBlocker b(m_daxBtn); m_daxBtn->setChecked(m_model->daxOn()); }
    m_monBtn->setChecked(m_model->sbMonitor());
    m_monSlider->setValue(m_model->monGainSb());
    m_monLabel->setText(QString::number(m_model->monGainSb()));

    {
        const QSignalBlocker blocker(m_micProfileCombo);
        int idx = m_micProfileCombo->findText(m_model->activeMicProfile());
        if (idx >= 0) m_micProfileCombo->setCurrentIndex(idx);
    }

    m_updatingFromModel = false;
}

// ── CW sync ──────────────────────────────────────────────────────────────────

void PhoneCwApplet::syncCwFromModel()
{
    if (!m_model) return;
    m_updatingFromModel = true;

    m_delaySlider->setValue(m_model->cwDelay());
    m_delayLabel->setText(QString::number(m_model->cwDelay()));

    m_speedSlider->setValue(m_model->cwSpeed());
    m_speedLabel->setText(QString::number(m_model->cwSpeed()));

    m_sidetoneBtn->setChecked(m_model->cwSidetone());
    m_sidetoneSlider->setValue(m_model->monGainCw());
    m_sidetoneLabel->setText(QString::number(m_model->monGainCw()));

    m_breakinBtn->setChecked(m_model->cwBreakIn());
    m_iambicBtn->setChecked(m_model->cwIambic());

    m_pitchLabel->setText(QString::number(m_model->cwPitch()));

    m_updatingFromModel = false;
}

// ── Meter updates ────────────────────────────────────────────────────────────

void PhoneCwApplet::updateMeters(float micLevel, float compLevel,
                                  float micPeak, float compPeak)
{
    Q_UNUSED(compLevel);
    Q_UNUSED(compPeak);

    // Suppress mic meter when met_in_rx is off and not transmitting
    if (m_model && !m_model->metInRx() && !m_model->isTransmitting()) {
        m_levelGauge->setValue(-150.0f);
        m_levelGauge->setPeakValue(-150.0f);
        return;
    }

    m_levelGauge->setValue(micLevel);
    m_levelGauge->setPeakValue(micPeak);
    // Compression gauge is now driven exclusively by updateCompression()
}

void PhoneCwApplet::updateCompression(float compPeak)
{
    // compPeak is raw dBFS from COMPPEAK meter.
    // Silence (-150) → 0. Speech (-10..+14) → clamp to -25..0 range.
    float gauge = (compPeak > -30.0f) ? qBound(-25.0f, compPeak, 0.0f) : 0.0f;
    m_compGauge->setValue(gauge);
}

void PhoneCwApplet::updateAlc(float alc)
{
    m_alcGauge->setValue(alc);
}

} // namespace AetherSDR
