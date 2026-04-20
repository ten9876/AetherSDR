#include "AppletPanel.h"
#include "containers/ContainerManager.h"
#include "containers/ContainerTitleBar.h"
#include "containers/ContainerWidget.h"
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
#include "ClientEqApplet.h"
#include "ClientCompApplet.h"
#include "ClientGateApplet.h"
#include "ClientDeEssApplet.h"
#include "ClientTubeApplet.h"
#include "ClientPuduApplet.h"
#include "ClientReverbApplet.h"
#include "ClientChainApplet.h"
#include "CatControlApplet.h"
#include "DaxApplet.h"
#include "TciApplet.h"
#include "DaxIqApplet.h"
#include "AntennaGeniusApplet.h"
#include "MeterApplet.h"
#ifdef HAVE_MQTT
#include "MqttApplet.h"
#endif
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
    "RX", "TUN", "AMP", "TX", "PHNE", "P/CW", "EQ", "TXDSP", "CAT", "DAX", "TCI", "IQ", "MTR", "AG"
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

    // Phase 4a (#1713) — container system groundwork.  Created early
    // so the S-Meter (and any future root-level containers) can wrap
    // themselves here.  The root "sidebar" container is a hidden peer
    // reserved for later phases; existing applets still live in the
    // legacy m_stack alongside it.
    m_containerMgr = new ContainerManager(this);
    m_rootSidebar = m_containerMgr->createContainer("sidebar", "Sidebar");
    m_rootSidebar->titleBar()->setCloseButtonVisible(false);
    m_rootSidebar->hide();

    // ── S-Meter section (wrapped in a ContainerWidget like every
    //    other applet — #1713 Phase 4d) ─────────────────────────────
    auto* sMeterContent = new QWidget;
    auto* contentLayout = new QVBoxLayout(sMeterContent);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_sMeter = new SMeterWidget(sMeterContent);
    m_sMeter->setAccessibleName("S-Meter");
    m_sMeter->setAccessibleDescription("Signal strength meter, shows S-units or TX power");
    contentLayout->addWidget(m_sMeter);

    // ── TX / RX meter select row ──────────────────────────────────────────
    auto* selectRow = new QWidget(sMeterContent);
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
    contentLayout->addWidget(selectRow);

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
    auto* peakRow = new QWidget(sMeterContent);
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
    contentLayout->addWidget(peakRow);

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

    // One-shot migration: legacy Applet_ANLG visibility key → Applet_VU.
    // Run before reading Applet_VU so the first launch after upgrade
    // picks up the user's prior on/off state.
    {
        auto& s = AppSettings::instance();
        if (!s.contains("Applet_VU") && s.contains("Applet_ANLG")) {
            s.setValue("Applet_VU", s.value("Applet_ANLG", "True").toString());
            s.remove("Applet_ANLG");
        }
    }

    // Wrap the S-Meter content in a container and park it at the top
    // of the sidebar (outside the reorderable applet stack).  The
    // container's own titlebar provides drag/float/close; the VU
    // tray button toggles its visibility.
    m_sMeterContainer = m_containerMgr->createContainer("VU", "S-Meter");
    m_sMeterContainer->setContent(sMeterContent);
    const bool sMeterOn = AppSettings::instance()
        .value("Applet_VU", "True").toString() == "True";
    m_sMeterContainer->setContainerVisible(sMeterOn);
    root->addWidget(m_sMeterContainer);

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

    // Migrate Applet_DIGI → Applet_CAT on first run after the DIGI split (#1627).
    // DAX/TCI/IQ default off regardless — only the CAT tile inherits the old
    // DIGI visibility because the CAT button replaces DIGI's slot in the tray.
    if (settings.contains("Applet_DIGI") && !settings.contains("Applet_CAT")) {
        settings.setValue("Applet_CAT", settings.value("Applet_DIGI", "False").toString());
        settings.remove("Applet_DIGI");
        settings.save();
    }

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

    // Post-Phase-4c: each applet lives inside a ContainerWidget with
    // its own ContainerTitleBar (drag handle + float + close buttons).
    // `contentOrContainer` may be either the raw applet widget (we
    // wrap it in a new leaf container) or an already-built
    // ContainerWidget (used by composite tiles like TXDSP whose
    // structure was built outside this helper) — qobject_cast picks
    // the right path.
    auto makeEntry = [&](const QString& id, const QString& label,
                         QWidget* contentOrContainer, bool defaultOn,
                         QWidget* rowParent, QHBoxLayout* rowLayout) -> AppletEntry {
        ContainerWidget* c =
            qobject_cast<ContainerWidget*>(contentOrContainer);
        if (!c) {
            c = m_containerMgr->createContainer(id, label);
            c->setContent(contentOrContainer);
        }

        QPushButton* btn = nullptr;
        if (rowLayout) {
            btn = new QPushButton(id, rowParent);
            btn->setCheckable(true);
            rowLayout->addWidget(btn);
        }

        const QString key = QStringLiteral("Applet_%1").arg(id);
        bool on = settings.value(key, defaultOn ? "True" : "False").toString() == "True";
        if (btn) btn->setChecked(on);
        c->setContainerVisible(on);

        // One-shot legacy-float migration: if the old
        // FloatingApplet_<ID>_IsFloating key says this applet was
        // floating before the container refactor, route it through
        // the container manager so the float state carries over.
        // The key is read once; the new ContainerManager persistence
        // takes over from that point.
        const QString legacyFloatKey =
            QStringLiteral("FloatingApplet_%1_IsFloating").arg(id);
        if (settings.value(legacyFloatKey, "False").toString() == "True" && on) {
            QTimer::singleShot(0, this, [this, id]() {
                if (m_containerMgr) m_containerMgr->floatContainer(id);
            });
        }

        if (btn) {
            connect(btn, &QPushButton::toggled, this,
                    [this, id, c, key](bool checked) {
                // Floating containers: raising = show the window,
                // lowering = hide it.  The manager owns the window
                // so we just toggle the container's visibility.
                if (c->isFloating()) {
                    if (auto* w = c->window())
                        w->setVisible(checked);
                    return;
                }
                c->setContainerVisible(checked);
                AppSettings::instance().setValue(key, checked ? "True" : "False");
            });
        }

        // Propagate visibility changes (e.g. driven by the close
        // button on the ContainerTitleBar) back to the tray toggle
        // and settings so everything stays in sync.
        connect(c, &ContainerWidget::visibilityChanged, this,
                [this, btn, key](bool visible) {
            if (btn) {
                QSignalBlocker b(btn);
                btn->setChecked(visible);
            }
            AppSettings::instance().setValue(key, visible ? "True" : "False");
        });

        return {id, c, c->titleBar(), btn};
    };

    // Controls lock toggle — disables wheel/mouse on sidebar sliders (#745)
    {
        m_lockBtn = new QPushButton("LCK", btnRow2);
        m_lockBtn->setCheckable(true);
        m_lockBtn->setToolTip("Lock sidebar controls — prevent accidental\n"
                              "value changes while scrolling");
        m_lockBtn->setAccessibleName("Lock controls");
        m_lockBtn->setAccessibleDescription("Lock sidebar controls to prevent accidental value changes");
        // LCK is session-only: start every app launch unlocked and do not
        // restore or persist the prior session's state.
        AppSettings::instance().remove("ControlsLocked");
        m_lockBtn->setChecked(false);
        ControlsLock::setLocked(false);
        btnLayout2->addWidget(m_lockBtn);
        connect(m_lockBtn, &QPushButton::toggled, this, [this](bool on) {
            setControlsLocked(on);
        });
    }

    // VU button — toggles the S-Meter container (not in the
    // reorderable stack; sits permanently at the top of the sidebar).
    {
        m_vuBtn = new QPushButton("VU", btnRow1);
        m_vuBtn->setCheckable(true);
        m_vuBtn->setChecked(sMeterOn);
        btnLayout1->addWidget(m_vuBtn);

        connect(m_vuBtn, &QPushButton::toggled, this, [this](bool on) {
            if (!m_sMeterContainer) return;
            // When floating, toggle the floating window visibility
            // instead of the (empty) panel slot.
            if (m_sMeterContainer->isFloating()) {
                if (auto* w = m_sMeterContainer->window())
                    w->setVisible(on);
                return;
            }
            m_sMeterContainer->setContainerVisible(on);
            AppSettings::instance().setValue(
                "Applet_VU", on ? "True" : "False");
        });

        // Keep the button in sync with close-button / external
        // visibility changes.
        connect(m_sMeterContainer, &ContainerWidget::visibilityChanged,
                this, [this](bool visible) {
            if (m_vuBtn) {
                QSignalBlocker b(m_vuBtn);
                m_vuBtn->setChecked(visible);
            }
            AppSettings::instance().setValue(
                "Applet_VU", visible ? "True" : "False");
        });

        // Legacy float-state migration: if the user had the S-Meter
        // popped out under the old FloatingAppletWindow path, carry
        // that over to the container system on first launch.
        if (sMeterOn && settings.value(
                "FloatingApplet_VU_IsFloating", "False").toString() == "True") {
            QTimer::singleShot(0, this, [this]() {
                if (m_containerMgr) m_containerMgr->floatContainer("VU");
            });
        }
    }

    // Create all applets — row 1: core, row 2: accessories/conditional
    m_rxApplet = new RxApplet;
    m_appletOrder.append(makeEntry("RX", "RX Controls", m_rxApplet, true, btnRow1, btnLayout1));

    // Tuner / Amp entries use makeEntry like everything else;
    // MainWindow toggles tray-button visibility via setTunerVisible /
    // setAmpVisible once the hardware reports its presence.  Until
    // then the tray button stays hidden and the container itself
    // starts hidden (defaultOn = false).
    m_tunerApplet = new TunerApplet;
    {
        auto entry = makeEntry("TUN", "Tuner", m_tunerApplet, false,
                               btnRow1, btnLayout1);
        m_tuneBtn = entry.btn;
        if (m_tuneBtn) m_tuneBtn->hide();
        m_appletOrder.append(entry);
    }

    m_ampApplet = new AmpApplet;
    {
        auto entry = makeEntry("AMP", "Amplifier", m_ampApplet, false,
                               btnRow1, btnLayout1);
        m_ampBtn = entry.btn;
        if (m_ampBtn) m_ampBtn->hide();
        m_appletOrder.append(entry);
    }

    m_txApplet = new TxApplet;
    m_appletOrder.append(makeEntry("TX", "TX Controls", m_txApplet, true, btnRow1, btnLayout1));

    m_phoneApplet = new PhoneApplet;
    m_appletOrder.append(makeEntry("PHNE", "Phone", m_phoneApplet, true, btnRow1, btnLayout1));

    m_phoneCwApplet = new PhoneCwApplet;
    m_appletOrder.append(makeEntry("P/CW", "Phone/CW", m_phoneCwApplet, true, btnRow1, btnLayout1));

    m_eqApplet = new EqApplet;
    m_appletOrder.append(makeEntry("EQ", "Equalizer", m_eqApplet, true, btnRow1, btnLayout1));

    // CEQ and CMP intentionally have no toggle button in the tray —
    // their visibility follows DSP bypass state, driven externally
    // from the CHAIN widget and the respective floating editors.
    // TX DSP applets — instead of three independent AppletEntries,
    // we wrap them inside a single nested container (#1713 Phase 5).
    // Each applet becomes the content of its own sub-ContainerWidget
    // with a ContainerTitleBar offering per-section float / close;
    // the three sub-containers live inside a parent "tx_dsp"
    // container whose own titlebar is hidden (the outer AppletEntry
    // wrapper provides the group's drag-handle + tray-toggle).
    m_clientEqApplet    = new ClientEqApplet;
    m_clientCompApplet  = new ClientCompApplet;
    m_clientGateApplet  = new ClientGateApplet;
    m_clientDeEssApplet = new ClientDeEssApplet;
    m_clientTubeApplet  = new ClientTubeApplet;
    m_clientPuduApplet  = new ClientPuduApplet;
    m_clientReverbApplet = new ClientReverbApplet;
    m_clientChainApplet = new ClientChainApplet;

    auto* txDsp = m_containerMgr->createContainer(
        "tx_dsp", QString::fromUtf8("PooDoo\xe2\x84\xa2 Audio"));

    auto makeChildContainer = [this, txDsp](const QString& id,
                                            const QString& title,
                                            QWidget* applet,
                                            int index) {
        auto* child = m_containerMgr->createContainer(
            id, title, /*contentType=*/{}, /*parentId=*/"tx_dsp", index);
        if (child) child->setContent(applet);
        return child;
    };
    // CHAIN lives directly inside the TX DSP container body — no
    // sub-titlebar, no independent pop-out.  Floating the TX DSP
    // parent carries CHAIN along with it.  (CEQ and CMP remain as
    // pop-outable sub-containers since users commonly want them
    // floating while working on the chain.)
    if (txDsp) txDsp->insertChildWidget(-1, m_clientChainApplet);
    makeChildContainer("gate",  "GATE",  m_clientGateApplet,   -1);
    makeChildContainer("ceq",   "CEQ",   m_clientEqApplet,     -1);
    makeChildContainer("dess",  "DESS",  m_clientDeEssApplet,  -1);
    makeChildContainer("cmp",   "COMPRESSOR",   m_clientCompApplet,   -1);
    makeChildContainer("tube",  "TUBE",  m_clientTubeApplet,   -1);
    makeChildContainer("pudu",  "PUDU",  m_clientPuduApplet,   -1);
    makeChildContainer("reverb","REVERB",m_clientReverbApplet, -1);

    // One-shot settings migration (#1713 Phase 4b): carry over the
    // legacy Applet_CHAIN visibility to the new Applet_TXDSP key so
    // existing users who had the chain tile showing don't have to
    // re-enable it.  Run once — only fills TXDSP when it's absent.
    if (!settings.contains("Applet_TXDSP")
        && settings.contains("Applet_CHAIN")) {
        settings.setValue(
            "Applet_TXDSP",
            settings.value("Applet_CHAIN", "False").toString());
    }

    // Register the composite tx_dsp container as one tile in the
    // applet tray — users toggle it, drag it, and pop it out as a
    // unit.  Individual section pop-outs happen via each sub-
    // container's own titlebar inside.  Button label is "PUDU" —
    // matches the exciter stage name and the PooDoo™ Audio brand.
    // Settings ID stays TXDSP for persistence.
    {
        auto entry = makeEntry("TXDSP", "PUDU", txDsp, false,
                               btnRow2, btnLayout2);
        if (entry.btn) entry.btn->setText("PUDU");
        m_appletOrder.append(entry);
    }

    m_catControlApplet = new CatControlApplet;
    m_appletOrder.append(makeEntry("CAT", "CAT Control", m_catControlApplet, false, btnRow2, btnLayout2));

    m_daxApplet = new DaxApplet;
    m_appletOrder.append(makeEntry("DAX", "DAX Audio", m_daxApplet, false, btnRow2, btnLayout2));

    m_tciApplet = new TciApplet;
    m_appletOrder.append(makeEntry("TCI", "TCI Server", m_tciApplet, false, btnRow2, btnLayout2));

    m_daxIqApplet = new DaxIqApplet;
    m_appletOrder.append(makeEntry("IQ", "DAX IQ", m_daxIqApplet, false, btnRow2, btnLayout2));

    m_meterApplet = new MeterApplet;
    m_appletOrder.append(makeEntry("MTR", "Meters", m_meterApplet, false, btnRow2, btnLayout2));

    m_agApplet = new AntennaGeniusApplet;
    {
        auto entry = makeEntry("AG", "Antenna Genius", m_agApplet, false,
                               btnRow2, btnLayout2);
        m_agBtn = entry.btn;
        if (m_agBtn) m_agBtn->hide();
        m_appletOrder.append(entry);
    }

#ifdef HAVE_MQTT
    m_mqttApplet = new MqttApplet;
    m_appletOrder.append(makeEntry("MQTT", "MQTT", m_mqttApplet, false, btnRow2, btnLayout2));
#endif

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

    // Float-state restore for individual applets happens per-entry in
    // makeEntry via the legacy FloatingApplet_<id>_IsFloating migration;
    // no separate loop needed.
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

void AppletPanel::setAppletVisible(const QString& id, bool visible)
{
    for (const auto& entry : m_appletOrder) {
        if (entry.id != id) continue;
        if (auto* c = qobject_cast<ContainerWidget*>(entry.widget)) {
            c->setContainerVisible(visible);
        } else if (entry.widget) {
            entry.widget->setVisible(visible);
        }
        if (entry.btn) {
            QSignalBlocker b(entry.btn);
            entry.btn->setChecked(visible);
        }
        return;
    }

    // Fall through to the container manager — some applets
    // (CEQ, CMP, CHAIN since #1713 Phase 4b) live as sub-containers
    // inside a composite tile rather than as standalone AppletEntry
    // instances, so the legacy id lookup misses them.  Try case-
    // insensitively since MainWindow calls use upper-case while the
    // container IDs are lower-case.
    if (m_containerMgr) {
        if (auto* c = m_containerMgr->container(id.toLower())) {
            c->setContainerVisible(visible);
        }
    }
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

// Conditional-presence setters for hardware-dependent tiles (tuner,
// amplifier, Antenna Genius).  MainWindow calls these when the
// corresponding device is discovered / lost; the tray button shows
// and the container becomes available.  Float state is restored
// automatically by the one-shot legacy migration in makeEntry.

static void applyConditionalPresence(QPushButton* btn,
                                     const QString& appletKey,
                                     bool visible)
{
    if (!btn) return;
    if (visible) {
        btn->show();
        const bool savedOn =
            AppSettings::instance().value(appletKey, "True").toString() == "True";
        if (savedOn && !btn->isChecked()) btn->setChecked(true);
    } else {
        // Preserve the checked state in settings before we flip it off,
        // so a later reconnect restores the user's preference.
        btn->setChecked(false);
        btn->hide();
    }
}

void AppletPanel::setTunerVisible(bool visible)
{
    applyConditionalPresence(m_tuneBtn, "Applet_TUN", visible);
}

void AppletPanel::setAmpVisible(bool visible)
{
    applyConditionalPresence(m_ampBtn, "Applet_AMP", visible);
}

void AppletPanel::setAgVisible(bool visible)
{
    applyConditionalPresence(m_agBtn, "Applet_AG", visible);
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

void AppletPanel::setMaxSlices(int maxSlices)
{
    m_rxApplet->setMaxSlices(maxSlices);
}

void AppletPanel::updateSliceButtons(const QList<SliceModel*>& slices, int activeSliceId)
{
    m_rxApplet->updateSliceButtons(slices, activeSliceId);
}

void AppletPanel::setTxDspChainOrder(
    const QVector<AudioEngine::TxChainStage>& stages)
{
    if (!m_containerMgr) return;
    auto* parent = m_containerMgr->container("tx_dsp");
    if (!parent) return;

    // Map enum → child-container id.  Stages not present in the chain
    // are ignored (they stay wherever they currently sit; their tiles
    // should have been hidden via stageEnabledChanged anyway).
    auto idFor = [](AudioEngine::TxChainStage s) -> QString {
        switch (s) {
            case AudioEngine::TxChainStage::Gate:   return "gate";
            case AudioEngine::TxChainStage::Eq:     return "ceq";
            case AudioEngine::TxChainStage::DeEss:  return "dess";
            case AudioEngine::TxChainStage::Comp:   return "cmp";
            case AudioEngine::TxChainStage::Tube:   return "tube";
            case AudioEngine::TxChainStage::Enh:    return "pudu";
            case AudioEngine::TxChainStage::Reverb: return "reverb";
            case AudioEngine::TxChainStage::None:   return {};
        }
        return {};
    };

    // Pluck each ordered child out of its current position and re-
    // insert at the end.  Walking the list in chain order rebuilds the
    // parent body in the desired order without touching CHAIN itself —
    // CHAIN lives at index 0 and isn't in the ordered set, so it stays
    // put as the earlier siblings migrate to the end around it.
    for (auto s : stages) {
        const QString id = idFor(s);
        if (id.isEmpty()) continue;
        auto* child = m_containerMgr->container(id);
        if (!child) continue;
        parent->removeChildWidget(child);
        parent->insertChildWidget(-1, child);
    }
}


} // namespace AetherSDR
