#include "HelpDialog.h"

#include <QDialogButtonBox>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace AetherSDR {

HelpDialog::HelpDialog(const QString& windowTitle,
                       const QString& resourcePath,
                       QWidget* parent)
    : QDialog(parent)
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowTitle(windowTitle);
    buildUI(resourcePath);
}

void HelpDialog::buildUI(const QString& resourcePath)
{
    resize(760, 680);
    setMinimumSize(520, 420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* header = new QWidget(this);
    header->setStyleSheet("background: #0a0a14;");
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(20, 18, 20, 14);
    headerLayout->setSpacing(6);

    auto* eyebrow = new QLabel("AETHERSDR OFFLINE HELP", header);
    eyebrow->setStyleSheet("color: #00b4d8; font-size: 11px; letter-spacing: 2px;");
    headerLayout->addWidget(eyebrow);

    auto* title = new QLabel(windowTitle(), header);
    title->setWordWrap(true);
    title->setStyleSheet("color: #c8d8e8; font-size: 22px; font-weight: 700;");
    headerLayout->addWidget(title);

    auto* subtitle = new QLabel(
        "Bundled help is available even when your station computer is offline.",
        header);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet("color: #8aa8c0; font-size: 12px;");
    headerLayout->addWidget(subtitle);

    layout->addWidget(header);

    auto* separator = new QWidget(this);
    separator->setFixedHeight(1);
    separator->setStyleSheet("background: #203040;");
    layout->addWidget(separator);

    auto* browser = new QTextBrowser(this);
    browser->setReadOnly(true);
    browser->setOpenExternalLinks(true);
    browser->setOpenLinks(true);
    browser->document()->setDocumentMargin(18);
    browser->document()->setDefaultStyleSheet(
        "body { color: #c8d8e8; font-size: 12px; font-family: sans-serif; }"
        "h1 { color: #00b4d8; font-size: 22px; font-weight: 700; margin: 0 0 16px 0; }"
        "h2 { color: #c8d8e8; font-size: 18px; font-weight: 700; margin: 18px 0 8px 0; }"
        "h3 { color: #8898a8; font-size: 14px; font-weight: 700; margin: 14px 0 6px 0; }"
        "h4 { color: #8898a8; font-size: 12px; font-weight: 700; margin: 12px 0 4px 0; }"
        "p { color: #c8d8e8; font-size: 12px; line-height: 1.5; margin: 0 0 10px 0; }"
        "ul, ol { margin: 0 0 12px 20px; }"
        "li { color: #c8d8e8; font-size: 12px; line-height: 1.55; margin: 3px 0; }"
        "ol li::marker { color: #00b4d8; font-weight: 700; }"
        "ul li::marker { color: #40c060; }"
        "strong { color: #dfeaf3; font-weight: 700; }"
        "em { color: #8898a8; font-style: italic; }"
        "code { color: #d8e4ee; background-color: #152230; }"
        "a { color: #00b4d8; text-decoration: none; }");
    browser->setMarkdown(loadMarkdown(resourcePath));
    browser->setStyleSheet(
        "QTextBrowser {"
        "  background: #0f0f1a;"
        "  color: #d8e4ee;"
        "  border: none;"
        "  padding: 10px;"
        "  selection-background-color: #245a7a;"
        "  font-size: 14px;"
        "  line-height: 1.5;"
        "}"
        "QScrollBar:vertical {"
        "  background: #0a0a14;"
        "  width: 10px;"
        "  margin: 8px 2px 8px 2px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #304050;"
        "  min-height: 28px;"
        "  border-radius: 5px;"
        "}"
        "QScrollBar::handle:vertical:hover {"
        "  background: #3f5870;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0px;"
        "}"
        "QScrollBar:horizontal {"
        "  background: #0a0a14;"
        "  height: 10px;"
        "  margin: 2px 8px 2px 8px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: #304050;"
        "  min-width: 28px;"
        "  border-radius: 5px;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "  width: 0px;"
        "}");
    layout->addWidget(browser, 1);

    auto* footer = new QWidget(this);
    footer->setStyleSheet("background: #0a0a14;");
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 12, 16, 16);

    auto* hint = new QLabel(
        "Tip: The Help menu keeps each guide separate so you can reopen just the topic you need.",
        footer);
    hint->setWordWrap(true);
    hint->setStyleSheet("color: #7f93a7; font-size: 11px;");
    footerLayout->addWidget(hint, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, footer);
    auto* closeButton = buttons->button(QDialogButtonBox::Close);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setStyleSheet(
        "QPushButton {"
        "  background: #00b4d8;"
        "  color: #0f0f1a;"
        "  font-weight: 700;"
        "  border: none;"
        "  border-radius: 16px;"
        "  min-width: 96px;"
        "  min-height: 32px;"
        "  padding: 0 18px;"
        "}"
        "QPushButton:hover { background: #18c8ea; }");
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    footerLayout->addWidget(buttons, 0, Qt::AlignRight);

    layout->addWidget(footer);

    setStyleSheet("HelpDialog { background: #0f0f1a; }");
}

QString HelpDialog::loadMarkdown(const QString& resourcePath) const
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QStringLiteral(
            "# Help file not available\n\n"
            "AetherSDR could not open this bundled help topic.\n\n"
            "Please reinstall the application or report the missing help asset.");
    }

    return QString::fromUtf8(file.readAll());
}

} // namespace AetherSDR
