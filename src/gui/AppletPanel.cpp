#include "AppletPanel.h"
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

namespace AetherSDR {

const QStringList AppletPanel::kDefaultOrder = {
    "RX", "TUN", "AMP", "TX", "PHNE", "P/CW", "EQ", "DIGI", "AG"
};

// ── Drag-handle title bar ───────────────────────────────────────────────────

class AppletTitleBar : public QWidget {
public:
    AppletTitleBar(const QString& text, const QString& appletId, QWidget* parent = nullptr)
        : QWidget(parent), m_appletId(appletId)
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

private:
    QString m_appletId;
    QPoint  m_dragStartPos;
    QLabel* m_label{nullptr};
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

    auto* sMeterTitle = new AppletTitleBar("S-Meter", "VU");
    sMeterLayout->addWidget(sMeterTitle);

    m_sMeter = new SMeterWidget(m_sMeterSection);
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
                         QWidget* applet, bool defaultOn) -> AppletEntry {
        auto* titleBar = new AppletTitleBar(label, id);
        auto* wrapper = new QWidget;
        auto* wl = new QVBoxLayout(wrapper);
        wl->setContentsMargins(0, 0, 0, 0);
        wl->setSpacing(0);
        wl->addWidget(titleBar);
        applet->show();
        wl->addWidget(applet);

        auto* btn = new QPushButton(id, btnRow);
        btn->setCheckable(true);
        btnLayout->addWidget(btn);

        const QString key = QStringLiteral("Applet_%1").arg(id);
        bool on = settings.value(key, defaultOn ? "True" : "False").toString() == "True";
        btn->setChecked(on);
        wrapper->setVisible(on);

        connect(btn, &QPushButton::toggled, wrapper, [wrapper, key](bool checked) {
            wrapper->setVisible(checked);
            AppSettings::instance().setValue(key, checked ? "True" : "False");
        });

        return {id, wrapper, titleBar, btn};
    };

    // ANLG / VU button — toggles the S-Meter section (not in the reorderable stack)
    {
        auto* anlgBtn = new QPushButton("VU", btnRow);
        anlgBtn->setCheckable(true);
        bool anlgOn = settings.value("Applet_ANLG", "True").toString() == "True";
        anlgBtn->setChecked(anlgOn);
        m_sMeterSection->setVisible(anlgOn);
        btnLayout->addWidget(anlgBtn);
        connect(anlgBtn, &QPushButton::toggled, this, [this](bool on) {
            m_sMeterSection->setVisible(on);
            AppSettings::instance().setValue("Applet_ANLG", on ? "True" : "False");
        });
    }

    // Create all applets
    m_rxApplet = new RxApplet;
    m_appletOrder.append(makeEntry("RX", "RX Controls", m_rxApplet, true));

    m_tunerApplet = new TunerApplet;
    {
        auto* titleBar = new AppletTitleBar("Tuner", "TUN");
        auto* wrapper = new QWidget;
        auto* wl = new QVBoxLayout(wrapper);
        wl->setContentsMargins(0, 0, 0, 0);
        wl->setSpacing(0);
        wl->addWidget(titleBar);
        m_tunerApplet->show();
        wl->addWidget(m_tunerApplet);

        m_tuneBtn = new QPushButton("TUN", btnRow);
        m_tuneBtn->setCheckable(true);
        m_tuneBtn->hide();
        btnLayout->addWidget(m_tuneBtn);
        wrapper->hide();
        connect(m_tuneBtn, &QPushButton::toggled, wrapper, &QWidget::setVisible);
        m_appletOrder.append({"TUN", wrapper, titleBar, m_tuneBtn});
    }

    m_ampApplet = new AmpApplet;
    {
        auto* titleBar = new AppletTitleBar("Amplifier", "AMP");
        auto* wrapper = new QWidget;
        auto* wl = new QVBoxLayout(wrapper);
        wl->setContentsMargins(0, 0, 0, 0);
        wl->setSpacing(0);
        wl->addWidget(titleBar);
        m_ampApplet->show();
        wl->addWidget(m_ampApplet);

        m_ampBtn = new QPushButton("AMP", btnRow);
        m_ampBtn->setCheckable(true);
        m_ampBtn->hide();
        btnLayout->addWidget(m_ampBtn);
        wrapper->hide();
        connect(m_ampBtn, &QPushButton::toggled, wrapper, &QWidget::setVisible);
        m_appletOrder.append({"AMP", wrapper, titleBar, m_ampBtn});
    }

    m_txApplet = new TxApplet;
    m_appletOrder.append(makeEntry("TX", "TX Controls", m_txApplet, true));

    m_phoneApplet = new PhoneApplet;
    m_appletOrder.append(makeEntry("PHNE", "Phone", m_phoneApplet, true));

    m_phoneCwApplet = new PhoneCwApplet;
    m_appletOrder.append(makeEntry("P/CW", "Phone/CW", m_phoneCwApplet, true));

    m_eqApplet = new EqApplet;
    m_appletOrder.append(makeEntry("EQ", "Equalizer", m_eqApplet, true));

    m_catApplet = new CatApplet;
    m_appletOrder.append(makeEntry("DIGI", "Digital Mode Controls", m_catApplet, false));

    m_agApplet = new AntennaGeniusApplet;
    {
        auto* wrapper = new QWidget;
        auto* wl = new QVBoxLayout(wrapper);
        wl->setContentsMargins(0, 0, 0, 0);
        wl->setSpacing(0);
        auto* titleBar = new AppletTitleBar("Antenna Genius", "AG");
        wl->addWidget(titleBar);
        m_agApplet->show();
        wl->addWidget(m_agApplet);

        m_agBtn = new QPushButton("AG", btnRow);
        m_agBtn->setCheckable(true);
        m_agBtn->hide();
        btnLayout->addWidget(m_agBtn);
        wrapper->hide();
        connect(m_agBtn, &QPushButton::toggled, wrapper, &QWidget::setVisible);
        m_appletOrder.append({"AG", wrapper, titleBar, m_agBtn});
    }

    btnLayout->addStretch();

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
        m_agBtn->setChecked(false);
        m_agBtn->hide();
    }
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

} // namespace AetherSDR
