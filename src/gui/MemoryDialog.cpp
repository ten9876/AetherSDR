#include "MemoryDialog.h"
#include "core/AppSettings.h"
#include "core/MemoryCsvCompat.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "core/RadioConnection.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QHeaderView>
#include <QDebug>
#include <QMessageBox>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QCloseEvent>

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

QString encodeMemoryText(const QString& value)
{
    return QString(value).replace(' ', QChar(0x7f));
}

bool buildMemoryFieldUpdate(int col, const QTableWidgetItem* item,
                            QString& commandSuffix, QMap<QString, QString>& kvs)
{
    if (!item)
        return false;

    const QString value = item->text();
    switch (col) {
    case 0: {
        const QString encoded = encodeMemoryText(value);
        commandSuffix = "group=" + encoded;
        kvs["group"] = encoded;
        return true;
    }
    case 1: {
        const QString encoded = encodeMemoryText(value);
        commandSuffix = "owner=" + encoded;
        kvs["owner"] = encoded;
        return true;
    }
    case 2:
        commandSuffix = "freq=" + value;
        kvs["freq"] = value;
        return true;
    case 3: {
        const QString encoded = encodeMemoryText(value);
        commandSuffix = "name=" + encoded;
        kvs["name"] = encoded;
        return true;
    }
    case 4:
        commandSuffix = "mode=" + value;
        kvs["mode"] = value;
        return true;
    case 5:
        commandSuffix = "step=" + value;
        kvs["step"] = value;
        return true;
    case 6:
        commandSuffix = "repeater=" + value;
        kvs["repeater"] = value;
        return true;
    case 7:
        commandSuffix = "repeater_offset=" + value;
        kvs["repeater_offset"] = value;
        return true;
    case 8:
        commandSuffix = "tone_mode=" + value;
        kvs["tone_mode"] = value;
        return true;
    case 9:
        commandSuffix = "tone_value=" + value;
        kvs["tone_value"] = value;
        return true;
    case 10: {
        const QString squelch = item->checkState() == Qt::Checked ? "1" : "0";
        commandSuffix = "squelch=" + squelch;
        kvs["squelch"] = squelch;
        return true;
    }
    case 11:
        commandSuffix = "squelch_level=" + value;
        kvs["squelch_level"] = value;
        return true;
    case 12:
        commandSuffix = "rx_filter_low=" + value;
        kvs["rx_filter_low"] = value;
        return true;
    case 13:
        commandSuffix = "rx_filter_high=" + value;
        kvs["rx_filter_high"] = value;
        return true;
    case 14:
        commandSuffix = "rtty_mark=" + value;
        kvs["rtty_mark"] = value;
        return true;
    case 15:
        commandSuffix = "rtty_shift=" + value;
        kvs["rtty_shift"] = value;
        return true;
    case 16:
        commandSuffix = "digl_offset=" + value;
        kvs["digl_offset"] = value;
        return true;
    case 17:
        commandSuffix = "digu_offset=" + value;
        kvs["digu_offset"] = value;
        return true;
    default:
        return false;
    }
}

QString defaultExportFilePath()
{
    const QString baseName = QString("AetherSDR_Memories_%1_v%2.csv")
        .arg(QDateTime::currentDateTime().toString("MM-dd-yy_hh_mm"))
        .arg(QCoreApplication::applicationVersion());
    return QDir::home().filePath(QString("Documents/%1").arg(baseName));
}

QList<MemoryCsvRecord> currentExportRecords(const QMap<int, MemoryEntry>& memories,
                                            const QString& filterProfile)
{
    QList<MemoryCsvRecord> records;
    for (auto it = memories.cbegin(); it != memories.cend(); ++it) {
        const MemoryEntry& memory = it.value();
        if (!filterProfile.isEmpty() && memory.group != filterProfile)
            continue;
        records << MemoryCsvCompat::fromMemoryEntry(memory);
    }

    std::sort(records.begin(), records.end(),
              [](const MemoryCsvRecord& lhs, const MemoryCsvRecord& rhs) {
        if (!qFuzzyCompare(lhs.memory.freq + 1.0, rhs.memory.freq + 1.0))
            return lhs.memory.freq < rhs.memory.freq;
        return lhs.memory.index < rhs.memory.index;
    });
    return records;
}

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

    // ── Profile filter ───────────────────────────────────────────────────
    auto* filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel("Filter by Profile:"));
    m_filterCombo = new QComboBox;
    m_filterCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    rebuildFilterCombo();
    filterRow->addWidget(m_filterCombo);
    root->addLayout(filterRow);

    connect(m_filterCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { populateTable(); });

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
    auto* exportBtn = new QPushButton("Export...");
    auto* addBtn = new QPushButton("Add");
    auto* selectBtn = new QPushButton("Select");
    auto* removeBtn = new QPushButton("Remove");
    btnRow->addWidget(addBtn);
    btnRow->addWidget(selectBtn);
    btnRow->addWidget(exportBtn);
    btnRow->addStretch();
    btnRow->addWidget(removeBtn);
    root->addLayout(btnRow);

    connect(exportBtn, &QPushButton::clicked, this, &MemoryDialog::onExport);
    connect(addBtn, &QPushButton::clicked, this, &MemoryDialog::onAdd);
    connect(selectBtn, &QPushButton::clicked, this, &MemoryDialog::onSelect);
    connect(removeBtn, &QPushButton::clicked, this, &MemoryDialog::onRemove);

    // Rebuild filter combo when profile lists change
    connect(model, &RadioModel::globalProfilesChanged,
            this, &MemoryDialog::rebuildFilterCombo);
    connect(&model->transmitModel(), &TransmitModel::profileListChanged,
            this, &MemoryDialog::rebuildFilterCombo);

    // Listen for live memory updates while dialog is open
    connect(model, &RadioModel::memoryChanged,
            this, [this](int) {
        QTimer::singleShot(50, this, [this]() { populateTable(); });
    });
    connect(model, &RadioModel::memoryRemoved,
            this, [this](int) {
        populateTable();
    });
    connect(model, &RadioModel::memoriesCleared,
            this, [this]() {
        populateTable();
    });

    // Send edits to the radio when any cell changes
    connect(m_table, &QTableWidget::cellChanged, this, [this](int row, int col) {
        submitCellEdit(row, col);
    });

    // The radio doesn't support "sub memory all" or "memory list".
    // Populate from RadioModel cache (filled from status pushes during connect).
    // If cache is empty, memories may not have been pushed yet. As a fallback,
    // new memories created via Add will populate the table immediately.
    populateTable();

}

void MemoryDialog::closeEvent(QCloseEvent* event)
{
    QDialog::closeEvent(event);
}

void MemoryDialog::populateTable()
{
    const QSignalBlocker blocker(m_table);
    m_table->setSortingEnabled(false);
    m_table->setRowCount(0);
    const auto& memories = m_model->memories();
    const QString filterProfile = m_filterCombo->currentData().toString();

    for (auto it = memories.begin(); it != memories.end(); ++it) {
        const auto& m = it.value();

        // Apply profile filter: skip memories whose group doesn't match
        if (!filterProfile.isEmpty() && m.group != filterProfile) {
            continue;
        }
        int row = m_table->rowCount();
        m_table->insertRow(row);

        int col = 0;
        m_table->setItem(row, col++, new QTableWidgetItem(m.group));
        m_table->setItem(row, col++, new QTableWidgetItem(m.owner));
        // Show the full MHz value so the last 3 digits (Hz) are not lost.
        auto* freqItem = new MemoryTableItem(QString::number(m.freq, 'f', 6));
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

void MemoryDialog::submitCellEdit(int row, int col)
{
    auto* indexItem = m_table->item(row, 0);
    auto* item = m_table->item(row, col);
    if (!indexItem || !item)
        return;

    QString commandSuffix;
    QMap<QString, QString> kvs;
    if (!buildMemoryFieldUpdate(col, item, commandSuffix, kvs))
        return;

    const int memIdx = indexItem->data(Qt::UserRole).toInt();
    RadioModel* const model = m_model;
    const QPointer<MemoryDialog> dialogGuard(this);
    model->sendCmdPublic(QString("memory set %1 %2").arg(memIdx).arg(commandSuffix),
        [model, dialogGuard, memIdx, kvs](int code, const QString&) {
        if (code == 0) {
            model->handleMemoryStatus(memIdx, kvs);
            return;
        }
        if (dialogGuard)
            dialogGuard->populateTable();
    });
}

void MemoryDialog::onAdd()
{
    // memory create returns the new index in the response body.
    // We then populate it from the current active slice state.
    RadioModel* const model = m_model;
    const QPointer<MemoryDialog> dialogGuard(this);
    m_model->sendCmdPublic("memory create",
        [model, dialogGuard](int code, const QString& body) {
        if (code != 0) return;
        bool ok;
        int idx = body.trimmed().toInt(&ok);
        if (!ok) return;

        // The radio created the memory but won't push status.
        // We need to set each field from the active slice.
        const auto slices = model->slices();
        if (slices.isEmpty()) return;
        SliceModel* s = nullptr;
        for (auto* candidate : slices) {
            if (candidate && candidate->isActive()) {
                s = candidate;
                break;
            }
        }
        if (!s)
            s = slices.first();

        // Set memory fields from current slice
        auto send = [model](const QString& cmd) { model->sendCommand(cmd); };
        QString base = QString("memory set %1 ").arg(idx);
        send(base + QString("freq=%1").arg(s->frequency(), 0, 'f', 6));
        send(base + QString("mode=%1").arg(s->mode()));
        send(base + QString("owner=%1").arg(encodeMemoryText(model->callsign())));

        // Tag the memory with the active global profile name for filtering
        const QString activeProfile = model->activeGlobalProfile();
        if (!activeProfile.isEmpty()) {
            send(base + QString("group=%1").arg(encodeMemoryText(activeProfile)));
        }
        send(base + QString("step=%1").arg(s->stepHz()));
        send(base + QString("rx_filter_low=%1").arg(s->filterLow()));
        send(base + QString("rx_filter_high=%1").arg(s->filterHigh()));
        send(base + QString("squelch=%1").arg(s->squelchOn() ? 1 : 0));
        send(base + QString("squelch_level=%1").arg(s->squelchLevel()));
        send(base + QString("repeater=%1").arg(s->repeaterOffsetDir()));
        send(base + QString("repeater_offset=%1").arg(s->fmRepeaterOffsetFreq(), 0, 'f', 6));
        send(base + QString("tone_mode=%1").arg(s->fmToneMode()));
        send(base + QString("tone_value=%1").arg(s->fmToneValue()));
        send(base + QString("rtty_mark=%1").arg(s->rttyMark()));
        send(base + QString("rtty_shift=%1").arg(s->rttyShift()));
        send(base + QString("digl_offset=%1").arg(s->diglOffset()));
        send(base + QString("digu_offset=%1").arg(s->diguOffset()));

        // Create a local cache entry immediately for the table
        MemoryEntry m;
        m.index = idx;
        m.freq = s->frequency();
        m.mode = s->mode();
        m.owner = model->callsign();
        m.group = activeProfile;
        m.step = s->stepHz();
        m.rxFilterLow = s->filterLow();
        m.rxFilterHigh = s->filterHigh();
        m.squelch = s->squelchOn();
        m.squelchLevel = s->squelchLevel();
        m.offsetDir = s->repeaterOffsetDir();
        m.repeaterOffset = s->fmRepeaterOffsetFreq();
        m.toneMode = s->fmToneMode();
        m.toneValue = s->fmToneValue().toDouble();
        m.rttyMark = s->rttyMark();
        m.rttyShift = s->rttyShift();
        m.diglOffset = s->diglOffset();
        m.diguOffset = s->diguOffset();

        // Insert into RadioModel's cache via handleMemoryStatus
        QMap<QString, QString> kvs;
        kvs["freq"] = QString::number(m.freq, 'f', 6);
        kvs["mode"] = m.mode;
        kvs["owner"] = encodeMemoryText(m.owner);
        if (!activeProfile.isEmpty()) {
            kvs["group"] = encodeMemoryText(activeProfile);
        }
        kvs["step"] = QString::number(m.step);
        kvs["rx_filter_low"] = QString::number(m.rxFilterLow);
        kvs["rx_filter_high"] = QString::number(m.rxFilterHigh);
        kvs["squelch"] = m.squelch ? "1" : "0";
        kvs["squelch_level"] = QString::number(m.squelchLevel);
        kvs["repeater"] = m.offsetDir;
        kvs["repeater_offset"] = QString::number(m.repeaterOffset, 'f', 6);
        kvs["tone_mode"] = m.toneMode;
        kvs["tone_value"] = QString::number(m.toneValue, 'f', 1);
        kvs["rtty_mark"] = QString::number(m.rttyMark);
        kvs["rtty_shift"] = QString::number(m.rttyShift);
        kvs["digl_offset"] = QString::number(m.diglOffset);
        kvs["digu_offset"] = QString::number(m.diguOffset);
        model->handleMemoryStatus(idx, kvs);

        if (dialogGuard)
            dialogGuard->populateTable();
    });
}

void MemoryDialog::onExport()
{
    const QString filterProfile = m_filterCombo->currentData().toString();
    const QList<MemoryCsvRecord> records =
        currentExportRecords(m_model->memories(), filterProfile);

    if (records.isEmpty()) {
        QMessageBox::information(this, "Export Memories",
                                 filterProfile.isEmpty()
                                     ? "There are no memories to export."
                                     : "There are no memories in the current filter to export.");
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this,
        "Export Memories",
        defaultExportFilePath(),
        "CSV Files (*.csv)");
    if (path.isEmpty())
        return;

    const QByteArray csv = MemoryCsvCompat::serialize(records);
    const MemoryCsvParseResult validation = MemoryCsvCompat::parse(csv);
    if (!validation.ok()) {
        QMessageBox::warning(this, "Export Memories",
                             QString("The generated SmartSDR CSV failed validation:\n%1")
                                 .arg(validation.errors.join('\n')));
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, "Export Memories",
                             QString("Couldn't open %1 for writing.")
                                 .arg(QDir::toNativeSeparators(path)));
        return;
    }

    if (file.write(csv) != csv.size()) {
        QMessageBox::warning(this, "Export Memories",
                             QString("Couldn't write the SmartSDR CSV to %1.")
                                 .arg(QDir::toNativeSeparators(path)));
        return;
    }

    if (!file.commit()) {
        QMessageBox::warning(this, "Export Memories",
                             QString("Couldn't save %1.")
                                 .arg(QDir::toNativeSeparators(path)));
        return;
    }

    QMessageBox::information(this, "Export Memories",
                             QString("Exported %1 memories to %2.")
                                 .arg(records.size())
                                 .arg(QDir::toNativeSeparators(QFileInfo(path).fileName())));
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
    RadioModel* const model = m_model;
    const QPointer<MemoryDialog> dialogGuard(this);
    m_model->sendCmdPublic(QString("memory remove %1").arg(idx),
        [model, dialogGuard, idx](int code, const QString&) {
        if (code == 0) {
            QMap<QString, QString> kvs;
            kvs["removed"] = QString{};
            model->handleMemoryStatus(idx, kvs);
            return;
        }
        if (dialogGuard)
            dialogGuard->populateTable();
    });
}

void MemoryDialog::rebuildFilterCombo()
{
    const QSignalBlocker blocker(m_filterCombo);
    const QString previous = m_filterCombo->currentData().toString();
    m_filterCombo->clear();

    // "All" shows every memory regardless of group
    m_filterCombo->addItem("All Memories", QString());

    // Collect unique profile names from global and transmit profiles
    QStringList profileNames;
    for (const QString& p : m_model->globalProfiles()) {
        if (!profileNames.contains(p)) {
            profileNames.append(p);
        }
    }
    for (const QString& p : m_model->transmitModel().profileList()) {
        if (!profileNames.contains(p)) {
            profileNames.append(p);
        }
    }
    profileNames.sort(Qt::CaseInsensitive);

    for (const QString& name : profileNames) {
        m_filterCombo->addItem(name, name);
    }

    // Restore previous selection if still present
    int idx = m_filterCombo->findData(previous);
    if (idx >= 0) {
        m_filterCombo->setCurrentIndex(idx);
    }
}

} // namespace AetherSDR
