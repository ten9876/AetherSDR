#include "core/MidiSettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <iostream>

using namespace AetherSDR;

namespace {

bool expect(bool condition, const char* label)
{
    std::cout << (condition ? "[ OK ] " : "[FAIL] ") << label << '\n';
    return condition;
}

bool sameBinding(const MidiBinding& a, const MidiBinding& b)
{
    return a.channel == b.channel
        && a.msgType == b.msgType
        && a.number == b.number
        && a.paramId == b.paramId
        && a.inverted == b.inverted
        && a.relative == b.relative;
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir fakeHome(QDir::tempPath() + "/aether-midi-settings-test-XXXXXX");
    if (!fakeHome.isValid()) {
        std::cerr << "[FAIL] create temporary home\n";
        return 1;
    }
    qputenv("HOME", fakeHome.path().toUtf8());
    qputenv("CFFIXED_USER_HOME", fakeHome.path().toUtf8());
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);

    const QString configRoot =
        QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QDir(configRoot + "/AetherSDR").removeRecursively();

    MidiBinding afGain;
    afGain.channel = 2;
    afGain.msgType = MidiBinding::CC;
    afGain.number = 74;
    afGain.paramId = "rx.afGain";
    afGain.inverted = true;
    afGain.relative = true;

    MidiBinding tuneKnob;
    tuneKnob.channel = -1;
    tuneKnob.msgType = MidiBinding::PitchBend;
    tuneKnob.number = -1;
    tuneKnob.paramId = "rx.tuneKnob";

    QVector<MidiBinding> saved{afGain, tuneKnob};

    auto& settings = MidiSettings::instance();
    settings.setLastDevice("Initial Controller");
    settings.setAutoConnect(true);
    settings.saveBindings(saved);

    settings.setLastDevice("Renamed Controller");
    settings.setAutoConnect(false);
    settings.save();

    const auto loaded = settings.loadBindings();
    bool ok = true;
    ok &= expect(loaded.size() == saved.size(),
                 "preference-only save preserves binding count");
    if (loaded.size() == saved.size()) {
        ok &= expect(sameBinding(loaded[0], saved[0]),
                     "preference-only save preserves first binding");
        ok &= expect(sameBinding(loaded[1], saved[1]),
                     "preference-only save preserves second binding");
    }

    settings.load();
    ok &= expect(settings.lastDevice() == "Renamed Controller",
                 "preference-only save updates last device");
    ok &= expect(!settings.autoConnect(),
                 "preference-only save updates auto-connect");

    QFile::remove(configRoot + "/AetherSDR/midi.settings");
    QDir(configRoot + "/AetherSDR").removeRecursively();

    return ok ? 0 : 1;
}
