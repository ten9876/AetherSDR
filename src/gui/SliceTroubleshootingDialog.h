#pragma once

#include <QDialog>
#include <QJsonObject>

class QLabel;
class QPlainTextEdit;

namespace AetherSDR {

class RadioModel;

class SliceTroubleshootingDialog : public QDialog {
    Q_OBJECT

public:
    explicit SliceTroubleshootingDialog(RadioModel* model, QWidget* parent = nullptr);

private:
    void refreshSnapshot();
    void copySummary();
    void copyJson();
    void exportJson();
    void setStatusMessage(const QString& message);

    static QString buildSummary(const QJsonObject& snapshot);

    RadioModel* m_model{nullptr};
    QJsonObject m_snapshot;
    QString m_summaryText;
    QString m_jsonText;

    QPlainTextEdit* m_summaryView{nullptr};
    QPlainTextEdit* m_jsonView{nullptr};
    QLabel* m_statusLabel{nullptr};
};

} // namespace AetherSDR
