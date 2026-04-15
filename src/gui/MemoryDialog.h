#pragma once

#include <QDialog>
#include <QCloseEvent>
#include <QEvent>
#include <QShowEvent>
#include <QTableWidget>
#include <QMap>

class QComboBox;
class QLineEdit;

namespace AetherSDR {

class RadioModel;

class MemoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit MemoryDialog(RadioModel* model, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void activateMemoryRow(int row);
    void populateTable();
    void submitCellEdit(int row, int col);
    void onAdd();
    void onSelect();
    void onRemove();
    bool isSortableColumn(int column) const;
    void rebuildFilterCombo();

    RadioModel* m_model;
    QTableWidget* m_table;
    QLineEdit* m_searchEdit;
    QComboBox* m_filterCombo;
    int m_sortColumn{2};
    Qt::SortOrder m_sortOrder{Qt::AscendingOrder};
};

} // namespace AetherSDR
