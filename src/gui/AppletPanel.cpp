#include "AppletPanel.h"
#include "RxApplet.h"
#include "SMeterWidget.h"
#include "TunerApplet.h"
#include "TxApplet.h"
#include "PhoneCwApplet.h"
#include "PhoneApplet.h"
#include "EqApplet.h"

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
        "border-radius: 3px; padding: 2px 5px; font-size: 11px; color: #c8d8e8; }"
        "QPushButton:checked { background: #0070c0; color: #ffffff; "
        "border: 1px solid #0090e0; }");
    auto* btnLayout = new QHBoxLayout(btnRow);
    btnLayout->setContentsMargins(3, 3, 3, 3);
    btnLayout->setSpacing(2);
    root->addWidget(btnRow);

    // ── S-Meter section (with title bar, toggled by ANLG button) ─────────────
    m_sMeterSection = new QWidget;
    auto* sMeterLayout = new QVBoxLayout(m_sMeterSection);
    sMeterLayout->setContentsMargins(0, 0, 0, 0);
    sMeterLayout->setSpacing(0);
    sMeterLayout->addWidget(appletTitleBar("S-Meter"));
    m_sMeter = new SMeterWidget(m_sMeterSection);
    sMeterLayout->addWidget(m_sMeter);
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

    // ── Helper: add one applet with its toggle button ────────────────────────
    auto addApplet = [&](const QString& label, QWidget* applet) {
        auto* btn = new QPushButton(label, btnRow);
        btn->setCheckable(true);
        btnLayout->addWidget(btn);

        // Insert before the trailing stretch (index = count - 1).
        m_stack->insertWidget(m_stack->count() - 1, applet);

        connect(btn, &QPushButton::toggled, applet, &QWidget::setVisible);
    };

    // ANLG button — toggles the S-Meter section (visible by default)
    {
        auto* anlgBtn = new QPushButton("ANLG", btnRow);
        anlgBtn->setCheckable(true);
        anlgBtn->setChecked(true);
        btnLayout->addWidget(anlgBtn);
        connect(anlgBtn, &QPushButton::toggled, m_sMeterSection, &QWidget::setVisible);
    }

    // RX applet — visible by default
    m_rxApplet = new RxApplet;
    addApplet("RX", m_rxApplet);
    static_cast<QPushButton*>(btnLayout->itemAt(1)->widget())->setChecked(true);
    m_rxApplet->show();

    // Tuner applet — hidden until TGXL detected via amplifier subscription
    m_tunerApplet = new TunerApplet;
    {
        m_tuneBtn = new QPushButton("TUNE", btnRow);
        m_tuneBtn->setCheckable(true);
        m_tuneBtn->hide();  // hidden until setTunerVisible(true)
        btnLayout->addWidget(m_tuneBtn);
        m_stack->insertWidget(m_stack->count() - 1, m_tunerApplet);
        connect(m_tuneBtn, &QPushButton::toggled, m_tunerApplet, &QWidget::setVisible);
    }

    // TX applet — visible by default
    m_txApplet = new TxApplet;
    addApplet("TX",   m_txApplet);
    // The TX button was just added by addApplet; check it to show the applet.
    static_cast<QPushButton*>(btnLayout->itemAt(btnLayout->count() - 1)->widget())->setChecked(true);
    m_txApplet->show();

    m_phoneApplet = new PhoneApplet;
    addApplet("PHNE", m_phoneApplet);

    // P/CW applet — visible by default
    m_phoneCwApplet = new PhoneCwApplet;
    addApplet("P/CW", m_phoneCwApplet);
    static_cast<QPushButton*>(btnLayout->itemAt(btnLayout->count() - 1)->widget())->setChecked(true);
    m_phoneCwApplet->show();

    m_eqApplet = new EqApplet;
    addApplet("EQ", m_eqApplet);

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

void AppletPanel::setSlice(SliceModel* slice)
{
    m_rxApplet->setSlice(slice);
}

void AppletPanel::setAntennaList(const QStringList& ants)
{
    m_rxApplet->setAntennaList(ants);
}

} // namespace AetherSDR
