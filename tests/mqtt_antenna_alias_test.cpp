// MQTT antenna-alias parser + radio-gating tests (#2880).
//
// Drives the pure parser directly with synthetic (topic, payload) pairs so
// no live broker is required. The "MQTT aliases do not apply while no radio
// is connected" acceptance criterion is exercised by replaying the same
// MainWindow slot logic against a stub radio model — kept here because that
// guard is the one place MQTT input meets the alias store.

#include "core/AppSettings.h"
#include "core/MqttAntennaAliasParser.h"
#include "models/AntennaAliasStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QMap>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <iostream>

using namespace AetherSDR;

namespace {

bool expect(bool condition, const char* label)
{
    std::cout << (condition ? "[ OK ] " : "[FAIL] ") << label << '\n';
    return condition;
}

// Tiny stand-in for the MainWindow slot that wires MqttApplet ->
// RadioModel::setAntennaAlias. Exists only so the test can assert the
// "no radio connected ⇒ drop" guard without instantiating MainWindow.
struct StubRadio {
    bool connected = false;
    QStringList known;
    QString radioKey;

    void applyAlias(const QString& token, const QString& alias) {
        if (!connected)
            return;
        if (!known.contains(token))
            return;
        auto aliases = AntennaAliasStore::load(radioKey);
        if (alias.trimmed().isEmpty()) {
            aliases.remove(token);
        } else {
            aliases.insert(token, alias.trimmed());
        }
        AntennaAliasStore::save(radioKey, aliases);
    }
};

void deliver(StubRadio& radio, const QString& topic, const QByteArray& payload)
{
    for (const auto& upd : MqttAntennaAliasParser::parse(topic, payload))
        radio.applyAlias(upd.token, upd.alias);
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir fakeHome(QDir::tempPath() + "/aether-mqtt-antenna-alias-test-XXXXXX");
    if (!fakeHome.isValid()) {
        std::cerr << "[FAIL] create temporary home\n";
        return 1;
    }
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("CFFIXED_USER_HOME", fakeHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);

    const QString configRoot =
        QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    QDir(configRoot + "/AetherSDR").removeRecursively();

    auto& settings = AppSettings::instance();
    settings.reset();

    bool ok = true;

    // ── Pure parser: per-port set ────────────────────────────────────────
    {
        const auto updates = MqttAntennaAliasParser::parse(
            QStringLiteral("aethersdr/antenna/name/ANT1"),
            QByteArrayLiteral("80m Dipole"));
        ok &= expect(updates.size() == 1
                     && updates[0].token == QStringLiteral("ANT1")
                     && updates[0].alias == QStringLiteral("80m Dipole"),
                     "per-port topic parses (token, alias)");
    }

    // ── Pure parser: per-port empty payload clears ───────────────────────
    {
        const auto updates = MqttAntennaAliasParser::parse(
            QStringLiteral("aethersdr/antenna/name/ANT2"),
            QByteArray());
        ok &= expect(updates.size() == 1
                     && updates[0].token == QStringLiteral("ANT2")
                     && updates[0].alias.isEmpty(),
                     "per-port empty payload yields clear");
    }

    // ── Pure parser: bulk JSON with set + null-clear + empty-clear ───────
    {
        const auto updates = MqttAntennaAliasParser::parse(
            QStringLiteral("aethersdr/antenna/names"),
            QByteArrayLiteral(R"({"ANT1":"80m Dipole","ANT2":null,"RX_A":""})"));
        QMap<QString, QString> seen;
        for (const auto& u : updates)
            seen.insert(u.token, u.alias);
        ok &= expect(seen.size() == 3, "bulk JSON yields three updates");
        ok &= expect(seen.value(QStringLiteral("ANT1")) == QStringLiteral("80m Dipole"),
                     "bulk JSON: string value sets alias");
        ok &= expect(seen.contains(QStringLiteral("ANT2"))
                     && seen.value(QStringLiteral("ANT2")).isEmpty(),
                     "bulk JSON: null value clears alias");
        ok &= expect(seen.contains(QStringLiteral("RX_A"))
                     && seen.value(QStringLiteral("RX_A")).isEmpty(),
                     "bulk JSON: empty string clears alias");
    }

    // ── Pure parser: malformed JSON yields nothing ───────────────────────
    {
        const auto updates = MqttAntennaAliasParser::parse(
            QStringLiteral("aethersdr/antenna/names"),
            QByteArrayLiteral("not json"));
        ok &= expect(updates.isEmpty(),
                     "malformed bulk JSON is ignored");
    }

    // ── Pure parser: unrelated topic yields nothing ──────────────────────
    {
        const auto updates = MqttAntennaAliasParser::parse(
            QStringLiteral("station/rotator/pos"),
            QByteArrayLiteral("12.0"));
        ok &= expect(updates.isEmpty(),
                     "unrelated topic is ignored");
    }

    // ── Pure parser: per-port with empty token (trailing slash) ──────────
    {
        const auto updates = MqttAntennaAliasParser::parse(
            QStringLiteral("aethersdr/antenna/name/"),
            QByteArrayLiteral("ignored"));
        ok &= expect(updates.isEmpty(),
                     "per-port topic with empty token is ignored");
    }

    // ── Full chain: connected radio applies and clears aliases ───────────
    {
        StubRadio radio;
        radio.radioKey = QStringLiteral("serial-mqtt-1");
        radio.connected = true;
        radio.known = QStringList({QStringLiteral("ANT1"),
                                    QStringLiteral("ANT2"),
                                    QStringLiteral("RX_A")});
        QDir(configRoot + "/AetherSDR").removeRecursively();
        settings.reset();

        // Per-port set
        deliver(radio,
                QStringLiteral("aethersdr/antenna/name/ANT1"),
                QByteArrayLiteral("80m Dipole"));
        auto aliases = AntennaAliasStore::load(radio.radioKey);
        ok &= expect(aliases.value(QStringLiteral("ANT1")) == QStringLiteral("80m Dipole"),
                     "per-port topic sets alias when radio connected");

        // Per-port clear (empty payload)
        deliver(radio,
                QStringLiteral("aethersdr/antenna/name/ANT1"),
                QByteArray());
        aliases = AntennaAliasStore::load(radio.radioKey);
        ok &= expect(!aliases.contains(QStringLiteral("ANT1")),
                     "per-port empty payload clears alias");

        // Bulk JSON: mix of set + null clear + "" clear
        // Seed an alias to be cleared
        deliver(radio,
                QStringLiteral("aethersdr/antenna/name/RX_A"),
                QByteArrayLiteral("Beverage NE"));
        deliver(radio,
                QStringLiteral("aethersdr/antenna/names"),
                QByteArrayLiteral(R"({"ANT1":"80m Dipole","ANT2":"Hexbeam","RX_A":null})"));
        aliases = AntennaAliasStore::load(radio.radioKey);
        ok &= expect(aliases.value(QStringLiteral("ANT1")) == QStringLiteral("80m Dipole"),
                     "bulk JSON sets ANT1");
        ok &= expect(aliases.value(QStringLiteral("ANT2")) == QStringLiteral("Hexbeam"),
                     "bulk JSON sets ANT2");
        ok &= expect(!aliases.contains(QStringLiteral("RX_A")),
                     "bulk JSON null clears RX_A");

        // Invalid token (not in knownAntennaTokens) is dropped
        deliver(radio,
                QStringLiteral("aethersdr/antenna/name/ROGUE"),
                QByteArrayLiteral("Pwn"));
        aliases = AntennaAliasStore::load(radio.radioKey);
        ok &= expect(!aliases.contains(QStringLiteral("ROGUE")),
                     "unknown token is dropped");

        // Bulk JSON with a mix of valid and invalid tokens — invalid is dropped,
        // valid is applied
        deliver(radio,
                QStringLiteral("aethersdr/antenna/names"),
                QByteArrayLiteral(R"({"ROGUE":"Pwn","ANT2":"Updated"})"));
        aliases = AntennaAliasStore::load(radio.radioKey);
        ok &= expect(!aliases.contains(QStringLiteral("ROGUE")),
                     "bulk JSON: unknown token dropped");
        ok &= expect(aliases.value(QStringLiteral("ANT2")) == QStringLiteral("Updated"),
                     "bulk JSON: valid token applied alongside unknown");
    }

    // ── Disconnected radio: messages drop, nothing queued ────────────────
    {
        StubRadio radio;
        radio.radioKey = QStringLiteral("serial-mqtt-2");
        radio.connected = false;
        radio.known = QStringList({QStringLiteral("ANT1"), QStringLiteral("ANT2")});
        QDir(configRoot + "/AetherSDR").removeRecursively();
        settings.reset();

        deliver(radio,
                QStringLiteral("aethersdr/antenna/name/ANT1"),
                QByteArrayLiteral("Should Not Apply"));
        deliver(radio,
                QStringLiteral("aethersdr/antenna/names"),
                QByteArrayLiteral(R"({"ANT2":"Also Not Applied"})"));

        auto aliases = AntennaAliasStore::load(radio.radioKey);
        ok &= expect(aliases.isEmpty(),
                     "messages while disconnected are not stored");

        // Now connect — earlier messages must not have been queued
        radio.connected = true;
        aliases = AntennaAliasStore::load(radio.radioKey);
        ok &= expect(aliases.isEmpty(),
                     "queued aliases do not leak to a later radio");

        // New message after connect should apply normally
        deliver(radio,
                QStringLiteral("aethersdr/antenna/name/ANT1"),
                QByteArrayLiteral("Now Applied"));
        aliases = AntennaAliasStore::load(radio.radioKey);
        ok &= expect(aliases.value(QStringLiteral("ANT1")) == QStringLiteral("Now Applied"),
                     "post-connect messages apply normally");
    }

    QDir(configRoot + "/AetherSDR").removeRecursively();
    return ok ? 0 : 1;
}
