#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;

namespace AetherSDR {

class HelpDialog : public QDialog {
    Q_OBJECT

public:
    explicit HelpDialog(const QString& windowTitle,
                        const QString& resourcePath,
                        QWidget* parent = nullptr);

private:
    void buildUI(const QString& resourcePath);
    void focusFindField();
    void findNext();
    QString loadMarkdown(const QString& resourcePath) const;
    void resetFindState();
    void updateFindFeedback(const QString& message, bool noMatch);

    QTextBrowser* m_browser{nullptr};
    QLineEdit* m_findEdit{nullptr};
    QPushButton* m_findButton{nullptr};
    QLabel* m_findStatus{nullptr};
};

} // namespace AetherSDR
