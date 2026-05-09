// CwxModel queue-drain signal tests.
// Covers the queueEmpty path that wait_morse and the #2450 TX-release fix
// both depend on.
// Run: ./build/cwx_model_queue_test

#include "models/CwxModel.h"

#include <QCoreApplication>
#include <QMap>
#include <QStringList>
#include <cstdio>

using namespace AetherSDR;

namespace {

int g_failed = 0;

void check(bool ok, const char* name)
{
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", name);
    if (!ok) ++g_failed;
}

// ── queueEmpty signal ────────────────────────────────────────────────────────

void testQueueEmptyOnEmptyValue()
{
    CwxModel model;
    int fired = 0;
    QObject::connect(&model, &CwxModel::queueEmpty, [&]() { ++fired; });

    model.applyStatus({{"queue", ""}});
    check(fired == 1, "queueEmpty fires when queue= has empty value");
}

void testQueueEmptyOnZeroValue()
{
    CwxModel model;
    int fired = 0;
    QObject::connect(&model, &CwxModel::queueEmpty, [&]() { ++fired; });

    model.applyStatus({{"queue", "0"}});
    check(fired == 1, "queueEmpty fires when queue=0");
}

void testQueueEmptyNotFiredOnNonEmptyQueue()
{
    CwxModel model;
    int fired = 0;
    QObject::connect(&model, &CwxModel::queueEmpty, [&]() { ++fired; });

    model.applyStatus({{"queue", "3"}});
    check(fired == 0, "queueEmpty does NOT fire when queue=3 (items pending)");
}

void testQueueEmptyNotFiredOnNonQueueKey()
{
    CwxModel model;
    int fired = 0;
    QObject::connect(&model, &CwxModel::queueEmpty, [&]() { ++fired; });

    model.applyStatus({{"wpm", "20"}, {"sent", "5"}});
    check(fired == 0, "queueEmpty does NOT fire on unrelated status keys");
}

void testQueueEmptyFiresOnlyForEmptyAmongMixedKeys()
{
    CwxModel model;
    int fired = 0;
    QObject::connect(&model, &CwxModel::queueEmpty, [&]() { ++fired; });

    model.applyStatus({{"wpm", "20"}, {"queue", ""}, {"sent", "2"}});
    check(fired == 1, "queueEmpty fires exactly once in a mixed-key applyStatus");
}

void testQueueEmptyMultipleUpdates()
{
    CwxModel model;
    int fired = 0;
    QObject::connect(&model, &CwxModel::queueEmpty, [&]() { ++fired; });

    model.applyStatus({{"queue", ""}});
    model.applyStatus({{"queue", "0"}});
    check(fired == 2, "queueEmpty fires independently on each drain status");
}

// ── send() / block counter ───────────────────────────────────────────────────

void testSendEmitsCommandReady()
{
    CwxModel model;
    QStringList cmds;
    QObject::connect(&model, &CwxModel::commandReady,
                     [&](const QString& cmd) { cmds << cmd; });

    model.send("CQ");
    check(cmds.size() == 1, "send() emits exactly one commandReady");
    check(cmds[0].startsWith("cwx send"), "commandReady starts with 'cwx send'");
    check(cmds[0].contains("\"CQ\""), "commandReady includes the CW text");
}

void testSendBlockCounterIncrements()
{
    CwxModel model;
    QStringList cmds;
    QObject::connect(&model, &CwxModel::commandReady,
                     [&](const QString& cmd) { cmds << cmd; });

    model.send("DE");
    model.send("W1AW");
    check(cmds.size() == 2, "two sends emit two commands");
    // Command format: "cwx send \"TEXT\" N" — block number is the last token
    int n0 = cmds[0].split(' ').last().toInt();
    int n1 = cmds[1].split(' ').last().toInt();
    check(n0 >= 1,         "first block number is >= 1");
    check(n1 == n0 + 1,    "block counter increments by 1 per send()");
}

void testSendEmptyIsNoop()
{
    CwxModel model;
    int cmdCount = 0;
    QObject::connect(&model, &CwxModel::commandReady,
                     [&](const QString&) { ++cmdCount; });

    model.send("");
    check(cmdCount == 0, "send(\"\") emits no commandReady");
}

void testSendSpaceEncodedAsDel()
{
    CwxModel model;
    QStringList cmds;
    QObject::connect(&model, &CwxModel::commandReady,
                     [&](const QString& cmd) { cmds << cmd; });

    model.send("CQ CQ");
    check(!cmds.isEmpty(), "send with space emits command");
    // Command format: cwx send "CQ\x7fCQ" N — check the quoted payload only
    int qStart = cmds[0].indexOf('"');
    int qEnd   = cmds[0].lastIndexOf('"');
    const QString encoded = (qStart >= 0 && qEnd > qStart)
        ? cmds[0].mid(qStart + 1, qEnd - qStart - 1) : QString{};
    check(encoded.contains(QChar(0x7f)), "space encoded as DEL (0x7f) in payload");
    check(!encoded.contains(' '),        "no bare space in quoted payload");
}

// ── clearBuffer ──────────────────────────────────────────────────────────────

void testClearBufferEmitsCommandAndCancellation()
{
    CwxModel model;
    QStringList cmds;
    int cancelled = 0;
    QObject::connect(&model, &CwxModel::commandReady,
                     [&](const QString& cmd) { cmds << cmd; });
    QObject::connect(&model, &CwxModel::transmissionCancelled,
                     [&]() { ++cancelled; });

    model.clearBuffer();
    check(cmds.size() == 1 && cmds[0] == "cwx clear",
          "clearBuffer() sends 'cwx clear' command");
    check(cancelled == 1, "clearBuffer() emits transmissionCancelled");
}

// ── speed / delay ────────────────────────────────────────────────────────────

void testSetSpeedClamped()
{
    CwxModel model;
    QStringList cmds;
    QObject::connect(&model, &CwxModel::commandReady,
                     [&](const QString& cmd) { cmds << cmd; });

    model.setSpeed(3);    // below minimum (5)
    model.setSpeed(200);  // above maximum (100)
    check(cmds.size() == 2, "setSpeed sends two commands even when clamped");
    check(cmds[0].endsWith("5"),   "speed clamped to 5 at low end");
    check(cmds[1].endsWith("100"), "speed clamped to 100 at high end");
}

void testApplyStatusWpm()
{
    CwxModel model;
    int speedSignals = 0;
    QObject::connect(&model, &CwxModel::speedChanged,
                     [&](int) { ++speedSignals; });

    model.applyStatus({{"wpm", "25"}});
    check(speedSignals == 1, "applyStatus wpm=25 fires speedChanged");
    check(model.speed() == 25, "speed() returns 25 after applyStatus");
}

void testApplyStatusWpmNoDuplicateSignal()
{
    CwxModel model;
    model.applyStatus({{"wpm", "20"}});

    int speedSignals = 0;
    QObject::connect(&model, &CwxModel::speedChanged,
                     [&](int) { ++speedSignals; });

    model.applyStatus({{"wpm", "20"}});  // same value
    check(speedSignals == 0, "applyStatus with same wpm does not fire speedChanged");
}

// ── charSent ─────────────────────────────────────────────────────────────────

void testApplyStatusSentIndex()
{
    CwxModel model;
    int lastSent = -1;
    QObject::connect(&model, &CwxModel::charSent,
                     [&](int idx) { lastSent = idx; });

    model.applyStatus({{"sent", "7"}});
    check(lastSent == 7, "applyStatus sent=7 fires charSent(7)");
    check(model.sentIndex() == 7, "sentIndex() == 7 after applyStatus");
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testQueueEmptyOnEmptyValue();
    testQueueEmptyOnZeroValue();
    testQueueEmptyNotFiredOnNonEmptyQueue();
    testQueueEmptyNotFiredOnNonQueueKey();
    testQueueEmptyFiresOnlyForEmptyAmongMixedKeys();
    testQueueEmptyMultipleUpdates();
    testSendEmitsCommandReady();
    testSendBlockCounterIncrements();
    testSendEmptyIsNoop();
    testSendSpaceEncodedAsDel();
    testClearBufferEmitsCommandAndCancellation();
    testSetSpeedClamped();
    testApplyStatusWpm();
    testApplyStatusWpmNoDuplicateSignal();
    testApplyStatusSentIndex();

    std::printf("\n%s — %d test(s) failed\n",
                g_failed == 0 ? "PASSED" : "FAILED", g_failed);
    return g_failed ? 1 : 0;
}
