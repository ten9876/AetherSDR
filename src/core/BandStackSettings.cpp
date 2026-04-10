#include "BandStackSettings.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDebug>

namespace AetherSDR {

BandStackSettings::BandStackSettings()
{
    m_filePath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                 + "/AetherSDR/BandStack.settings";
}

BandStackSettings& BandStackSettings::instance()
{
    static BandStackSettings inst;
    return inst;
}

QString BandStackSettings::sanitizeSerial(const QString& serial)
{
    // XML element names: ^[A-Za-z_][A-Za-z0-9_]*$
    QString s = serial;
    s.replace('-', '_');
    s.replace(' ', '_');
    return "Radio_" + s;
}

QVector<BandStackEntry> BandStackSettings::entries(const QString& radioSerial) const
{
    return m_entries.value(sanitizeSerial(radioSerial));
}

void BandStackSettings::addEntry(const QString& radioSerial, const BandStackEntry& entry)
{
    m_entries[sanitizeSerial(radioSerial)].append(entry);
}

void BandStackSettings::removeEntry(const QString& radioSerial, int index)
{
    QString key = sanitizeSerial(radioSerial);
    if (!m_entries.contains(key)) return;
    auto& vec = m_entries[key];
    if (index >= 0 && index < vec.size()) {
        vec.removeAt(index);
    }
}

void BandStackSettings::load()
{
    m_entries.clear();

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;  // no file yet — first run
    }

    QXmlStreamReader xml(&file);

    // Structure: <BandStack> <Radio_XXXX> <Entry_N> <FrequencyMhz>... </Entry_N> </Radio_XXXX> </BandStack>
    QString currentRadio;
    BandStackEntry currentEntry;
    bool inEntry = false;

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            const QString name = xml.name().toString();

            if (name == "BandStack") {
                continue;
            } else if (name.startsWith("Radio_")) {
                currentRadio = name;
            } else if (name.startsWith("Entry_") && !currentRadio.isEmpty()) {
                inEntry = true;
                currentEntry = BandStackEntry{};
            } else if (inEntry) {
                // Read field value
                QString text = xml.readElementText();
                if (name == "FrequencyMhz") {
                    currentEntry.frequencyMhz = text.toDouble();
                } else if (name == "Mode") {
                    currentEntry.mode = text;
                } else if (name == "FilterLow") {
                    currentEntry.filterLow = text.toInt();
                } else if (name == "FilterHigh") {
                    currentEntry.filterHigh = text.toInt();
                } else if (name == "RxAntenna") {
                    currentEntry.rxAntenna = text;
                } else if (name == "TxAntenna") {
                    currentEntry.txAntenna = text;
                } else if (name == "AgcMode") {
                    currentEntry.agcMode = text;
                } else if (name == "AgcThreshold") {
                    currentEntry.agcThreshold = text.toInt();
                } else if (name == "AudioGain") {
                    currentEntry.audioGain = text.toInt();
                } else if (name == "NbOn") {
                    currentEntry.nbOn = text == "True";
                } else if (name == "NbLevel") {
                    currentEntry.nbLevel = text.toInt();
                } else if (name == "NrOn") {
                    currentEntry.nrOn = text == "True";
                } else if (name == "NrLevel") {
                    currentEntry.nrLevel = text.toInt();
                } else if (name == "WnbOn") {
                    currentEntry.wnbOn = text == "True";
                } else if (name == "WnbLevel") {
                    currentEntry.wnbLevel = text.toInt();
                }
            }
        } else if (xml.isEndElement()) {
            const QString name = xml.name().toString();
            if (name.startsWith("Entry_") && inEntry) {
                m_entries[currentRadio].append(currentEntry);
                inEntry = false;
            } else if (name.startsWith("Radio_")) {
                currentRadio.clear();
            }
        }
    }

    if (xml.hasError()) {
        qWarning() << "BandStackSettings: XML parse error:" << xml.errorString();
    }
}

void BandStackSettings::save()
{
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());

    const QString tmpPath = m_filePath + ".tmp";
    QFile file(tmpPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "BandStackSettings: cannot write" << tmpPath;
        return;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(2);
    xml.writeStartDocument();
    xml.writeStartElement("BandStack");

    QList<QString> radios = m_entries.keys();
    std::sort(radios.begin(), radios.end());

    for (const QString& radio : radios) {
        const auto& vec = m_entries[radio];
        if (vec.isEmpty()) continue;

        xml.writeStartElement(radio);
        for (int i = 0; i < vec.size(); ++i) {
            const BandStackEntry& e = vec[i];
            xml.writeStartElement(QString("Entry_%1").arg(i));
            xml.writeTextElement("FrequencyMhz", QString::number(e.frequencyMhz, 'f', 6));
            xml.writeTextElement("Mode", e.mode);
            xml.writeTextElement("FilterLow", QString::number(e.filterLow));
            xml.writeTextElement("FilterHigh", QString::number(e.filterHigh));
            xml.writeTextElement("RxAntenna", e.rxAntenna);
            xml.writeTextElement("TxAntenna", e.txAntenna);
            xml.writeTextElement("AgcMode", e.agcMode);
            xml.writeTextElement("AgcThreshold", QString::number(e.agcThreshold));
            xml.writeTextElement("AudioGain", QString::number(e.audioGain));
            xml.writeTextElement("NbOn", e.nbOn ? "True" : "False");
            xml.writeTextElement("NbLevel", QString::number(e.nbLevel));
            xml.writeTextElement("NrOn", e.nrOn ? "True" : "False");
            xml.writeTextElement("NrLevel", QString::number(e.nrLevel));
            xml.writeTextElement("WnbOn", e.wnbOn ? "True" : "False");
            xml.writeTextElement("WnbLevel", QString::number(e.wnbLevel));
            xml.writeEndElement();  // Entry_N
        }
        xml.writeEndElement();  // Radio_XXXX
    }

    xml.writeEndElement();  // BandStack
    xml.writeEndDocument();
    file.close();

    // Atomic rename
    QFile::remove(m_filePath);
    QFile::rename(tmpPath, m_filePath);
}

} // namespace AetherSDR
