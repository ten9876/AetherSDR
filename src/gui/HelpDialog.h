#pragma once

#include <QDialog>

class QString;

namespace AetherSDR {

class HelpDialog : public QDialog {
    Q_OBJECT

public:
    explicit HelpDialog(const QString& windowTitle,
                        const QString& resourcePath,
                        QWidget* parent = nullptr);

private:
    void buildUI(const QString& resourcePath);
    QString loadMarkdown(const QString& resourcePath) const;
};

} // namespace AetherSDR
