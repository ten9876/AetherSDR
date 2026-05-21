#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace AetherSDR {

// Pure parser for the fixed AetherSDR MQTT antenna-alias topic contract
// (#2880). Kept free of Qt UI types so unit tests can drive it without a
// live broker and without linking the GUI / MqttClient.
namespace MqttAntennaAliasParser {

extern const QString kNamesTopic;     // bulk JSON: aethersdr/antenna/names
extern const QString kNamePrefix;     // per-port:  aethersdr/antenna/name/<TOKEN>

struct AliasUpdate {
    QString token;
    QString alias;  // empty ⇒ clear that local name
};

// Returns one update per (token, alias) pair found in (topic, payload).
// Per-port topic empty payload → one update with empty alias (clear).
// Bulk JSON null or "" value → update with empty alias (clear).
// Malformed JSON or topic outside the contract → empty vector.
QVector<AliasUpdate> parse(const QString& topic, const QByteArray& payload);

} // namespace MqttAntennaAliasParser

} // namespace AetherSDR
