#include "SpotSettingsDialog.h"
#include "models/RadioModel.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSettings>

namespace AetherSDR {

SpotSettingsDialog::SpotSettingsDialog(RadioModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle("Spot Settings");
    setFixedSize(340, 360);

    // Load persisted settings
    QSettings s("AetherSDR", "AetherSDR");
    m_spotsEnabled    = s.value("spots/enabled", true).toBool();
    m_overrideColors  = s.value("spots/overrideColors", false).toBool();
    m_overrideBg      = s.value("spots/overrideBg", true).toBool();
    m_overrideBgAutoMode = s.value("spots/overrideBgAuto", true).toBool();
    int levels   = s.value("spots/levels", 3).toInt();
    int position = s.value("spots/position", 50).toInt();
    int fontSize = s.value("spots/fontSize", 16).toInt();

    auto* root = new QVBoxLayout(this);

    // ── Title ─────────────────────────────────────────────────────────────
    auto* title = new QLabel("Spot Settings");
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel { font-size: 16px; font-weight: bold; color: #c8d8e8; }");
    root->addWidget(title);
    root->addSpacing(8);

    // ── Grid layout ───────────────────────────────────────────────────────
    auto* grid = new QGridLayout;
    grid->setColumnStretch(1, 1);
    int row = 0;

    // Spots: Enabled/Disabled
    grid->addWidget(new QLabel("Spots:"), row, 0);
    m_spotsToggle = new QPushButton(m_spotsEnabled ? "Enabled" : "Disabled");
    m_spotsToggle->setCheckable(true);
    m_spotsToggle->setChecked(m_spotsEnabled);
    m_spotsToggle->setFixedWidth(80);
    m_spotsToggle->setStyleSheet(
        "QPushButton { background: #206030; color: white; border: 1px solid #305040; padding: 3px; }"
        "QPushButton:!checked { background: #603020; }");
    connect(m_spotsToggle, &QPushButton::toggled, this, [this](bool on) {
        m_spotsEnabled = on;
        m_spotsToggle->setText(on ? "Enabled" : "Disabled");
        QSettings s("AetherSDR", "AetherSDR");
        s.setValue("spots/enabled", on);
    });
    grid->addWidget(m_spotsToggle, row++, 1, Qt::AlignLeft);

    // Levels slider
    grid->addWidget(new QLabel("Levels:"), row, 0);
    auto* levelsRow = new QHBoxLayout;
    m_levelsSlider = new QSlider(Qt::Horizontal);
    m_levelsSlider->setRange(1, 10);
    m_levelsSlider->setValue(levels);
    m_levelsValue = new QLabel(QString::number(levels));
    m_levelsValue->setFixedWidth(24);
    m_levelsValue->setAlignment(Qt::AlignRight);
    levelsRow->addWidget(m_levelsSlider);
    levelsRow->addWidget(m_levelsValue);
    connect(m_levelsSlider, &QSlider::valueChanged, this, [this](int v) {
        m_levelsValue->setText(QString::number(v));
        QSettings s("AetherSDR", "AetherSDR");
        s.setValue("spots/levels", v);
    });
    grid->addLayout(levelsRow, row++, 1);

    // Position slider
    grid->addWidget(new QLabel("Position:"), row, 0);
    auto* posRow = new QHBoxLayout;
    m_positionSlider = new QSlider(Qt::Horizontal);
    m_positionSlider->setRange(0, 100);
    m_positionSlider->setValue(position);
    m_positionValue = new QLabel(QString::number(position));
    m_positionValue->setFixedWidth(24);
    m_positionValue->setAlignment(Qt::AlignRight);
    posRow->addWidget(m_positionSlider);
    posRow->addWidget(m_positionValue);
    connect(m_positionSlider, &QSlider::valueChanged, this, [this](int v) {
        m_positionValue->setText(QString::number(v));
        QSettings s("AetherSDR", "AetherSDR");
        s.setValue("spots/position", v);
    });
    grid->addLayout(posRow, row++, 1);

    // Font Size slider
    grid->addWidget(new QLabel("Font Size:"), row, 0);
    auto* fontRow = new QHBoxLayout;
    m_fontSizeSlider = new QSlider(Qt::Horizontal);
    m_fontSizeSlider->setRange(8, 32);
    m_fontSizeSlider->setValue(fontSize);
    m_fontSizeValue = new QLabel(QString::number(fontSize));
    m_fontSizeValue->setFixedWidth(24);
    m_fontSizeValue->setAlignment(Qt::AlignRight);
    fontRow->addWidget(m_fontSizeSlider);
    fontRow->addWidget(m_fontSizeValue);
    connect(m_fontSizeSlider, &QSlider::valueChanged, this, [this](int v) {
        m_fontSizeValue->setText(QString::number(v));
        QSettings s("AetherSDR", "AetherSDR");
        s.setValue("spots/fontSize", v);
    });
    grid->addLayout(fontRow, row++, 1);

    // Override Colors
    grid->addWidget(new QLabel("Override Colors:"), row, 0);
    m_overrideColorsToggle = new QPushButton(m_overrideColors ? "Enabled" : "Disabled");
    m_overrideColorsToggle->setCheckable(true);
    m_overrideColorsToggle->setChecked(m_overrideColors);
    m_overrideColorsToggle->setFixedWidth(80);
    m_overrideColorsToggle->setStyleSheet(
        "QPushButton { background: #206030; color: white; border: 1px solid #305040; padding: 3px; }"
        "QPushButton:!checked { background: #603020; }");
    connect(m_overrideColorsToggle, &QPushButton::toggled, this, [this](bool on) {
        m_overrideColors = on;
        m_overrideColorsToggle->setText(on ? "Enabled" : "Disabled");
        QSettings s("AetherSDR", "AetherSDR");
        s.setValue("spots/overrideColors", on);
    });
    grid->addWidget(m_overrideColorsToggle, row++, 1, Qt::AlignLeft);

    // Override Background: Enabled / Auto
    grid->addWidget(new QLabel("Override Background:"), row, 0);
    auto* bgRow = new QHBoxLayout;
    m_overrideBgEnabled = new QPushButton("Enabled");
    m_overrideBgEnabled->setCheckable(true);
    m_overrideBgEnabled->setChecked(m_overrideBg);
    m_overrideBgEnabled->setFixedWidth(70);
    m_overrideBgAuto = new QPushButton("Auto");
    m_overrideBgAuto->setCheckable(true);
    m_overrideBgAuto->setChecked(m_overrideBgAutoMode);
    m_overrideBgAuto->setFixedWidth(50);
    QString bgStyle =
        "QPushButton { background: #206030; color: white; border: 1px solid #305040; padding: 3px; }"
        "QPushButton:!checked { background: #603020; }";
    m_overrideBgEnabled->setStyleSheet(bgStyle);
    m_overrideBgAuto->setStyleSheet(bgStyle);
    connect(m_overrideBgEnabled, &QPushButton::toggled, this, [this](bool on) {
        m_overrideBg = on;
        QSettings s("AetherSDR", "AetherSDR");
        s.setValue("spots/overrideBg", on);
    });
    connect(m_overrideBgAuto, &QPushButton::toggled, this, [this](bool on) {
        m_overrideBgAutoMode = on;
        QSettings s("AetherSDR", "AetherSDR");
        s.setValue("spots/overrideBgAuto", on);
    });
    bgRow->addWidget(m_overrideBgEnabled);
    bgRow->addWidget(m_overrideBgAuto);
    bgRow->addStretch();
    grid->addLayout(bgRow, row++, 1);

    // Total Spots
    grid->addWidget(new QLabel("Total Spots:"), row, 0);
    m_totalSpotsLabel = new QLabel("0");
    m_totalSpotsLabel->setStyleSheet("QLabel { color: #c8d8e8; font-weight: bold; }");
    grid->addWidget(m_totalSpotsLabel, row++, 1);

    root->addLayout(grid);
    root->addStretch();

    // ── Clear All Spots button ────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    auto* clearBtn = new QPushButton("Clear All Spots");
    clearBtn->setFixedWidth(120);
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        m_model->sendCommand("spot clear");
        m_totalSpotsLabel->setText("0");
    });
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);
}

} // namespace AetherSDR
