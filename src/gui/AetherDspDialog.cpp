#include "AetherDspDialog.h"
#include "core/AudioEngine.h"
#include "core/AppSettings.h"
#include "GuardedSlider.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QRadioButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>

namespace AetherSDR {

static const QString kDialogStyle = QStringLiteral(
    "QDialog { background: #0f0f1a; color: #c8d8e8; }"
    "QTabWidget::pane { border: 1px solid #304050; background: #0f0f1a; }"
    "QTabBar::tab { background: #1a2a3a; color: #8090a0; padding: 6px 16px;"
    "  border: 1px solid #304050; border-bottom: none; border-radius: 3px 3px 0 0; }"
    "QTabBar::tab:selected { background: #0f0f1a; color: #c8d8e8;"
    "  border-bottom: 1px solid #0f0f1a; }"
    "QGroupBox { border: 1px solid #304050; border-radius: 4px;"
    "  margin-top: 12px; padding-top: 8px; color: #8090a0; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }"
    "QLabel { color: #8090a0; }"
    "QRadioButton { color: #c8d8e8; }"
    "QCheckBox { color: #c8d8e8; }"
    "QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050;"
    "  border-radius: 3px; padding: 4px 12px; }"
    "QPushButton:hover { background: rgba(0, 112, 192, 180); border: 1px solid #0090e0; }");

static const QString kSliderStyle = QStringLiteral(
    "QSlider::groove:horizontal { height: 4px; background: #304050; border-radius: 2px; }"
    "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0;"
    "  background: #c8d8e8; border-radius: 6px; }"
    "QSlider::handle:horizontal:hover { background: #00b4d8; }");

AetherDspDialog::AetherDspDialog(AudioEngine* audio, QWidget* parent)
    : QDialog(parent)
    , m_audio(audio)
{
    setWindowTitle("AetherDSP Settings");
    setMinimumSize(420, 380);
    setStyleSheet(kDialogStyle);

    auto* root = new QVBoxLayout(this);

    auto* title = new QLabel("AetherDSP Settings");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: #c8d8e8; }");
    root->addWidget(title);
    root->addSpacing(4);

    auto* tabs = new QTabWidget;
    buildNr2Tab(tabs);
    buildNr4Tab(tabs);
    buildDfnrTab(tabs);
    buildRn2Tab(tabs);
    buildBnrTab(tabs);
    root->addWidget(tabs);

    syncFromEngine();
}

// ── NR2 Tab ──────────────────────────────────────────────────────────────────

void AetherDspDialog::buildNr2Tab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);

    auto labelStyle = QStringLiteral("QLabel { color: #8090a0; font-size: 11px; }");
    auto valStyle   = QStringLiteral("QLabel { color: #c8d8e8; font-size: 11px; min-width: 40px; }");

    // Gain Method
    auto* gainGroup = new QGroupBox("Gain Method");
    auto* gainLayout = new QHBoxLayout(gainGroup);
    m_nr2GainGroup = new QButtonGroup(this);
    const char* gainLabels[] = {"Linear", "Log", "Gamma", "Trained"};
    for (int i = 0; i < 4; ++i) {
        auto* rb = new QRadioButton(gainLabels[i]);
        m_nr2GainGroup->addButton(rb, i);
        gainLayout->addWidget(rb);
    }
    m_nr2GainGroup->button(0)->setToolTip("Linear audio amplitude scale for gain computation.");
    m_nr2GainGroup->button(1)->setToolTip("Logarithmic amplitude scale \u2014 compresses dynamic range.");
    m_nr2GainGroup->button(2)->setToolTip("Gamma distribution model matching typical speech amplitude patterns.");
    m_nr2GainGroup->button(3)->setToolTip("Noise reduction model trained on real speech and noise samples.");
    m_nr2GainGroup->button(2)->setChecked(true);  // Gamma default
    connect(m_nr2GainGroup, &QButtonGroup::idClicked, this, [this](int id) {
        auto& s = AppSettings::instance();
        s.setValue("NR2GainMethod", QString::number(id));
        s.save();
        emit nr2GainMethodChanged(id);
    });
    vbox->addWidget(gainGroup);

    // NPE Method
    auto* npeGroup = new QGroupBox("NPE Method");
    auto* npeLayout = new QHBoxLayout(npeGroup);
    m_nr2NpeGroup = new QButtonGroup(this);
    const char* npeLabels[] = {"OSMS", "MMSE", "NSTAT"};
    for (int i = 0; i < 3; ++i) {
        auto* rb = new QRadioButton(npeLabels[i]);
        m_nr2NpeGroup->addButton(rb, i);
        npeLayout->addWidget(rb);
    }
    m_nr2NpeGroup->button(0)->setToolTip("Optimal Smoothing Minimum Statistics \u2014 tracks noise floor using a running minimum estimate.");
    m_nr2NpeGroup->button(1)->setToolTip("Minimum Mean Squared Error \u2014 minimizes the expected noise estimation error.");
    m_nr2NpeGroup->button(2)->setToolTip("Non-Stationary estimation \u2014 adapts to noise that changes over time.");
    m_nr2NpeGroup->button(0)->setChecked(true);  // OSMS default
    connect(m_nr2NpeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        auto& s = AppSettings::instance();
        s.setValue("NR2NpeMethod", QString::number(id));
        s.save();
        emit nr2NpeMethodChanged(id);
    });
    vbox->addWidget(npeGroup);

    // AE Filter
    m_nr2AeCheck = new QCheckBox("AE Filter (artifact elimination)");
    m_nr2AeCheck->setToolTip("Reduces ringing and musical artifacts typical of frequency-domain noise reduction.");
    m_nr2AeCheck->setChecked(true);
    connect(m_nr2AeCheck, &QCheckBox::toggled, this, [this](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("NR2AeFilter", on ? "True" : "False");
        s.save();
        emit nr2AeFilterChanged(on);
    });
    vbox->addWidget(m_nr2AeCheck);

    // Sliders: GainMax, GainSmooth, Q_SPP
    auto* sliderGrid = new QGridLayout;
    int row = 0;

    // Gain Max (reduction depth)
    {
        auto* lbl = new QLabel("Reduction Depth:");
        lbl->setStyleSheet(labelStyle);
        sliderGrid->addWidget(lbl, row, 0);
        m_nr2GainMaxSlider = new GuardedSlider(Qt::Horizontal);
        m_nr2GainMaxSlider->setRange(50, 200);  // 0.50–2.00
        m_nr2GainMaxSlider->setValue(150);       // default 1.50
        m_nr2GainMaxSlider->setStyleSheet(kSliderStyle);
        m_nr2GainMaxSlider->setToolTip("Maximum noise reduction depth. Higher values suppress more noise but risk distorting speech.");
        sliderGrid->addWidget(m_nr2GainMaxSlider, row, 1);
        m_nr2GainMaxLabel = new QLabel("1.50");
        m_nr2GainMaxLabel->setStyleSheet(valStyle);
        m_nr2GainMaxLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sliderGrid->addWidget(m_nr2GainMaxLabel, row, 2);
        connect(m_nr2GainMaxSlider, &QSlider::valueChanged, this, [this](int v) {
            float val = v / 100.0f;
            m_nr2GainMaxLabel->setText(QString::number(val, 'f', 2));
            auto& s = AppSettings::instance();
            s.setValue("NR2GainMax", QString::number(val, 'f', 2));
            s.save();
            emit nr2GainMaxChanged(val);
        });
        ++row;
    }

    // Gain Smooth
    {
        auto* lbl = new QLabel("Smoothing:");
        lbl->setStyleSheet(labelStyle);
        sliderGrid->addWidget(lbl, row, 0);
        m_nr2SmoothSlider = new GuardedSlider(Qt::Horizontal);
        m_nr2SmoothSlider->setRange(50, 98);  // 0.50–0.98
        m_nr2SmoothSlider->setValue(85);       // default 0.85
        m_nr2SmoothSlider->setStyleSheet(kSliderStyle);
        m_nr2SmoothSlider->setToolTip("How smoothly the noise estimate tracks changes. Higher values give steadier but slower adaptation.");
        sliderGrid->addWidget(m_nr2SmoothSlider, row, 1);
        m_nr2SmoothLabel = new QLabel("0.85");
        m_nr2SmoothLabel->setStyleSheet(valStyle);
        m_nr2SmoothLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sliderGrid->addWidget(m_nr2SmoothLabel, row, 2);
        connect(m_nr2SmoothSlider, &QSlider::valueChanged, this, [this](int v) {
            float val = v / 100.0f;
            m_nr2SmoothLabel->setText(QString::number(val, 'f', 2));
            auto& s = AppSettings::instance();
            s.setValue("NR2GainSmooth", QString::number(val, 'f', 2));
            s.save();
            emit nr2GainSmoothChanged(val);
        });
        ++row;
    }

    // Q_SPP (voice threshold)
    {
        auto* lbl = new QLabel("Voice Threshold:");
        lbl->setStyleSheet(labelStyle);
        sliderGrid->addWidget(lbl, row, 0);
        m_nr2QsppSlider = new GuardedSlider(Qt::Horizontal);
        m_nr2QsppSlider->setRange(5, 50);  // 0.05–0.50
        m_nr2QsppSlider->setValue(20);      // default 0.20
        m_nr2QsppSlider->setStyleSheet(kSliderStyle);
        m_nr2QsppSlider->setToolTip("Speech detection threshold. Lower values preserve quiet speech but may pass more noise.");
        sliderGrid->addWidget(m_nr2QsppSlider, row, 1);
        m_nr2QsppLabel = new QLabel("0.20");
        m_nr2QsppLabel->setStyleSheet(valStyle);
        m_nr2QsppLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sliderGrid->addWidget(m_nr2QsppLabel, row, 2);
        connect(m_nr2QsppSlider, &QSlider::valueChanged, this, [this](int v) {
            float val = v / 100.0f;
            m_nr2QsppLabel->setText(QString::number(val, 'f', 2));
            auto& s = AppSettings::instance();
            s.setValue("NR2Qspp", QString::number(val, 'f', 2));
            s.save();
            emit nr2QsppChanged(val);
        });
        ++row;
    }

    vbox->addLayout(sliderGrid);

    // Reset button
    auto* resetBtn = new QPushButton("Reset Defaults");
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_nr2GainGroup->button(2)->setChecked(true);    // Gamma
        m_nr2NpeGroup->button(0)->setChecked(true);     // OSMS
        m_nr2AeCheck->setChecked(true);
        m_nr2GainMaxSlider->setValue(150);               // 1.50
        m_nr2SmoothSlider->setValue(85);                 // 0.85
        m_nr2QsppSlider->setValue(20);                   // 0.20
    });
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(resetBtn);
    vbox->addLayout(btnRow);

    vbox->addStretch();
    tabs->addTab(page, "NR2");
}

// ── NR4 Tab (libspecbleach) ──────────────────────────────────────────────────

void AetherDspDialog::buildNr4Tab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);

    auto labelStyle = QStringLiteral("QLabel { color: #8090a0; font-size: 11px; }");
    auto valStyle   = QStringLiteral("QLabel { color: #c8d8e8; font-size: 11px; min-width: 40px; }");

    // Noise Estimation Method
    auto* methodGroup = new QGroupBox("Noise Estimation Method");
    auto* methodLayout = new QHBoxLayout(methodGroup);
    m_nr4MethodGroup = new QButtonGroup(this);
    const char* methodLabels[] = {"SPP-MMSE", "Brandt", "Martin"};
    for (int i = 0; i < 3; ++i) {
        auto* rb = new QRadioButton(methodLabels[i]);
        m_nr4MethodGroup->addButton(rb, i);
        methodLayout->addWidget(rb);
    }
    m_nr4MethodGroup->button(0)->setToolTip("MMSE with Speech Presence Probability \u2014 balances noise estimation with speech preservation.");
    m_nr4MethodGroup->button(1)->setToolTip("Recursive smoothing using critical frequency bands \u2014 good for non-stationary noise.");
    m_nr4MethodGroup->button(2)->setToolTip("Minimum statistics using running spectral minima \u2014 robust for slowly varying noise floors.");
    m_nr4MethodGroup->button(0)->setChecked(true);
    connect(m_nr4MethodGroup, &QButtonGroup::idClicked, this, [this](int id) {
        auto& s = AppSettings::instance();
        s.setValue("NR4NoiseEstimationMethod", QString::number(id));
        s.save();
        emit nr4NoiseMethodChanged(id);
    });
    vbox->addWidget(methodGroup);

    // Adaptive Noise
    m_nr4AdaptiveCheck = new QCheckBox("Adaptive Noise Estimation");
    m_nr4AdaptiveCheck->setToolTip("Continuously re-estimates the noise floor as conditions change. Disable for stable environments.");
    m_nr4AdaptiveCheck->setChecked(true);
    connect(m_nr4AdaptiveCheck, &QCheckBox::toggled, this, [this](bool on) {
        auto& s = AppSettings::instance();
        s.setValue("NR4AdaptiveNoise", on ? "True" : "False");
        s.save();
        emit nr4AdaptiveNoiseChanged(on);
    });
    vbox->addWidget(m_nr4AdaptiveCheck);

    // Sliders
    auto* sliderGrid = new QGridLayout;
    int row = 0;

    // Reduction Amount (0–40 dB)
    {
        auto* lbl = new QLabel("Reduction (dB):");
        lbl->setStyleSheet(labelStyle);
        sliderGrid->addWidget(lbl, row, 0);
        m_nr4ReductionSlider = new GuardedSlider(Qt::Horizontal);
        m_nr4ReductionSlider->setRange(0, 400);  // 0.0–40.0
        m_nr4ReductionSlider->setValue(100);      // default 10.0
        m_nr4ReductionSlider->setStyleSheet(kSliderStyle);
        m_nr4ReductionSlider->setToolTip("Maximum noise reduction in dB. Higher values remove more noise but may affect speech.");
        sliderGrid->addWidget(m_nr4ReductionSlider, row, 1);
        m_nr4ReductionLabel = new QLabel("10.0");
        m_nr4ReductionLabel->setStyleSheet(valStyle);
        m_nr4ReductionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sliderGrid->addWidget(m_nr4ReductionLabel, row, 2);
        connect(m_nr4ReductionSlider, &QSlider::valueChanged, this, [this](int v) {
            float val = v / 10.0f;
            m_nr4ReductionLabel->setText(QString::number(val, 'f', 1));
            auto& s = AppSettings::instance();
            s.setValue("NR4ReductionAmount", QString::number(val, 'f', 1));
            s.save();
            emit nr4ReductionChanged(val);
        });
        ++row;
    }

    // Smoothing Factor (0–100%)
    {
        auto* lbl = new QLabel("Smoothing (%):");
        lbl->setStyleSheet(labelStyle);
        sliderGrid->addWidget(lbl, row, 0);
        m_nr4SmoothingSlider = new GuardedSlider(Qt::Horizontal);
        m_nr4SmoothingSlider->setRange(0, 100);
        m_nr4SmoothingSlider->setValue(0);  // default 0%
        m_nr4SmoothingSlider->setStyleSheet(kSliderStyle);
        m_nr4SmoothingSlider->setToolTip("Time-domain smoothing of the noise estimate. Higher values produce steadier but slower reduction.");
        sliderGrid->addWidget(m_nr4SmoothingSlider, row, 1);
        m_nr4SmoothingLabel = new QLabel("0");
        m_nr4SmoothingLabel->setStyleSheet(valStyle);
        m_nr4SmoothingLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sliderGrid->addWidget(m_nr4SmoothingLabel, row, 2);
        connect(m_nr4SmoothingSlider, &QSlider::valueChanged, this, [this](int v) {
            m_nr4SmoothingLabel->setText(QString::number(v));
            auto& s = AppSettings::instance();
            s.setValue("NR4SmoothingFactor", QString::number(static_cast<float>(v), 'f', 1));
            s.save();
            emit nr4SmoothingChanged(static_cast<float>(v));
        });
        ++row;
    }

    // Whitening Factor (0–100%)
    {
        auto* lbl = new QLabel("Whitening (%):");
        lbl->setStyleSheet(labelStyle);
        sliderGrid->addWidget(lbl, row, 0);
        m_nr4WhiteningSlider = new GuardedSlider(Qt::Horizontal);
        m_nr4WhiteningSlider->setRange(0, 100);
        m_nr4WhiteningSlider->setValue(0);  // default 0%
        m_nr4WhiteningSlider->setStyleSheet(kSliderStyle);
        m_nr4WhiteningSlider->setToolTip("Flattens the spectral shape of residual noise so it sounds more uniform.");
        sliderGrid->addWidget(m_nr4WhiteningSlider, row, 1);
        m_nr4WhiteningLabel = new QLabel("0");
        m_nr4WhiteningLabel->setStyleSheet(valStyle);
        m_nr4WhiteningLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sliderGrid->addWidget(m_nr4WhiteningLabel, row, 2);
        connect(m_nr4WhiteningSlider, &QSlider::valueChanged, this, [this](int v) {
            m_nr4WhiteningLabel->setText(QString::number(v));
            auto& s = AppSettings::instance();
            s.setValue("NR4WhiteningFactor", QString::number(static_cast<float>(v), 'f', 1));
            s.save();
            emit nr4WhiteningChanged(static_cast<float>(v));
        });
        ++row;
    }

    // Masking Depth (0.0–1.0)
    {
        auto* lbl = new QLabel("Masking Depth:");
        lbl->setStyleSheet(labelStyle);
        sliderGrid->addWidget(lbl, row, 0);
        m_nr4MaskingSlider = new GuardedSlider(Qt::Horizontal);
        m_nr4MaskingSlider->setRange(0, 100);  // 0.00–1.00
        m_nr4MaskingSlider->setValue(50);       // default 0.50
        m_nr4MaskingSlider->setStyleSheet(kSliderStyle);
        m_nr4MaskingSlider->setToolTip("Depth of spectral masking. Higher values suppress more noise in masked frequency regions.");
        sliderGrid->addWidget(m_nr4MaskingSlider, row, 1);
        m_nr4MaskingLabel = new QLabel("0.50");
        m_nr4MaskingLabel->setStyleSheet(valStyle);
        m_nr4MaskingLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sliderGrid->addWidget(m_nr4MaskingLabel, row, 2);
        connect(m_nr4MaskingSlider, &QSlider::valueChanged, this, [this](int v) {
            float val = v / 100.0f;
            m_nr4MaskingLabel->setText(QString::number(val, 'f', 2));
            auto& s = AppSettings::instance();
            s.setValue("NR4MaskingDepth", QString::number(val, 'f', 2));
            s.save();
            emit nr4MaskingDepthChanged(val);
        });
        ++row;
    }

    // Suppression Strength (0.0–1.0)
    {
        auto* lbl = new QLabel("Suppression:");
        lbl->setStyleSheet(labelStyle);
        sliderGrid->addWidget(lbl, row, 0);
        m_nr4SuppressionSlider = new GuardedSlider(Qt::Horizontal);
        m_nr4SuppressionSlider->setRange(0, 100);  // 0.00–1.00
        m_nr4SuppressionSlider->setValue(50);       // default 0.50
        m_nr4SuppressionSlider->setStyleSheet(kSliderStyle);
        m_nr4SuppressionSlider->setToolTip("Overall suppression strength. Higher values apply more aggressive noise removal.");
        sliderGrid->addWidget(m_nr4SuppressionSlider, row, 1);
        m_nr4SuppressionLabel = new QLabel("0.50");
        m_nr4SuppressionLabel->setStyleSheet(valStyle);
        m_nr4SuppressionLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sliderGrid->addWidget(m_nr4SuppressionLabel, row, 2);
        connect(m_nr4SuppressionSlider, &QSlider::valueChanged, this, [this](int v) {
            float val = v / 100.0f;
            m_nr4SuppressionLabel->setText(QString::number(val, 'f', 2));
            auto& s = AppSettings::instance();
            s.setValue("NR4SuppressionStrength", QString::number(val, 'f', 2));
            s.save();
            emit nr4SuppressionChanged(val);
        });
        ++row;
    }

    vbox->addLayout(sliderGrid);

    // Reset button
    auto* resetBtn = new QPushButton("Reset Defaults");
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        m_nr4MethodGroup->button(0)->setChecked(true);   // SPP-MMSE
        m_nr4AdaptiveCheck->setChecked(true);
        m_nr4ReductionSlider->setValue(100);              // 10.0 dB
        m_nr4SmoothingSlider->setValue(0);                // 0%
        m_nr4WhiteningSlider->setValue(0);                // 0%
        m_nr4MaskingSlider->setValue(50);                 // 0.50
        m_nr4SuppressionSlider->setValue(50);             // 0.50
    });
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(resetBtn);
    vbox->addLayout(btnRow);

    vbox->addStretch();
    tabs->addTab(page, "NR4");
}

// ── RN2 Tab ─────────────────────────────────────────────────────────────────

void AetherDspDialog::buildRn2Tab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    auto* lbl = new QLabel("RN2 (RNNoise) — no adjustable parameters");
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("QLabel { color: #506070; font-size: 14px; }");
    vbox->addWidget(lbl);
    vbox->addStretch();
    tabs->addTab(page, "RN2");
}

// ── BNR Tab ─────────────────────────────────────────────────────────────────

void AetherDspDialog::buildBnrTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    auto* lbl = new QLabel("BNR (NVIDIA) — intensity controlled from overlay menu");
    lbl->setAlignment(Qt::AlignCenter);
    lbl->setStyleSheet("QLabel { color: #506070; font-size: 14px; }");
    vbox->addWidget(lbl);
    vbox->addStretch();
    tabs->addTab(page, "BNR");
}

// ── DFNR Tab ────────────────────────────────────────────────────────────────

void AetherDspDialog::buildDfnrTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);

    auto* group = new QGroupBox("DeepFilterNet3 (DFNR)");
    auto* grid = new QGridLayout(group);
    grid->setColumnStretch(1, 1);

    auto& s = AppSettings::instance();

    // Info label
    auto* info = new QLabel("AI-powered speech enhancement \u2014 higher fidelity than RNNoise\n"
                            "in high-noise HF environments. CPU-only, 10 ms latency, 48 kHz.");
    info->setWordWrap(true);
    info->setStyleSheet("QLabel { color: #506070; font-size: 11px; }");
    grid->addWidget(info, 0, 0, 1, 3);

    // Attenuation Limit slider
    grid->addWidget(new QLabel("Attenuation Limit"), 1, 0);
    m_dfnrAttenSlider = new QSlider(Qt::Horizontal);
    m_dfnrAttenSlider->setRange(0, 100);
    m_dfnrAttenSlider->setValue(static_cast<int>(s.value("DfnrAttenLimit", "100").toFloat()));
    m_dfnrAttenSlider->setStyleSheet(kSliderStyle);
    m_dfnrAttenSlider->setToolTip("Maximum noise attenuation in dB.\n"
                                   "0 dB = passthrough (no denoising)\n"
                                   "100 dB = maximum noise removal\n\n"
                                   "For weak signals: 20\u201330 dB\n"
                                   "For casual listening: 40\u201360 dB\n"
                                   "For strong signals: 80\u2013100 dB");
    grid->addWidget(m_dfnrAttenSlider, 1, 1);
    m_dfnrAttenLabel = new QLabel(QString::number(m_dfnrAttenSlider->value()));
    m_dfnrAttenLabel->setFixedWidth(40);
    grid->addWidget(m_dfnrAttenLabel, 1, 2);

    connect(m_dfnrAttenSlider, &QSlider::valueChanged, this, [this](int v) {
        m_dfnrAttenLabel->setText(QString::number(v));
        float db = static_cast<float>(v);
        auto& s = AppSettings::instance();
        s.setValue("DfnrAttenLimit", QString::number(db, 'f', 0));
        s.save();
        emit dfnrAttenLimitChanged(db);
    });

    // Post-Filter Beta slider
    grid->addWidget(new QLabel("Post-Filter Beta"), 2, 0);
    m_dfnrBetaSlider = new QSlider(Qt::Horizontal);
    m_dfnrBetaSlider->setRange(0, 30);  // 0.0 to 0.30 in 0.01 steps
    m_dfnrBetaSlider->setValue(static_cast<int>(s.value("DfnrPostFilterBeta", "0.0").toFloat() * 100));
    m_dfnrBetaSlider->setStyleSheet(kSliderStyle);
    m_dfnrBetaSlider->setToolTip("Post-filter strength for additional noise suppression.\n"
                                  "0 = disabled (default)\n"
                                  "0.05\u20130.15 = subtle additional filtering\n"
                                  "0.15\u20130.30 = aggressive post-processing");
    grid->addWidget(m_dfnrBetaSlider, 2, 1);
    m_dfnrBetaLabel = new QLabel(QString::number(m_dfnrBetaSlider->value() / 100.0f, 'f', 2));
    m_dfnrBetaLabel->setFixedWidth(40);
    grid->addWidget(m_dfnrBetaLabel, 2, 2);

    connect(m_dfnrBetaSlider, &QSlider::valueChanged, this, [this](int v) {
        float beta = v / 100.0f;
        m_dfnrBetaLabel->setText(QString::number(beta, 'f', 2));
        auto& s = AppSettings::instance();
        s.setValue("DfnrPostFilterBeta", QString::number(beta, 'f', 2));
        s.save();
        emit dfnrPostFilterBetaChanged(beta);
    });

    vbox->addWidget(group);
    vbox->addStretch();
    tabs->addTab(page, "DFNR");
}

// ── Sync from saved settings ─────────────────────────────────────────────────

void AetherDspDialog::syncFromEngine()
{
    auto& s = AppSettings::instance();

    int gainMethod = s.value("NR2GainMethod", "2").toInt();
    if (auto* btn = m_nr2GainGroup->button(gainMethod))
        btn->setChecked(true);

    int npeMethod = s.value("NR2NpeMethod", "0").toInt();
    if (auto* btn = m_nr2NpeGroup->button(npeMethod))
        btn->setChecked(true);

    bool aeFilter = s.value("NR2AeFilter", "True").toString() == "True";
    m_nr2AeCheck->setChecked(aeFilter);

    int gainMax = static_cast<int>(s.value("NR2GainMax", "1.50").toFloat() * 100);
    m_nr2GainMaxSlider->setValue(gainMax);
    m_nr2GainMaxLabel->setText(QString::number(gainMax / 100.0f, 'f', 2));

    int smooth = static_cast<int>(s.value("NR2GainSmooth", "0.85").toFloat() * 100);
    m_nr2SmoothSlider->setValue(smooth);
    m_nr2SmoothLabel->setText(QString::number(smooth / 100.0f, 'f', 2));

    int qspp = static_cast<int>(s.value("NR2Qspp", "0.20").toFloat() * 100);
    m_nr2QsppSlider->setValue(qspp);
    m_nr2QsppLabel->setText(QString::number(qspp / 100.0f, 'f', 2));

    // NR4 sync
    int noiseMethod = s.value("NR4NoiseEstimationMethod", "0").toInt();
    if (auto* btn = m_nr4MethodGroup->button(noiseMethod))
        btn->setChecked(true);

    bool adaptive = s.value("NR4AdaptiveNoise", "True").toString() == "True";
    m_nr4AdaptiveCheck->setChecked(adaptive);

    int reduction = static_cast<int>(s.value("NR4ReductionAmount", "10.0").toFloat() * 10);
    m_nr4ReductionSlider->setValue(reduction);
    m_nr4ReductionLabel->setText(QString::number(reduction / 10.0f, 'f', 1));

    int smoothing = static_cast<int>(s.value("NR4SmoothingFactor", "0.0").toFloat());
    m_nr4SmoothingSlider->setValue(smoothing);
    m_nr4SmoothingLabel->setText(QString::number(smoothing));

    int whitening = static_cast<int>(s.value("NR4WhiteningFactor", "0.0").toFloat());
    m_nr4WhiteningSlider->setValue(whitening);
    m_nr4WhiteningLabel->setText(QString::number(whitening));

    int masking = static_cast<int>(s.value("NR4MaskingDepth", "0.50").toFloat() * 100);
    m_nr4MaskingSlider->setValue(masking);
    m_nr4MaskingLabel->setText(QString::number(masking / 100.0f, 'f', 2));

    int suppression = static_cast<int>(s.value("NR4SuppressionStrength", "0.50").toFloat() * 100);
    m_nr4SuppressionSlider->setValue(suppression);
    m_nr4SuppressionLabel->setText(QString::number(suppression / 100.0f, 'f', 2));

    // DFNR sync
    if (m_dfnrAttenSlider) {
        int atten = static_cast<int>(s.value("DfnrAttenLimit", "100").toFloat());
        m_dfnrAttenSlider->setValue(atten);
        m_dfnrAttenLabel->setText(QString::number(atten));
    }
    if (m_dfnrBetaSlider) {
        int beta = static_cast<int>(s.value("DfnrPostFilterBeta", "0.0").toFloat() * 100);
        m_dfnrBetaSlider->setValue(beta);
        m_dfnrBetaLabel->setText(QString::number(beta / 100.0f, 'f', 2));
    }
}

} // namespace AetherSDR
