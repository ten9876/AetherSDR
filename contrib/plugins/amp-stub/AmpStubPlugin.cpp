#include "AmpStubPlugin.h"
#include <QDebug>

bool AmpStubPlugin::initialize(const QString& configPath)
{
    qDebug() << "[AmpStub] initialize, config dir:" << configPath;
    // A real plugin would open its serial/USB/TCP connection here
    // and read settings from configPath.
    return true;
}

void AmpStubPlugin::onFrequencyChanged(double hz, const QString& mode, int sliceIdx)
{
    qDebug() << "[AmpStub] frequency" << hz / 1e6 << "MHz, mode:" << mode
             << "slice:" << sliceIdx;

    if (m_statusLabel)
        m_statusLabel->setText(QString("Freq: %1 MHz  Mode: %2")
            .arg(hz / 1e6, 0, 'f', 4).arg(mode));

    // A real plugin would send a band-switch command to the amplifier here.
}

void AmpStubPlugin::onPttChanged(bool transmitting)
{
    qDebug() << "[AmpStub] PTT" << (transmitting ? "ON" : "OFF");

    if (m_statusLabel) {
        const QString base = m_statusLabel->text().section("  PTT:", 0, 0);
        m_statusLabel->setText(base + (transmitting ? "  PTT: TX" : "  PTT: RX"));
    }
}

QWidget* AmpStubPlugin::createWidget(QWidget* parent)
{
    auto* container = new QWidget(parent);
    auto* layout    = new QVBoxLayout(container);
    layout->setContentsMargins(6, 6, 6, 6);

    auto* title = new QLabel("Stub Amp Plugin", container);
    title->setStyleSheet("QLabel { color: #8aa8c0; font-size: 11px; font-weight: bold; }");
    layout->addWidget(title);

    m_statusLabel = new QLabel("Waiting for frequency event...", container);
    m_statusLabel->setStyleSheet("QLabel { color: #c8d8e8; font-size: 10px; }");
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);

    auto* hint = new QLabel(
        "This is a developer stub plugin.\n"
        "Replace this widget with real amplifier controls.", container);
    hint->setStyleSheet("QLabel { color: #607080; font-size: 10px; }");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch();

    return container;
}
