#include "MemoryDialog.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "core/RadioConnection.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QHeaderView>
#include <QDebug>
#include <QTimer>

namespace AetherSDR {

namespace {

class MemoryTableItem : public QTableWidgetItem {
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem& other) const override
    {
        const QVariant lhs = data(Qt::UserRole);
        const QVariant rhs = other.data(Qt::UserRole);
        if (lhs.isValid() && rhs.isValid())
            return lhs.toDouble() < rhs.toDouble();
        return QTableWidgetItem::operator<(other);
    }
};

} // namespace

static const QStringList COLUMNS = {
    "Group", "Owner", "Frequency", "Name", "Mode", "Step",
    "FM TX Offset Dir", "Repeater Offset", "Tone Mode", "Tone Value",
    "Squelch", "Squelch Level", "RX Filter Low", "RX Filter High",
    "RTTY Mark", "RTTY Shift", "DIGL Offset", "DIGU Offset"
};

MemoryDialog::MemoryDialog(RadioModel* model, QWidget* parent)
    : QDialog(parent), m_model(model)
{
    setWindowTitle("Memory Channels");
    resize(1000, 500);

    auto* root = new QVBoxLayout(this);

    // ── Table ─────────────────────────────────────────────────────────────
    m_table = new QTableWidget(0, COLUMNS.size());
    m_table->setHorizontalHeaderLabels(COLUMNS);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::DoubleClicked);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    m_table->setSortingEnabled(false);
    m_table->setStyleSheet(
        "QTableWidget { alternate-background-color: #1a1a2e; }"
        "QTableWidget::item:selected { background: #2060a0; }");
    auto* header = m_table->horizontalHeader();
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(false);
    header->resizeSection(2, 110);
    connect(header, &QHeaderView::sectionClicked, this, [this, header](int section) {
        if (!isSortableColumn(section)) return;
        if (m_sortColumn == section)
            m_sortOrder = (m_sortOrder == Qt::AscendingOrder)
                ? Qt::DescendingOrder : Qt::AscendingOrder;
        else {
            m_sortColumn = section;
            m_sortOrder = Qt::AscendingOrder;
        }
        header->setSortIndicatorShown(true);
        header->setSortIndicator(m_sortColumn, m_sortOrder);
        m_table->sortItems(m_sortColumn, m_sortOrder);
    });
    root->addWidget(m_table);

    // ── Buttons ───────────────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    auto* addBtn = new QPushButton("Add");
    auto* selectBtn = new QPushButton("Select");
    auto* removeBtn = new QPushButton("Remove");
    btnRow->addWidget(addBtn);
    btnRow->addWidget(selectBtn);
    btnRow->addStretch();
    btnRow->addWidget(removeBtn);
    root->addLayout(btnRow);

    connect(addBtn, &QPushButton::clicked, this, &MemoryDialog::onAdd);
    connect(selectBtn, &QPushButton::clicked, this, &MemoryDialog::onSelect);
    connect(removeBtn, &QPushButton::clicked, this, &MemoryDialog::onRemove);

    // Listen for live memory updates while dialog is open
    connect(model, &RadioModel::statusReceived,
            this, [this](const QString& object, const QMap<QString, QString>&) {
        if (object.startsWith("memory "))
            QTimer::singleShot(50, this, [this]() { populateTable(); });
    });
    connect(model, &RadioModel::memoryRemoved,
            this, [this](int) {
        populateTable();
    });

    // Send edits to the radio when any cell changes
    connect(m_table, &QTableWidget::cellChanged, this, [this](int row, int col) {
        auto* indexItem = m_table->item(row, 0);
        if (!indexItem) return;
        int memIdx = indexItem->data(Qt::UserRole).toInt();
        auto* item = m_table->item(row, col);
        if (!item) return;
        QString val = item->text();
        QString base = QString("memory set %1 ").arg(memIdx);

        // Map column index to memory set parameter
        switch (col) {
        case 0:  m_model->sendCommand(base + "group=" + val.replace(' ', '\x7f')); break;
        case 1:  m_model->sendCommand(base + "owner=" + val.replace(' ', '\x7f')); break;
        case 2:  m_model->sendCommand(base + "freq=" + val); break;
        case 3:  m_model->sendCommand(base + "name=" + val.replace(' ', '\x7f')); break;
        case 4:  m_model->sendCommand(base + "mode=" + val); break;
        case 5:  m_model->sendCommand(base + "step=" + val); break;
        case 6:  m_model->sendCommand(base + "repeater=" + val); break;
        case 7:  m_model->sendCommand(base + "repeater_offset=" + val); break;
        case 8:  m_model->sendCommand(base + "tone_mode=" + val); break;
        case 9:  m_model->sendCommand(base + "tone_value=" + val); break;
        case 10: m_model->sendCommand(base + QString("squelch=%1")
                     .arg(item->checkState() == Qt::Checked ? 1 : 0)); break;
        case 11: m_model->sendCommand(base + "squelch_level=" + val); break;
        case 12: m_model->sendCommand(base + "rx_filter_low=" + val); break;
        case 13: m_model->sendCommand(base + "rx_filter_high=" + val); break;
        case 14: m_model->sendCommand(base + "rtty_mark=" + val); break;
        case 15: m_model->sendCommand(base + "rtty_shift=" + val); break;
        case 16: m_model->sendCommand(base + "digl_offset=" + val); break;
        case 17: m_model->sendCommand(base + "digu_offset=" + val); break;
        default: break;
        }
    });

    // The radio doesn't support "sub memory all" or "memory list".
    // Populate from RadioModel cache (filled from status pushes during connect).
    // If cache is empty, memories may not have been pushed yet. As a fallback,
    // new memories created via Add will populate the table immediately.
    populateTable();
}

void MemoryDialog::populateTable()
{
    const QSignalBlocker blocker(m_table);
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    const auto& memories = m_model->memories();

    for (auto it = memories.begin(); it != memories.end(); ++it) {
        const auto& m = it.value();
        int row = m_table->rowCount();
        m_table->insertRow(row);

        int col = 0;
        m_table->setItem(row, col++, new QTableWidgetItem(m.group));
        m_table->setItem(row, col++, new QTableWidgetItem(m.owner));
        auto* freqItem = new MemoryTableItem(QString::number(m.freq, 'f', 3));
        freqItem->setData(Qt::UserRole, m.freq);
        m_table->setItem(row, col++, freqItem);
        m_table->setItem(row, col++, new QTableWidgetItem(m.name));
        m_table->setItem(row, col++, new QTableWidgetItem(m.mode));
        m_table->setItem(row, col++, new QTableWidgetItem(
            QString::number(m.step)));
        m_table->setItem(row, col++, new QTableWidgetItem(m.offsetDir));
        m_table->setItem(row, col++, new QTableWidgetItem(
            QString::number(m.repeaterOffset, 'f', 1)));
        m_table->setItem(row, col++, new QTableWidgetItem(m.toneMode));
        m_table->setItem(row, col++, new QTableWidgetItem(
            QString::number(m.toneValue, 'f', 1)));

        // Squelch checkbox column
        auto* sqItem = new QTableWidgetItem();
        sqItem->setCheckState(m.squelch ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(row, col++, sqItem);

        m_table->setItem(row, col++, new QTableWidgetItem(
            QString::number(m.squelchLevel)));
        m_table->setItem(row, col++, new QTableWidgetItem(
            QString::number(m.rxFilterLow)));
        m_table->setItem(row, col++, new QTableWidgetItem(
            QString::number(m.rxFilterHigh)));
        m_table->setItem(row, col++, new QTableWidgetItem(
            QString::number(m.rttyMark)));
        m_table->setItem(row, col++, new QTableWidgetItem(
            QString::number(m.rttyShift)));
        m_table->setItem(row, col++, new QTableWidgetItem(
            QString::number(m.diglOffset)));
        m_table->setItem(row, col++, new QTableWidgetItem(
            QString::number(m.diguOffset)));

        // Store memory index in first column's data for retrieval
        m_table->item(row, 0)->setData(Qt::UserRole, m.index);

        // All columns are editable (double-click to edit).
        // Squelch column (10) uses checkbox — keep it user-checkable.
        for (int c = 0; c < m_table->columnCount(); ++c) {
            auto* item = m_table->item(row, c);
            if (item && c != 10)
                item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
    }

    m_table->resizeColumnsToContents();
    m_table->setColumnWidth(2, std::max(m_table->columnWidth(2), 105));
    if (isSortableColumn(m_sortColumn)) {
        auto* header = m_table->horizontalHeader();
        header->setSortIndicatorShown(true);
        header->setSortIndicator(m_sortColumn, m_sortOrder);
        m_table->sortItems(m_sortColumn, m_sortOrder);
    }
    m_table->setSortingEnabled(true);
}

bool MemoryDialog::isSortableColumn(int column) const
{
    return column == 2 || column == 3 || column == 4;
}

void MemoryDialog::onAdd()
{
    // memory create returns the new index in the response body.
    // We then populate it from the current active slice state.
    m_model->sendCmdPublic("memory create",
        [this](int code, const QString& body) {
        if (code != 0) return;
        bool ok;
        int idx = body.trimmed().toInt(&ok);
        if (!ok) return;

        // Get current slice state to populate the memory
        const auto& mems = m_model->memories();
        Q_UNUSED(mems);

        // The radio created the memory but won't push status.
        // We need to set each field from the active slice.
        auto slices = m_model->slices();
        if (slices.isEmpty()) return;
        auto* s = slices.first();

        // Set memory fields from current slice
        auto send = [this](const QString& cmd) { m_model->sendCommand(cmd); };
        QString base = QString("memory set %1 ").arg(idx);
        send(base + QString("freq=%1").arg(s->frequency(), 0, 'f', 6));
        send(base + QString("mode=%1").arg(s->mode()));
        send(base + QString("owner=%1").arg(m_model->callsign().replace(' ', '\x7f')));
        send(base + QString("rx_filter_low=%1").arg(s->filterLow()));
        send(base + QString("rx_filter_high=%1").arg(s->filterHigh()));
        send(base + QString("squelch=%1").arg(s->squelchOn() ? 1 : 0));
        send(base + QString("squelch_level=%1").arg(s->squelchLevel()));
        send(base + QString("repeater=%1").arg(s->repeaterOffsetDir()));
        send(base + QString("rtty_mark=%1").arg(s->rttyMark()));
        send(base + QString("rtty_shift=%1").arg(s->rttyShift()));

        // Create a local cache entry immediately for the table
        MemoryEntry m;
        m.index = idx;
        m.freq = s->frequency();
        m.mode = s->mode();
        m.owner = m_model->callsign();
        m.rxFilterLow = s->filterLow();
        m.rxFilterHigh = s->filterHigh();
        m.squelch = s->squelchOn();
        m.squelchLevel = s->squelchLevel();
        m.offsetDir = s->repeaterOffsetDir();
        m.rttyMark = s->rttyMark();
        m.rttyShift = s->rttyShift();

        // Insert into RadioModel's cache via handleMemoryStatus
        // Insert directly into RadioModel's cache
        QMap<QString, QString> kvs;
        kvs["freq"] = QString::number(m.freq, 'f', 6);
        kvs["mode"] = m.mode;
        kvs["owner"] = m.owner.replace(' ', '\x7f');
        kvs["rx_filter_low"] = QString::number(m.rxFilterLow);
        kvs["rx_filter_high"] = QString::number(m.rxFilterHigh);
        kvs["squelch"] = m.squelch ? "1" : "0";
        kvs["squelch_level"] = QString::number(m.squelchLevel);
        kvs["repeater"] = m.offsetDir;
        kvs["rtty_mark"] = QString::number(m.rttyMark);
        kvs["rtty_shift"] = QString::number(m.rttyShift);
        m_model->handleMemoryStatus(idx, kvs);

        populateTable();
    });
}

void MemoryDialog::onSelect()
{
    const int row = m_table->currentRow();
    if (row < 0) return;
    const int idx = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    m_model->sendCommand(QString("memory apply %1").arg(idx));
}

void MemoryDialog::onRemove()
{
    const int row = m_table->currentRow();
    if (row < 0) return;
    const int idx = m_table->item(row, 0)->data(Qt::UserRole).toInt();
    m_model->sendCommand(QString("memory remove %1").arg(idx));
}

} // namespace AetherSDR
