#ifdef HAVE_HIDAPI
#include "StreamDeckDialog.h"
#include "core/StreamDeckManager.h"
#include "core/StreamDeckDevice.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QFrame>

namespace AetherSDR {

static const QString kLabelStyle =
    "QLabel { color: #8090a0; font-size: 11px; }";
static const QString kValueStyle =
    "QLabel { color: #c8d8e8; font-size: 11px; font-weight: bold; }";

StreamDeckDialog::StreamDeckDialog(StreamDeckManager* manager, QWidget* parent)
    : QDialog(parent)
    , m_manager(manager)
{
    setWindowTitle("StreamDeck Configuration");
    setMinimumSize(500, 400);
    setStyleSheet("QDialog { background: #0f0f1a; }");
    setAttribute(Qt::WA_DeleteOnClose);

    buildUI();

    connect(manager, &StreamDeckManager::deviceConnected,
            this, &StreamDeckDialog::onDeviceConnected);
    connect(manager, &StreamDeckManager::deviceDisconnected,
            this, &StreamDeckDialog::onDeviceDisconnected);

    refreshDeviceList();
}

void StreamDeckDialog::buildUI()
{
    auto* vbox = new QVBoxLayout(this);
    vbox->setSpacing(8);
    vbox->setContentsMargins(12, 12, 12, 12);

    // ── Device selector row ─────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);

        auto* lbl = new QLabel("Device:");
        lbl->setStyleSheet(kLabelStyle);
        row->addWidget(lbl);

        m_deviceCombo = new QComboBox;
        m_deviceCombo->setStyleSheet(
            "QComboBox { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; padding: 3px 6px; color: #c8d8e8; font-size: 11px; }"
            "QComboBox::drop-down { border: none; }"
            "QComboBox QAbstractItemView { background: #1a2a3a; color: #c8d8e8; "
            "selection-background-color: #0070c0; }");
        row->addWidget(m_deviceCombo, 1);

        m_refreshBtn = new QPushButton("Refresh");
        m_refreshBtn->setFixedWidth(70);
        m_refreshBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; padding: 3px 8px; }"
            "QPushButton:hover { background: #203040; }");
        connect(m_refreshBtn, &QPushButton::clicked, this, &StreamDeckDialog::refreshDeviceList);
        row->addWidget(m_refreshBtn);

        vbox->addLayout(row);
    }

    // ── Info label ──────────────────────────────────────────────────────
    m_infoLabel = new QLabel("No device connected");
    m_infoLabel->setStyleSheet(kValueStyle);
    m_infoLabel->setAlignment(Qt::AlignCenter);
    vbox->addWidget(m_infoLabel);

    // ── Separator ──────────────────────────────────────────────────────
    auto* sep1 = new QFrame;
    sep1->setFrameShape(QFrame::HLine);
    sep1->setStyleSheet("color: #304050;");
    vbox->addWidget(sep1);

    // ── Button grid container ───────────────────────────────────────────
    m_gridContainer = new QWidget;
    m_gridContainer->setStyleSheet("background: #0a0a14; border: 1px solid #203040; border-radius: 4px;");
    m_gridLayout = new QGridLayout(m_gridContainer);
    m_gridLayout->setSpacing(4);
    m_gridLayout->setContentsMargins(8, 8, 8, 8);
    vbox->addWidget(m_gridContainer, 1);

    // ── Dial info ──────────────────────────────────────────────────────
    m_dialLabel = new QLabel("");
    m_dialLabel->setStyleSheet("QLabel { color: #8090a0; font-size: 10px; }");
    m_dialLabel->setAlignment(Qt::AlignCenter);
    m_dialLabel->setWordWrap(true);
    vbox->addWidget(m_dialLabel);

    // ── Separator ──────────────────────────────────────────────────────
    auto* sep2 = new QFrame;
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet("color: #304050;");
    vbox->addWidget(sep2);

    // ── Brightness slider ───────────────────────────────────────────────
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);

        auto* lbl = new QLabel("Brightness:");
        lbl->setStyleSheet(kLabelStyle);
        row->addWidget(lbl);

        m_brightnessSlider = new QSlider(Qt::Horizontal);
        m_brightnessSlider->setRange(0, 100);
        m_brightnessSlider->setValue(70);
        m_brightnessSlider->setStyleSheet(
            "QSlider { border: none; }"
            "QSlider::groove:horizontal { height: 4px; background: #203040; border-radius: 2px; }"
            "QSlider::handle:horizontal { width: 14px; height: 14px; margin: -5px 0;"
            " background: #00b4d8; border-radius: 7px; }");
        connect(m_brightnessSlider, &QSlider::valueChanged,
                this, &StreamDeckDialog::onBrightnessChanged);
        row->addWidget(m_brightnessSlider, 1);

        m_brightnessLabel = new QLabel("70%");
        m_brightnessLabel->setStyleSheet(kValueStyle);
        m_brightnessLabel->setFixedWidth(35);
        m_brightnessLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        row->addWidget(m_brightnessLabel);

        vbox->addLayout(row);
    }

    // ── Close button ────────────────────────────────────────────────────
    auto* closeBtn = new QPushButton("Close");
    closeBtn->setStyleSheet(
        "QPushButton { background: #00b4d8; color: #0f0f1a; font-weight: bold; "
        "border-radius: 4px; padding: 6px 24px; }"
        "QPushButton:hover { background: #00c8f0; }");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    vbox->addWidget(closeBtn, 0, Qt::AlignCenter);
}

void StreamDeckDialog::refreshDeviceList()
{
    m_deviceCombo->clear();
    m_keyButtons.clear();

    auto serials = m_manager->connectedSerials();
    if (serials.isEmpty()) {
        m_deviceCombo->addItem("No device connected");
        m_infoLabel->setText("No device connected");
        m_dialLabel->clear();
        // Clear grid
        while (m_gridLayout->count())
            delete m_gridLayout->takeAt(0)->widget();
        return;
    }

    for (const auto& serial : serials) {
        auto* info = m_manager->deviceInfo(serial);
        if (info)
            m_deviceCombo->addItem(QString("%1 — %2").arg(info->name).arg(serial), serial);
    }

    // Select first device
    if (!serials.isEmpty()) {
        m_selectedSerial = serials.first();
        auto* info = m_manager->deviceInfo(m_selectedSerial);
        if (info) {
            m_infoLabel->setText(QString("%1  •  Serial: %2  •  %3 keys (%4×%5)  •  %6 dials")
                .arg(info->name).arg(m_selectedSerial)
                .arg(info->keyCount).arg(info->keyCols).arg(info->keyRows)
                .arg(info->dialCount));
            buildGrid(info->keyRows, info->keyCols, 56);

            if (info->dialCount > 0) {
                QStringList dialDescs;
                const char* dialNames[] = {"VFO Tune", "AF Gain", "RF Power",
                                            "Squelch", "RIT", "XIT"};
                const char* dialPush[] = {"Step Cycle", "Mute", "TUNE",
                                           "SQL On/Off", "RIT On/Off", "XIT On/Off"};
                for (int i = 0; i < info->dialCount && i < 6; ++i)
                    dialDescs << QString("Dial %1: %2 (push: %3)").arg(i+1).arg(dialNames[i]).arg(dialPush[i]);
                m_dialLabel->setText(dialDescs.join("   •   "));
            } else {
                m_dialLabel->clear();
            }
        }
    }
}

void StreamDeckDialog::buildGrid(int rows, int cols, int keySize)
{
    // Clear existing
    m_keyButtons.clear();
    while (m_gridLayout->count())
        delete m_gridLayout->takeAt(0)->widget();

    m_gridRows = rows;
    m_gridCols = cols;

    // Row labels
    const char* rowLabels[] = {"Bands", "Modes", "Controls", "DSP"};

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            int idx = r * cols + c;
            auto* btn = new QPushButton;
            btn->setFixedSize(keySize, keySize);
            btn->setStyleSheet(
                "QPushButton { background: #1a2a3a; border: 2px solid #304050; "
                "border-radius: 4px; color: #c8d8e8; font-size: 9px; font-weight: bold; }"
                "QPushButton:hover { border-color: #00b4d8; }"
                "QPushButton:pressed { background: #00b4d8; color: #0f0f1a; }");

            // Set default button text based on layout
            QString text;
            if (r == 0) {
                const char* bands[] = {"160m","80m","60m","40m","30m","20m","17m","15m","12m"};
                if (c < 9) text = bands[c];
            } else if (r == 1) {
                const char* modes[] = {"10m","6m","USB","LSB","CW","FT8","AM","FM","SAM"};
                if (c < 9) text = modes[c];
            } else if (r == 2) {
                const char* ctrls[] = {"FREQ","FREQ","SPLIT","LOCK","MUTE","MOX","TUNE","ATU",""};
                if (c < 9) text = ctrls[c];
            } else if (r == 3) {
                const char* dsp[] = {"NB","NR","ANF","NR2","RN2","APF","AGC","DAX","APD"};
                if (c < 9) text = dsp[c];
            }
            btn->setText(text);

            connect(btn, &QPushButton::clicked, this, [idx]() {
                // Future: open key configuration dialog
                qDebug() << "StreamDeck key" << idx << "clicked in config";
            });

            m_gridLayout->addWidget(btn, r, c);
            m_keyButtons.append({btn, idx});
        }

        // Row label
        auto* label = new QLabel(r < 4 ? rowLabels[r] : "");
        label->setStyleSheet("QLabel { color: #506070; font-size: 9px; }");
        label->setAlignment(Qt::AlignVCenter);
        m_gridLayout->addWidget(label, r, cols);
    }

    // Touchscreen strip placeholder
    auto* info = m_manager->deviceInfo(m_selectedSerial);
    if (info && info->touchWidth > 0) {
        auto* tsLabel = new QLabel("Touchscreen Strip — Frequency + Mode Display");
        tsLabel->setStyleSheet(
            "QLabel { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #8090a0; font-size: 10px; padding: 4px; }");
        tsLabel->setAlignment(Qt::AlignCenter);
        tsLabel->setFixedHeight(30);
        m_gridLayout->addWidget(tsLabel, rows, 0, 1, cols + 1);
    }
}

void StreamDeckDialog::onDeviceConnected(const QString&, const QString&,
                                          int, int, int, int)
{
    refreshDeviceList();
}

void StreamDeckDialog::onDeviceDisconnected(const QString&)
{
    refreshDeviceList();
}

void StreamDeckDialog::onBrightnessChanged(int value)
{
    m_brightnessLabel->setText(QString("%1%").arg(value));
    // Debounce: only send after 200ms of no changes
    if (!m_brightnessTimer) {
        m_brightnessTimer = new QTimer(this);
        m_brightnessTimer->setSingleShot(true);
        m_brightnessTimer->setInterval(200);
        connect(m_brightnessTimer, &QTimer::timeout, this, [this] {
            int val = m_brightnessSlider->value();
            if (!m_selectedSerial.isEmpty()) {
                QMetaObject::invokeMethod(m_manager, [this, val] {
                    m_manager->setBrightness(m_selectedSerial, val);
                });
            }
        });
    }
    m_brightnessTimer->start();
}

} // namespace AetherSDR
#endif
