#include "AppletPanel.h"
#include "ComboStyle.h"
#include "RxApplet.h"
#include "SMeterWidget.h"
#include "TunerApplet.h"
#include "TxApplet.h"
#include "PhoneCwApplet.h"
#include "PhoneApplet.h"
#include "EqApplet.h"
#include "CatApplet.h"
#include "AntennaGeniusApplet.h"
#include "models/SliceModel.h"
#include <QComboBox>
#include <QLabel>
#include <QSettings>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>

namespace AetherSDR {

// ── Common gradient title bar (matches SmartSDR style) ──────────────────────

static QWidget* appletTitleBar(const QString& text)
{
    auto* bar = new QWidget;
    bar->setFixedHeight(16);
    bar->setStyleSheet(
        "QWidget { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #3a4a5a, stop:0.5 #2a3a4a, stop:1 #1a2a38); "
        "border-bottom: 1px solid #0a1a28; }");

    auto* lbl = new QLabel(text, bar);
    lbl->setStyleSheet("QLabel { background: transparent; color: #8aa8c0; "
                       "font-size: 10px; font-weight: bold; }");
    lbl->setGeometry(6, 1, 200, 14);
    return bar;
}

// ── AppletPanel ──────────────────────────────────────────────────────────────

AppletPanel::AppletPanel(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(260);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Toggle button row (always at the very top) ───────────────────────────
    auto* btnRow = new QWidget;
    btnRow->setStyleSheet(
        "QWidget { background: #0a0a18; border-bottom: 1px solid #1e2e3e; }"
        "QPushButton { background: #1a2a3a; border: 1px solid #203040; "
        "border-radius: 3px; padding: 2px 3px; font-size: 11px; color: #c8d8e8; }"
        "QPushButton:checked { background: #0070c0; color: #ffffff; "
        "border: 1px solid #0090e0; }");
    auto* btnLayout = new QHBoxLayout(btnRow);
    btnLayout->setContentsMargins(2, 3, 2, 3);
    btnLayout->setSpacing(1);
    root->addWidget(btnRow);

    // ── S-Meter section (with title bar, toggled by ANLG button) ─────────────
    m_sMeterSection = new QWidget;
    auto* sMeterLayout = new QVBoxLayout(m_sMeterSection);
    sMeterLayout->setContentsMargins(0, 0, 0, 0);
    sMeterLayout->setSpacing(0);
    sMeterLayout->addWidget(appletTitleBar("S-Meter"));
    m_sMeter = new SMeterWidget(m_sMeterSection);
    sMeterLayout->addWidget(m_sMeter);

    // ── TX / RX meter select row ──────────────────────────────────────────
    auto* selectRow = new QWidget(m_sMeterSection);
    auto* selectLayout = new QHBoxLayout(selectRow);
    selectLayout->setContentsMargins(4, 2, 4, 2);
    selectLayout->setSpacing(6);

    const QString labelStyle = QStringLiteral(
        "color: #8090a0; font-size: 10px; font-weight: bold;");

    // TX Select
    auto* txLabel = new QLabel("TX Select", selectRow);
    txLabel->setStyleSheet(labelStyle);
    txLabel->setAlignment(Qt::AlignCenter);
    m_txSelect = new QComboBox(selectRow);
    m_txSelect->addItems({"Power", "SWR", "Level", "Compression"});
    AetherSDR::applyComboStyle(m_txSelect);

    auto* txCol = new QVBoxLayout;
    txCol->setSpacing(1);
    txCol->addWidget(txLabel);
    txCol->addWidget(m_txSelect);

    // RX Select
    auto* rxLabel = new QLabel("RX Select", selectRow);
    rxLabel->setStyleSheet(labelStyle);
    rxLabel->setAlignment(Qt::AlignCenter);
    m_rxSelect = new QComboBox(selectRow);
    m_rxSelect->addItems({"S-Meter", "S-Meter Peak"});
    m_rxSelect->setCurrentIndex(0);  // default to S-Meter
    AetherSDR::applyComboStyle(m_rxSelect);

    auto* rxCol = new QVBoxLayout;
    rxCol->setSpacing(1);
    rxCol->addWidget(rxLabel);
    rxCol->addWidget(m_rxSelect);

    selectLayout->addLayout(txCol, 1);
    selectLayout->addLayout(rxCol, 1);
    sMeterLayout->addWidget(selectRow);

    // Wire dropdowns to SMeterWidget mode slots
    connect(m_txSelect, &QComboBox::currentTextChanged,
            m_sMeter, &SMeterWidget::setTxMode);
    connect(m_rxSelect, &QComboBox::currentTextChanged,
            m_sMeter, &SMeterWidget::setRxMode);

    root->addWidget(m_sMeterSection);

    // ── Scrollable applet stack ──────────────────────────────────────────────
    auto* scrollArea = new QScrollArea;
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setWidgetResizable(true);

    auto* container = new QWidget;
    m_stack = new QVBoxLayout(container);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->setSpacing(0);
    m_stack->addStretch();
    scrollArea->setWidget(container);
    root->addWidget(scrollArea, 1);

    QSettings settings;

    // ── Helper: add one applet with its toggle button (persistent state) ────
    auto addApplet = [&](const QString& label, QWidget* applet, bool defaultOn) {
        auto* btn = new QPushButton(label, btnRow);
        btn->setCheckable(true);
        btnLayout->addWidget(btn);

        // Insert before the trailing stretch (index = count - 1).
        m_stack->insertWidget(m_stack->count() - 1, applet);

        // Restore saved state (or use default)
        const QString key = QStringLiteral("applet/%1").arg(label);
        bool on = settings.value(key, defaultOn).toBool();
        btn->setChecked(on);
        applet->setVisible(on);

        connect(btn, &QPushButton::toggled, applet, [applet, key](bool checked) {
            applet->setVisible(checked);
            QSettings().setValue(key, checked);
        });
    };

    // ANLG button — toggles the S-Meter section (default: visible)
    {
        auto* anlgBtn = new QPushButton("VU", btnRow);
        anlgBtn->setCheckable(true);
        bool anlgOn = settings.value("applet/ANLG", true).toBool();
        anlgBtn->setChecked(anlgOn);
        m_sMeterSection->setVisible(anlgOn);
        btnLayout->addWidget(anlgBtn);
        connect(anlgBtn, &QPushButton::toggled, this, [this](bool on) {
            m_sMeterSection->setVisible(on);
            QSettings().setValue("applet/ANLG", on);
        });
    }

    m_rxApplet = new RxApplet;
    addApplet("RX", m_rxApplet, true);

    // Tuner applet — hidden until TGXL detected via amplifier subscription
    m_tunerApplet = new TunerApplet;
    {
        m_tuneBtn = new QPushButton("TUN", btnRow);
        m_tuneBtn->setCheckable(true);
        m_tuneBtn->hide();  // hidden until setTunerVisible(true)
        btnLayout->addWidget(m_tuneBtn);
        m_stack->insertWidget(m_stack->count() - 1, m_tunerApplet);
        connect(m_tuneBtn, &QPushButton::toggled, m_tunerApplet, &QWidget::setVisible);
    }

    m_txApplet = new TxApplet;
    addApplet("TX", m_txApplet, true);

    m_phoneApplet = new PhoneApplet;
    addApplet("PHNE", m_phoneApplet, true);

    m_phoneCwApplet = new PhoneCwApplet;
    addApplet("P/CW", m_phoneCwApplet, true);

    m_eqApplet = new EqApplet;
    addApplet("EQ", m_eqApplet, true);

    m_catApplet = new CatApplet;
    addApplet("DIGI", m_catApplet, false);

    // Antenna Genius applet — hidden until device discovered on UDP 9007
    m_agApplet = new AntennaGeniusApplet;
    {
        m_agBtn = new QPushButton("AG", btnRow);
        m_agBtn->setCheckable(true);
        m_agBtn->hide();  // hidden until setAgVisible(true)
        btnLayout->addWidget(m_agBtn);
        m_stack->insertWidget(m_stack->count() - 1, m_agApplet);
        connect(m_agBtn, &QPushButton::toggled, m_agApplet, &QWidget::setVisible);
    }

    btnLayout->addStretch();
}

void AppletPanel::setTunerVisible(bool visible)
{
    if (visible) {
        m_tuneBtn->show();
        // Auto-check (show the applet) on first detection
        if (!m_tuneBtn->isChecked())
            m_tuneBtn->setChecked(true);
    } else {
        m_tuneBtn->setChecked(false);  // hide the applet
        m_tuneBtn->hide();
    }
}

void AppletPanel::setAgVisible(bool visible)
{
    if (visible) {
        m_agBtn->show();
        if (!m_agBtn->isChecked())
            m_agBtn->setChecked(true);
    } else {
        m_agBtn->setChecked(false);
        m_agBtn->hide();
    }
}

void AppletPanel::setSlice(SliceModel* slice)
{
    m_rxApplet->setSlice(slice);

    // Route mode changes to P/CW applet for Phone↔CW switching
    if (slice) {
        connect(slice, &SliceModel::modeChanged,
                m_phoneCwApplet, &PhoneCwApplet::setMode);
        m_phoneCwApplet->setMode(slice->mode());
    }
}

void AppletPanel::setAntennaList(const QStringList& ants)
{
    m_rxApplet->setAntennaList(ants);
}

} // namespace AetherSDR
