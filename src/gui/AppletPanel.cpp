#include "AppletPanel.h"
#include "FloatingAppletWindow.h"
#include "GuardedSlider.h"
#include "ComboStyle.h"
#include "RxApplet.h"
#include "SMeterWidget.h"
#include "TunerApplet.h"
#include "AmpApplet.h"
#include "TxApplet.h"
#include "PhoneCwApplet.h"
#include "PhoneApplet.h"
#include "EqApplet.h"
#include "CatApplet.h"
#include "AntennaGeniusApplet.h"
#include "MeterApplet.h"
#include "models/SliceModel.h"
#include <QComboBox>
#include <QLabel>
#include "core/AppSettings.h"
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QDrag>
#include <QMimeData>
#include <QMouseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QScrollBar>
#include <QPainter>
#include <QPixmap>
#include <QMenu>
#include <QTimer>
#include <QContextMenuEvent>

namespace AetherSDR {

const QStringList AppletPanel::kDefaultOrder = {
    "RX", "TUN", "AMP", "TX", "PHNE", "P/CW", "EQ", "DIGI", "MTR", "AG"
};

// ── Drag-handle title bar ───────────────────────────────────────────────────

class AppletTitleBar : public QWidget {
public:
    AppletTitleBar(const QString& text, const QString& appletId,
                   AppletPanel* panel = nullptr, QWidget* parent = nullptr)
        : QWidget(parent), m_appletId(appletId), m_panel(panel)
    {
        setFixedHeight(16);
        setCursor(Qt::OpenHandCursor);
        setStyleSheet(
            "QWidget { background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
            "stop:0 #3a4a5a, stop:0.5 #2a3a4a, stop:1 #1a2a38); "
            "border-bottom: 1px solid #0a1a28; }");

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(2, 0, 4, 0);
        layout->setSpacing(4);

        // Drag grip dots
        auto* grip = new QLabel(QString::fromUtf8("\xe2\x8b\xae\xe2\x8b\xae"));  // ⋮⋮
        grip->setStyleSheet("QLabel { background: transparent; color: #607080; font-size: 10px; }");
        layout->addWidget(grip);

        m_label = new QLabel(text);
        m_label->setStyleSheet("QLabel { background: transparent; color: #8aa8c0; "
                           "font-size: 10px; font-weight: bold; }");
        layout->addWidget(m_label);
        layout->addStretch();
    }

    const QString& appletId() const { return m_appletId; }
    void setTitle(const QString& text) { m_label->setText(text); }

protected:
    void mousePressEvent(QMouseEvent* ev) override {
        if (ev->button() == Qt::LeftButton)
            m_dragStartPos = ev->pos();
        QWidget::mousePressEvent(ev);
    }

    void mouseMoveEvent(QMouseEvent* ev) override {
        if (!(ev->buttons() & Qt::LeftButton)) return;
        if ((ev->pos() - m_dragStartPos).manhattanLength() < 10) return;

        setCursor(Qt::ClosedHandCursor);
        auto* drag = new QDrag(this);
        auto* mimeData = new QMimeData;
        mimeData->setData("application/x-aethersdr-applet", m_appletId.toUtf8());
        drag->setMimeData(mimeData);

        // Semi-transparent snapshot of this title bar as drag pixmap
        QPixmap pixmap(size());
        pixmap.fill(Qt::transparent);
        render(&pixmap);
        drag->setPixmap(pixmap);
        drag->setHotSpot(ev->pos());

        drag->exec(Qt::MoveAction);
        setCursor(Qt::OpenHandCursor);
    }

    void contextMenuEvent(QContextMenuEvent* ev) override {
        if (!m_panel) { return; }
        bool isFloating = m_panel->isAppletFloating(m_appletId);
        // Use popup() — no nested event loop
        QMenu* menu = new QMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        QAction* act = menu->addAction(isFloating ? "\u21a9 Dock" : "\u2197 Pop out");
        connect(act, &QAction::triggered, this, [this, isFloating]() {
            if (isFloating)
                m_panel->dockApplet(m_appletId);
            else
                m_panel->floatApplet(m_appletId);
        });
        menu->popup(ev->globalPos());
    }

private:
    QString      m_appletId;
    QPoint       m_dragStartPos;
    QLabel*      m_label{nullptr};
    AppletPanel* m_panel{nullptr};
};

// ── Drop-aware scroll area ──────────────────────────────────────────────────

class AppletDropArea : public QScrollArea {
public:
    AppletDropArea(AppletPanel* panel) : QScrollArea(panel), m_panel(panel) {
        setAcceptDrops(true);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* ev) override {
        if (ev->mimeData()->hasFormat("application/x-aethersdr-applet"))
            ev->acceptProposedAction();
    }

    void dragMoveEvent(QDragMoveEvent* ev) override {
        if (!ev->mimeData()->hasFormat("application/x-aethersdr-applet")) return;
        ev->acceptProposedAction();
        m_panel->m_dropIndicator->setVisible(true);

        // Position the indicator at the computed drop index
        int localY = ev->position().toPoint().y() + verticalScrollBar()->value();
        int idx = m_panel->dropIndexFromY(localY);

        // Find the Y position for the indicator
        int indicatorY = 0;
        if (idx < m_panel->m_appletOrder.size()) {
            auto* w = m_panel->m_appletOrder[idx].titleBar;
            if (w) indicatorY = w->mapTo(widget(), QPoint(0, 0)).y();
        } else if (!m_panel->m_appletOrder.isEmpty()) {
            auto& last = m_panel->m_appletOrder.back();
            auto* w = last.widget;
            if (w) indicatorY = w->mapTo(widget(), QPoint(0, w->height())).y();
        }
        m_panel->m_dropIndicator->setParent(widget());
        m_panel->m_dropIndicator->setGeometry(4, indicatorY - 1, widget()->width() - 8, 2);
        m_panel->m_dropIndicator->raise();
    }

    void dragLeaveEvent(QDragLeaveEvent*) override {
        m_panel->m_dropIndicator->setVisible(false);
    }

    void dropEvent(QDropEvent* ev) override {
        m_panel->m_dropIndicator->setVisible(false);
        if (!ev->mimeData()->hasFormat("application/x-aethersdr-applet")) return;

        QString draggedId = QString::fromUtf8(ev->mimeData()->data("application/x-aethersdr-applet"));
        int localY = ev->position().toPoint().y() + verticalScrollBar()->value();
        int dropIdx = m_panel->dropIndexFromY(localY);

        // Find the dragged applet's current index
        int srcIdx = -1;
        for (int i = 0; i < m_panel->m_appletOrder.size(); ++i) {
            if (m_panel->m_appletOrder[i].id == draggedId) { srcIdx = i; break; }
        }
        if (srcIdx < 0) return;

        // Adjust drop index if moving down (after removing source)
        if (dropIdx > srcIdx) dropIdx--;
        if (dropIdx == srcIdx) return;

        // Move the entry
        auto entry = m_panel->m_appletOrder[srcIdx];
        m_panel->m_appletOrder.remove(srcIdx);
        m_panel->m_appletOrder.insert(dropIdx, entry);
        m_panel->rebuildStackOrder();
        m_panel->saveOrder();

        ev->acceptProposedAction();
    }

private:
    AppletPanel* m_panel;
};

// ── AppletPanel ──────────────────────────────────────────────────────────────

AppletPanel::AppletPanel(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(260);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Toggle button rows (always at the very top) ──────────────────────────
    const char* btnRowStyle =
        "QWidget { background: #0a0a18; }"
        "QPushButton { background: #1a2a3a; border: 1px solid #203040; "
        "border-radius: 3px; padding: 2px 3px; font-size: 11px; color: #c8d8e8; }"
        "QPushButton:checked { background: #0070c0; color: #ffffff; "
        "border: 1px solid #0090e0; }";

    auto* btnRow1 = new QWidget;
    btnRow1->setStyleSheet(btnRowStyle);
    auto* btnLayout1 = new QHBoxLayout(btnRow1);
    btnLayout1->setContentsMargins(2, 3, 2, 0);
    btnLayout1->setSpacing(1);
    root->addWidget(btnRow1);

    auto* btnRow2 = new QWidget;
    btnRow2->setStyleSheet(QString(btnRowStyle) +
        "QWidget { border-bottom: 1px solid #1e2e3e; }");
    auto* btnLayout2 = new QHBoxLayout(btnRow2);
    btnLayout2->setContentsMargins(2, 2, 2, 3);
    btnLayout2->setSpacing(1);
    root->addWidget(btnRow2);

    // ── S-Meter section (with title bar, toggled by ANLG button) ─────────────
    m_sMeterSection = new QWidget;
    auto* sMeterLayout = new QVBoxLayout(m_sMeterSection);
    sMeterLayout->setContentsMargins(0, 0, 0, 0);
    sMeterLayout->setSpacing(0);

    auto* sMeterTitle = new AppletTitleBar("S-Meter", "VU");
    sMeterLayout->addWidget(sMeterTitle);

    m_sMeter = new SMeterWidget(m_sMeterSection);
    m_sMeter->setAccessibleName("S-Meter");
    m_sMeter->setAccessibleDescription("Signal strength meter, shows S-units or TX power");
    sMeterLayout->addWidget(m_sMeter);

    // ── TX / RX meter select row ──────────────────────────────────────────
    auto* selectRow = new QWidget(m_sMeterSection);
    auto* selectLayout = new QHBoxLayout(selectRow);
    selectLayout->setContentsMargins(4, 2, 4, 2);
    selectLayout->setSpacing(6);

    const QString labelStyle = QStringLiteral(
        "color: #8090a0; font-size: 10px; font-weight: bold;");

    auto* txLabel = new QLabel("TX Select", selectRow);
    txLabel->setStyleSheet(labelStyle);
    txLabel->setAlignment(Qt::AlignCenter);
    m_txSelect = new GuardedComboBox(selectRow);
    m_txSelect->addItems({"Power", "SWR", "Level", "Compression"});
    m_txSelect->setAccessibleName("TX meter mode");
    m_txSelect->setAccessibleDescription("Select which TX parameter the S-meter displays");
    AetherSDR::applyComboStyle(m_txSelect);

    auto* txCol = new QVBoxLayout;
    txCol->setSpacing(1);
    txCol->addWidget(txLabel);
    txCol->addWidget(m_txSelect);

    auto* rxLabel = new QLabel("RX Select", selectRow);
    rxLabel->setStyleSheet(labelStyle);
    rxLabel->setAlignment(Qt::AlignCenter);
    m_rxSelect = new GuardedComboBox(selectRow);
    m_rxSelect->addItems({"S-Meter", "S-Meter Peak"});
    m_rxSelect->setCurrentIndex(0);
    m_rxSelect->setAccessibleName("RX meter mode");
    m_rxSelect->setAccessibleDescription("Select which RX parameter the S-meter displays");
    AetherSDR::applyComboStyle(m_rxSelect);

    auto* rxCol = new QVBoxLayout;
    rxCol->setSpacing(1);
    rxCol->addWidget(rxLabel);
    rxCol->addWidget(m_rxSelect);

    selectLayout->addLayout(txCol, 1);
    selectLayout->addLayout(rxCol, 1);
    sMeterLayout->addWidget(selectRow);

    connect(m_txSelect, &QComboBox::currentTextChanged,
            m_sMeter, &SMeterWidget::setTxMode);
    connect(m_rxSelect, &QComboBox::currentTextChanged,
            m_sMeter, &SMeterWidget::setRxMode);

    // Restore saved TX/RX meter selections AFTER connecting signals
    // so setTxMode/setRxMode are called with the restored values (#809)
    int txIdx = AppSettings::instance().value("SMeter_TxSelect", 0).toInt();
    int rxIdx = AppSettings::instance().value("SMeter_RxSelect", 0).toInt();
    if (txIdx >= 0 && txIdx < m_txSelect->count())
        m_txSelect->setCurrentIndex(txIdx);
    if (rxIdx >= 0 && rxIdx < m_rxSelect->count())
        m_rxSelect->setCurrentIndex(rxIdx);

    // Persist TX/RX meter selections on change (#809)
    connect(m_txSelect, &QComboBox::currentIndexChanged,
            this, [](int idx) {
        AppSettings::instance().setValue("SMeter_TxSelect", idx);
    });
    connect(m_rxSelect, &QComboBox::currentIndexChanged,
            this, [](int idx) {
        AppSettings::instance().setValue("SMeter_RxSelect", idx);
    });

    // ── Peak hold line controls (#840) ────────────────────────────────────
    auto* peakRow = new QWidget(m_sMeterSection);
    auto* peakLayout = new QHBoxLayout(peakRow);
    peakLayout->setContentsMargins(4, 2, 4, 2);
    peakLayout->setSpacing(6);

    auto* peakBtn = new QPushButton("Peak Hold", peakRow);
    peakBtn->setCheckable(true);
    peakBtn->setChecked(AppSettings::instance().value("PeakHoldEnabled", "False") == "True");
    peakBtn->setAccessibleName("Peak hold");
    peakBtn->setAccessibleDescription("Toggle peak hold line on S-meter");
    peakBtn->setFixedHeight(20);
    peakBtn->setStyleSheet(
        "QPushButton { background: #1a1a2e; color: #8090a0; border: 1px solid #334; "
        "border-radius: 3px; font-size: 10px; padding: 0 6px; } "
        "QPushButton:checked { background: #0a3060; color: #00b4d8; border-color: #00b4d8; }");

    auto* decayLabel = new QLabel("Decay", peakRow);
    decayLabel->setStyleSheet(labelStyle);
    auto* decayCombo = new GuardedComboBox(peakRow);
    decayCombo->addItems({"Fast", "Medium", "Slow"});
    decayCombo->setAccessibleName("Peak decay rate");
    decayCombo->setAccessibleDescription("Speed at which peak hold marker decays");
    decayCombo->setCurrentText(
        AppSettings::instance().value("PeakDecayRate", "Medium").toString());
    AetherSDR::applyComboStyle(decayCombo);

    auto* resetBtn = new QPushButton("RST", peakRow);
    resetBtn->setFixedSize(32, 20);
    resetBtn->setToolTip("Reset peak hold");
    resetBtn->setAccessibleName("Reset peak hold");
    resetBtn->setStyleSheet(
        "QPushButton { background: #1a1a2e; color: #8090a0; border: 1px solid #334; "
        "border-radius: 3px; font-size: 10px; padding: 0 4px; } "
        "QPushButton:pressed { background: #2a2a4e; color: #c8d8e8; }");

    peakLayout->addWidget(peakBtn);
    peakLayout->addWidget(decayLabel);
    peakLayout->addWidget(decayCombo, 1);
    peakLayout->addWidget(resetBtn);
    sMeterLayout->addWidget(peakRow);

    // Apply decay preset: also sets the hold time (Fast=200ms, Medium=500ms, Slow=1000ms)
    auto applyDecayPreset = [this](const QString& rate) {
        m_sMeter->setPeakDecayRate(rate);
        if (rate == "Fast")        m_sMeter->setPeakHoldTimeMs(200);
        else if (rate == "Slow")   m_sMeter->setPeakHoldTimeMs(1000);
        else                       m_sMeter->setPeakHoldTimeMs(500);
    };

    m_sMeter->setPeakHoldEnabled(peakBtn->isChecked());
    applyDecayPreset(decayCombo->currentText());

    connect(peakBtn, &QPushButton::toggled, this, [this](bool on) {
        m_sMeter->setPeakHoldEnabled(on);
        AppSettings::instance().setValue("PeakHoldEnabled", on ? "True" : "False");
        AppSettings::instance().save();
    });
    connect(decayCombo, &QComboBox::currentTextChanged,
            this, [this, applyDecayPreset](const QString& rate) {
        applyDecayPreset(rate);
        AppSettings::instance().setValue("PeakDecayRate", rate);
        AppSettings::instance().save();
    });
    connect(resetBtn, &QPushButton::clicked, m_sMeter, &SMeterWidget::resetPeak);

    root->addWidget(m_sMeterSection);

    // ── Scrollable applet stack (drop-aware) ────────────────────────────────
    m_scrollArea = new AppletDropArea(this);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setWidgetResizable(true);

    auto* container = new QWidget;
    m_stack = new QVBoxLayout(container);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->setSpacing(0);
    m_stack->addStretch();
    m_scrollArea->setWidget(container);
    root->addWidget(m_scrollArea, 1);

    // Drop indicator line (cyan, hidden by default)
    m_dropIndicator = new QWidget(container);
    m_dropIndicator->setFixedHeight(2);
    m_dropIndicator->setStyleSheet("background: #00b4d8;");
    m_dropIndicator->hide();

    auto& settings = AppSettings::instance();

    // ── Build all applets with title bars ────────────────────────────────────

    // Helper: create an applet entry with draggable title bar
    // Event filter to initiate drag from existing applet title bars (top 16px).
    // Installed on each wrapper widget.
    class DragFilter : public QObject {
    public:
        DragFilter(const QString& id, QWidget* parent) : QObject(parent), m_id(id) {}
    protected:
        bool eventFilter(QObject* obj, QEvent* ev) override {
            auto* w = qobject_cast<QWidget*>(obj);
            if (!w) return false;
            if (ev->type() == QEvent::MouseButtonPress) {
                auto* me = static_cast<QMouseEvent*>(ev);
                if (me->button() == Qt::LeftButton && me->pos().y() < 18)
                    m_dragStart = me->pos();
            } else if (ev->type() == QEvent::MouseMove) {
                auto* me = static_cast<QMouseEvent*>(ev);
                if ((me->buttons() & Qt::LeftButton) && !m_dragStart.isNull()
                    && (me->pos() - m_dragStart).manhattanLength() > 10) {
                    auto* drag = new QDrag(w);
                    auto* mimeData = new QMimeData;
                    mimeData->setData("application/x-aethersdr-applet", m_id.toUtf8());
                    drag->setMimeData(mimeData);
                    QPixmap pm(w->width(), 16);
                    pm.fill(Qt::transparent);
                    w->render(&pm, QPoint(), QRegion(0, 0, w->width(), 16));
                    drag->setPixmap(pm);
                    drag->setHotSpot(me->pos());
                    drag->exec(Qt::MoveAction);
                    m_dragStart = {};
                    return true;
                }
            } else if (ev->type() == QEvent::MouseButtonRelease) {
                m_dragStart = {};
            }
            return false;
        }
    private:
        QString m_id;
        QPoint m_dragStart;
    };

    auto makeEntry = [&](const QString& id, const QString& label,
                         QWidget* applet, bool defaultOn,
                         QWidget* rowParent, QHBoxLayout* rowLayout) -> AppletEntry {
        auto* titleBar = new AppletTitleBar(label, id, this);
        auto* wrapper = new QWidget;
        auto* wl = new QVBoxLayout(wrapper);
        wl->setContentsMargins(0, 0, 0, 0);
        wl->setSpacing(0);
        wl->addWidget(titleBar);
        applet->show();
        wl->addWidget(applet);

        auto* btn = new QPushButton(id, rowParent);
        btn->setCheckable(true);
        rowLayout->addWidget(btn);

        const QString key = QStringLiteral("Applet_%1").arg(id);
        bool on = settings.value(key, defaultOn ? "True" : "False").toString() == "True";
        btn->setChecked(on);
        wrapper->setVisible(on);

        connect(btn, &QPushButton::toggled, this, [this, id, wrapper, key](bool checked) {
            // If the applet is floating, the toggle button raises/hides the
            // floating window rather than showing/hiding the docked wrapper.
            if (m_floatingWindows.contains(id)) {
                if (checked)
                    m_floatingWindows[id]->raise();
                else
                    m_floatingWindows[id]->hide();
                return;
            }
            wrapper->setVisible(checked);
            AppSettings::instance().setValue(key, checked ? "True" : "False");
        });

        return {id, wrapper, titleBar, btn, false};
    };

    // Controls lock toggle — disables wheel/mouse on sidebar sliders (#745)
    {
        m_lockBtn = new QPushButton("LCK", btnRow2);
        m_lockBtn->setCheckable(true);
        m_lockBtn->setToolTip("Lock sidebar controls — prevent accidental\n"
                              "value changes while scrolling");
        m_lockBtn->setAccessibleName("Lock controls");
        m_lockBtn->setAccessibleDescription("Lock sidebar controls to prevent accidental value changes");
        bool locked = settings.value("ControlsLocked", "False").toString() == "True";
        m_lockBtn->setChecked(locked);
        ControlsLock::setLocked(locked);
        btnLayout2->addWidget(m_lockBtn);
        connect(m_lockBtn, &QPushButton::toggled, this, [this](bool on) {
            setControlsLocked(on);
        });
    }

    // ANLG / VU button — toggles the S-Meter section (not in the reorderable stack)
    {
        auto* anlgBtn = new QPushButton("VU", btnRow1);
        anlgBtn->setCheckable(true);
        bool anlgOn = settings.value("Applet_ANLG", "True").toString() == "True";
        anlgBtn->setChecked(anlgOn);
        m_sMeterSection->setVisible(anlgOn);
        btnLayout1->addWidget(anlgBtn);
        connect(anlgBtn, &QPushButton::toggled, this, [this](bool on) {
            m_sMeterSection->setVisible(on);
            AppSettings::instance().setValue("Applet_ANLG", on ? "True" : "False");
        });
    }

    // Create all applets — row 1: core, row 2: accessories/conditional
    m_rxApplet = new RxApplet;
    m_appletOrder.append(makeEntry("RX", "RX Controls", m_rxApplet, true, btnRow1, btnLayout1));

    m_tunerApplet = new TunerApplet;
    {
        auto* titleBar = new AppletTitleBar("Tuner", "TUN", this);
        auto* wrapper = new QWidget;
        auto* wl = new QVBoxLayout(wrapper);
        wl->setContentsMargins(0, 0, 0, 0);
        wl->setSpacing(0);
        wl->addWidget(titleBar);
        m_tunerApplet->show();
        wl->addWidget(m_tunerApplet);

        m_tuneBtn = new QPushButton("TUN", btnRow2);
        m_tuneBtn->setCheckable(true);
        m_tuneBtn->hide();
        btnLayout2->addWidget(m_tuneBtn);
        wrapper->hide();
        connect(m_tuneBtn, &QPushButton::toggled, wrapper, &QWidget::setVisible);
        m_appletOrder.append({"TUN", wrapper, titleBar, m_tuneBtn});
    }

    m_ampApplet = new AmpApplet;
    {
        auto* titleBar = new AppletTitleBar("Amplifier", "AMP", this);
        auto* wrapper = new QWidget;
        auto* wl = new QVBoxLayout(wrapper);
        wl->setContentsMargins(0, 0, 0, 0);
        wl->setSpacing(0);
        wl->addWidget(titleBar);
        m_ampApplet->show();
        wl->addWidget(m_ampApplet);

        m_ampBtn = new QPushButton("AMP", btnRow2);
        m_ampBtn->setCheckable(true);
        m_ampBtn->hide();
        btnLayout2->addWidget(m_ampBtn);
        wrapper->hide();
        connect(m_ampBtn, &QPushButton::toggled, wrapper, &QWidget::setVisible);
        m_appletOrder.append({"AMP", wrapper, titleBar, m_ampBtn});
    }

    m_txApplet = new TxApplet;
    m_appletOrder.append(makeEntry("TX", "TX Controls", m_txApplet, true, btnRow1, btnLayout1));

    m_phoneApplet = new PhoneApplet;
    m_appletOrder.append(makeEntry("PHNE", "Phone", m_phoneApplet, true, btnRow1, btnLayout1));

    m_phoneCwApplet = new PhoneCwApplet;
    m_appletOrder.append(makeEntry("P/CW", "Phone/CW", m_phoneCwApplet, true, btnRow1, btnLayout1));

    m_eqApplet = new EqApplet;
    m_appletOrder.append(makeEntry("EQ", "Equalizer", m_eqApplet, true, btnRow1, btnLayout1));

    m_catApplet = new CatApplet;
    m_appletOrder.append(makeEntry("DIGI", "Digital Mode Controls", m_catApplet, false, btnRow2, btnLayout2));

    m_meterApplet = new MeterApplet;
    m_appletOrder.append(makeEntry("MTR", "Meters", m_meterApplet, false, btnRow2, btnLayout2));

    m_agApplet = new AntennaGeniusApplet;
    {
        auto* wrapper = new QWidget;
        auto* wl = new QVBoxLayout(wrapper);
        wl->setContentsMargins(0, 0, 0, 0);
        wl->setSpacing(0);
        auto* titleBar = new AppletTitleBar("Antenna Genius", "AG", this);
        wl->addWidget(titleBar);
        m_agApplet->show();
        wl->addWidget(m_agApplet);

        m_agBtn = new QPushButton("AG", btnRow2);
        m_agBtn->setCheckable(true);
        m_agBtn->hide();
        btnLayout2->addWidget(m_agBtn);
        wrapper->hide();
        connect(m_agBtn, &QPushButton::toggled, wrapper, &QWidget::setVisible);
        m_appletOrder.append({"AG", wrapper, titleBar, m_agBtn});
    }

    btnLayout1->addStretch();
    btnLayout2->addStretch();

    // ── Restore saved order ─────────────────────────────────────────────────
    QString savedOrder = settings.value("AppletOrder").toString();
    if (!savedOrder.isEmpty()) {
        QStringList ids = savedOrder.split(',');
        QVector<AppletEntry> reordered;
        for (const auto& id : ids) {
            for (int i = 0; i < m_appletOrder.size(); ++i) {
                if (m_appletOrder[i].id == id) {
                    reordered.append(m_appletOrder[i]);
                    m_appletOrder.remove(i);
                    break;
                }
            }
        }
        // Append any remaining (new applets not in saved order)
        reordered.append(m_appletOrder);
        m_appletOrder = reordered;
    }

    rebuildStackOrder();

    // ── Restore floating state ──────────────────────────────────────────────
    for (auto& entry : m_appletOrder) {
        const QString floatKey = QStringLiteral("FloatingApplet_%1_IsFloating").arg(entry.id);
        if (AppSettings::instance().value(floatKey, "False").toString() == "True") {
            // Use QTimer::singleShot so the window system is ready before showing
            QTimer::singleShot(0, this, [this, id = entry.id]() { floatApplet(id); });
        }
    }

}

void AppletPanel::rebuildStackOrder()
{
    // Remove all items from layout without reparenting (avoids visibility issues)
    while (m_stack->count() > 0) {
        auto* item = m_stack->takeAt(0);
        delete item;  // deletes the layout item, NOT the widget
    }
    // Re-add in current order
    for (const auto& entry : m_appletOrder)
        m_stack->addWidget(entry.widget);
    m_stack->addStretch();
}

void AppletPanel::saveOrder()
{
    QStringList ids;
    for (const auto& entry : m_appletOrder)
        ids.append(entry.id);
    AppSettings::instance().setValue("AppletOrder", ids.join(","));
    AppSettings::instance().save();
}

void AppletPanel::resetOrder()
{
    // Reorder m_appletOrder to match kDefaultOrder
    QVector<AppletEntry> reordered;
    for (const auto& id : kDefaultOrder) {
        for (int i = 0; i < m_appletOrder.size(); ++i) {
            if (m_appletOrder[i].id == id) {
                reordered.append(m_appletOrder[i]);
                m_appletOrder.remove(i);
                break;
            }
        }
    }
    reordered.append(m_appletOrder);
    m_appletOrder = reordered;
    rebuildStackOrder();
    AppSettings::instance().remove("AppletOrder");
    AppSettings::instance().save();
}

int AppletPanel::dropIndexFromY(int localY) const
{
    int idx = 0;
    for (int i = 0; i < m_appletOrder.size(); ++i) {
        auto* w = m_appletOrder[i].widget;
        if (!w) continue;
        int mid = w->mapTo(m_scrollArea->widget(), QPoint(0, w->height() / 2)).y();
        if (localY > mid) idx = i + 1;
    }
    return idx;
}

void AppletPanel::setTunerVisible(bool visible)
{
    if (visible) {
        m_tuneBtn->show();
        if (!m_tuneBtn->isChecked())
            m_tuneBtn->setChecked(true);
    } else {
        if (m_floatingWindows.contains("TUN")) { dockApplet("TUN"); }
        m_tuneBtn->setChecked(false);
        m_tuneBtn->hide();
    }
}

void AppletPanel::setAmpVisible(bool visible)
{
    if (visible) {
        m_ampBtn->show();
        if (!m_ampBtn->isChecked())
            m_ampBtn->setChecked(true);
    } else {
        if (m_floatingWindows.contains("AMP")) { dockApplet("AMP"); }
        m_ampBtn->setChecked(false);
        m_ampBtn->hide();
    }
}

void AppletPanel::setAgVisible(bool visible)
{
    if (visible) {
        m_agBtn->show();
        if (!m_agBtn->isChecked())
            m_agBtn->setChecked(true);
    } else {
        if (m_floatingWindows.contains("AG")) { dockApplet("AG"); }
        m_agBtn->setChecked(false);
        m_agBtn->hide();
    }
}

bool AppletPanel::isAppletFloating(const QString& id) const
{
    for (const AppletEntry& e : m_appletOrder) {
        if (e.id == id) { return e.floating; }
    }
    return false;
}

bool AppletPanel::controlsLocked() const
{
    return ControlsLock::isLocked();
}

void AppletPanel::setControlsLocked(bool locked)
{
    ControlsLock::setLocked(locked);
    m_lockBtn->setText("LCK");
    m_lockBtn->setChecked(locked);
    AppSettings::instance().setValue("ControlsLocked", locked ? "True" : "False");
}

void AppletPanel::setSlice(SliceModel* slice)
{
    m_rxApplet->setSlice(slice);

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

void AppletPanel::floatApplet(const QString& id)
{
    // Find the entry
    int idx = -1;
    for (int i = 0; i < m_appletOrder.size(); ++i) {
        if (m_appletOrder[i].id == id) { idx = i; break; }
    }
    if (idx < 0 || m_appletOrder[idx].floating) { return; }

    AppletEntry& entry = m_appletOrder[idx];

    // The actual applet widget is the second item in the wrapper's layout
    // (index 0 = AppletTitleBar, index 1 = applet content)
    QWidget* appletWidget = nullptr;
    if (auto* wl = qobject_cast<QVBoxLayout*>(entry.widget->layout())) {
        if (wl->count() >= 2) {
            if (auto* item = wl->itemAt(1)) appletWidget = item->widget();
        }
    }
    if (!appletWidget) { return; }

    // Find the human-readable title from the title bar label
    QString title = id;
    if (auto* tb = static_cast<AppletTitleBar*>(entry.titleBar))
        title = tb->findChild<QLabel*>() ? tb->findChildren<QLabel*>().last()->text() : id;

    // Reparent applet out of wrapper into floating window
    appletWidget->setParent(nullptr);

    // Pass 'this' as parent so Qt treats the window as a tool window owned
    // by AppletPanel — Qt's shouldQuit() then correctly ignores tool windows
    // when deciding whether to keep the event loop alive.
    auto* win = new FloatingAppletWindow(id, title, appletWidget, this);
    m_floatingWindows[id] = win;
    entry.floating = true;

    connect(win, &FloatingAppletWindow::dockRequested,
            this, &AppletPanel::dockApplet);

    // show() first, then restore geometry after a short delay. The window
    // manager / compositor must map the window before position hints are
    // respected. QWidget::restoreGeometry() encodes screen identity so the
    // window returns to the correct monitor on X11 / XCB.
    // Note: Wayland compositors control window placement and ignore app-set
    // positions. On WSL2 (WSLg/Wayland), set QT_QPA_PLATFORM=xcb to get
    // X11 behaviour via XWayland.
    win->show();
    QTimer::singleShot(50, win, [win]() { win->restoreGeometry(); });

    // Hide wrapper in panel (keeps toggle button state as-is)
    entry.widget->hide();

    // Persist floating state
    AppSettings::instance().setValue(
        QStringLiteral("FloatingApplet_%1_IsFloating").arg(id), "True");
    AppSettings::instance().save();
}

void AppletPanel::dockApplet(const QString& id)
{
    if (!m_floatingWindows.contains(id)) { return; }

    int idx = -1;
    for (int i = 0; i < m_appletOrder.size(); ++i) {
        if (m_appletOrder[i].id == id) { idx = i; break; }
    }
    if (idx < 0) { return; }

    AppletEntry& entry = m_appletOrder[idx];
    FloatingAppletWindow* win = m_floatingWindows.value(id);

    // Retrieve the applet widget from the floating window's layout
    QWidget* appletWidget = nullptr;
    if (win && win->layout() && win->layout()->count() >= 2) {
        // root QVBoxLayout: 0=titleBar widget, 1=m_contentLayout (QLayout)
        if (auto* rootLayout = qobject_cast<QVBoxLayout*>(win->layout())) {
            if (auto* contentItem = rootLayout->itemAt(1)) {
                if (auto* contentLayout = contentItem->layout()) {
                    if (contentLayout->count() > 0) {
                        if (auto* item = contentLayout->itemAt(0))
                            appletWidget = item->widget();
                    }
                }
            }
        }
    }

    if (appletWidget) {
        // Re-insert applet widget back into wrapper layout (position 1, after title bar)
        if (auto* wl = qobject_cast<QVBoxLayout*>(entry.widget->layout())) {
            appletWidget->setParent(entry.widget);
            wl->addWidget(appletWidget);
            appletWidget->show();
        }
    }

    // Save geometry before destroying the window
    if (win) {
        win->saveGeometry();
        win->hide();
        win->deleteLater();
    }
    m_floatingWindows.remove(id);
    entry.floating = false;

    // Show wrapper only if the toggle button is checked
    if (entry.btn && entry.btn->isChecked())
        entry.widget->show();

    // Persist floating state
    AppSettings::instance().setValue(
        QStringLiteral("FloatingApplet_%1_IsFloating").arg(id), "False");
    AppSettings::instance().save();
}

} // namespace AetherSDR
