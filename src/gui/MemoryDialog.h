#pragma once

#include <QDialog>
#include <QCloseEvent>
#include <QTableWidget>
#include <QMap>

namespace AetherSDR {

class RadioModel;

class MemoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit MemoryDialog(RadioModel* model, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void populateTable();
    void submitCellEdit(int row, int col);
    void onAdd();
    void onSelect();
    void onRemove();
    bool isSortableColumn(int column) const;

    RadioModel* m_model;
    QTableWidget* m_table;
    int m_sortColumn{2};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};
};

} // namespace AetherSDR
