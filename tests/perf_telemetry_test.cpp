// Unit tests for PerfTelemetry — issue #2500.
//
// Covers: disabled hot-path no-op, window aggregation, stall thresholds,
// drag-aware UI lag, frame-restart counter, percentile-95 boundaries, and
// window timing — by driving the singleton through its public record* API
// with a synthetic clock seam (setClockOverrideForTest) and capturing the
// lcPerf log line via qInstallMessageHandler.
//
// CMake target `perf_telemetry_test`. Exit 0 = pass.

#include "core/PerfTelemetry.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QString>
#include <QStringList>
#include <QtGlobal>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

// PerfTelemetry.cpp uses lcPerf via LogManager.h. Standalone tests provide
// their own definition rather than pulling LogManager (which would drag in
// AppSettings, AsyncLogWriter, etc.). Default level is QtDebugMsg so the
// hot path is "enabled" — tests flip rules off when they need the disabled
// path verified.
namespace AetherSDR {
Q_LOGGING_CATEGORY(lcPerf, "aether.perf", QtDebugMsg)
} // namespace AetherSDR

using AetherSDR::PerfTelemetry;

namespace {

int g_failed = 0;
QStringList g_capturedLines;

void messageHandler(QtMsgType, const QMessageLogContext& ctx, const QString& msg)
{
    if (ctx.category && QString::fromUtf8(ctx.category) == QLatin1String("aether.perf"))
        g_capturedLines << msg;
}

void report(const char* name, bool ok, const std::string& detail = {})
{
    std::printf("%s %-58s %s\n",
                ok ? "[ OK ]" : "[FAIL]",
                name,
                detail.c_str());
    if (!ok) ++g_failed;
}

void resetCapture()
{
    g_capturedLines.clear();
}

QStringList stallLines()
{
    QStringList out;
    for (const auto& line : g_capturedLines)
        if (line.startsWith(QLatin1String("PerfStall")))
            out << line;
    return out;
}

QStringList summaryLines()
{
    QStringList out;
    for (const auto& line : g_capturedLines)
        if (line.startsWith(QLatin1String("PerfSummary")))
            out << line;
    return out;
}

QString fieldValue(const QString& line, const QString& key)
{
    const QStringList parts = line.split(QLatin1Char(' '));
    const QString prefix = key + QLatin1Char('=');
    for (const auto& part : parts) {
        if (part.startsWith(prefix))
            return part.mid(prefix.size());
    }
    return {};
}

void prime(qint64 t0)
{
    // Reset state and pin the clock — the next record call will start a
    // fresh window at t0.
    PerfTelemetry::setClockOverrideForTest(t0);
    PerfTelemetry::instance().resetForTest();
    resetCapture();
}

// Advance synthetic clock to the next 1-second boundary and emit a final
// recordPanFrame so maybeLogSummary fires. Returns the captured summary.
QString flushSummary(qint64 windowStartNs)
{
    PerfTelemetry::setClockOverrideForTest(windowStartNs + 1'100'000'000LL);
    PerfTelemetry::instance().recordPanFrame();
    const auto summaries = summaryLines();
    return summaries.isEmpty() ? QString() : summaries.last();
}

// --- Disabled hot-path is no-op ---------------------------------------------

void testDisabledHotPath()
{
    QLoggingCategory::setFilterRules(QStringLiteral("aether.perf.debug=false"));
    PerfTelemetry::setClockOverrideForTest(1'000'000'000LL);
    PerfTelemetry::instance().resetForTest();
    resetCapture();

    for (int i = 0; i < 50; ++i)
        PerfTelemetry::instance().recordPanUpdate(50.0);

    report("disabled hot-path emits no log lines",
           g_capturedLines.isEmpty(),
           g_capturedLines.isEmpty() ? "" : std::string("got ") + std::to_string(g_capturedLines.size()) + " lines");

    QLoggingCategory::setFilterRules(QStringLiteral("aether.perf.debug=true"));
}

// --- Window aggregation: count, p95, max ------------------------------------

void testWindowAggregation()
{
    constexpr qint64 t0 = 5'000'000'000LL;
    prime(t0);

    // Feed 10 panUpdate samples below the stall threshold (8.0 ms) so no
    // PerfStall lines are emitted — they would otherwise crowd capture.
    // Values: 1, 2, 3, 4, 5, 6, 7, 7.5, 7.8, 7.9 — all <= 8.0.
    const std::vector<double> samples = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 7.5, 7.8, 7.9};
    for (double v : samples)
        PerfTelemetry::instance().recordPanUpdate(v);

    const QString summary = flushSummary(t0);
    const bool hasSummary = !summary.isEmpty();
    report("window aggregation emits a summary", hasSummary);

    if (hasSummary) {
        // percentile95 for N=10: ceil(10*0.95)-1 = 9, sorted[9] = 7.9
        const QString p95 = fieldValue(summary, QStringLiteral("panUpdateP95Ms"));
        report("panUpdateP95Ms equals 7.9 for 10 sorted samples",
               p95 == QLatin1String("7.9"),
               std::string("got '") + p95.toStdString() + "'");
    }
}

// --- Window resets after summary --------------------------------------------

void testWindowResetsAfterSummary()
{
    constexpr qint64 t0 = 10'000'000'000LL;
    prime(t0);

    PerfTelemetry::instance().recordPanUpdate(5.0);
    PerfTelemetry::instance().recordPanUpdate(6.0);
    PerfTelemetry::instance().recordPanUpdate(7.0);
    (void)flushSummary(t0);

    // Now drive a fresh window from t0 + 1.1s onward and verify the new
    // summary doesn't carry the prior window's panUpdate samples.
    constexpr qint64 t1 = 10'000'000'000LL + 1'100'000'000LL;
    resetCapture();
    PerfTelemetry::setClockOverrideForTest(t1);
    PerfTelemetry::instance().recordPanUpdate(1.0);

    const QString summary = flushSummary(t1);
    const QString p95 = fieldValue(summary, QStringLiteral("panUpdateP95Ms"));
    // Second window had one sample at 1.0, then flushSummary added a
    // recordPanFrame (which doesn't add to panUpdateMs). So P95 should be 1.0.
    report("window state does not bleed across summaries",
           p95 == QLatin1String("1.0"),
           std::string("got '") + p95.toStdString() + "'");
}

// --- Stall thresholds -------------------------------------------------------

void testStallThresholdMatrix()
{
    struct Case {
        const char* name;
        double justUnder;
        double justOver;
        QString expectedStallKind;
        std::function<void(double)> trigger;
    };

    // Threshold values are duplicated from PerfTelemetry.cpp — if either
    // drifts, these tests must be updated in lockstep with the production
    // constants.
    const std::vector<Case> cases = {
        {"panUpdate stall (8.0 ms)", 7.5, 8.5, QStringLiteral("updateSpectrum"),
            [](double v){ PerfTelemetry::instance().recordPanUpdate(v); }},
        {"waterfallUpdate stall (12.0 ms)", 11.0, 13.0, QStringLiteral("updateWaterfallRow"),
            [](double v){ PerfTelemetry::instance().recordWaterfallUpdate(v); }},
        {"render stall (16.0 ms)", 15.0, 17.0, QStringLiteral("render"),
            [](double v){ PerfTelemetry::instance().recordRender(v); }},
        {"frameAge stall (100.0 ms)", 99.0, 101.0, QStringLiteral("frameAge"),
            [](double v){ PerfTelemetry::instance().recordFrameAge(PerfTelemetry::FrameKind::Panadapter, v); }},
        {"udpDrain stall (8.0 ms)", 7.5, 8.5, QStringLiteral("udpDrain"),
            [](double v){ PerfTelemetry::instance().recordUdpBatch(1, 100, v); }},
        {"input stall (16.0 ms)", 15.0, 17.0, QStringLiteral("input"),
            [](double v){ PerfTelemetry::instance().recordInputEvent("wheel", v); }},
    };

    qint64 t = 100'000'000'000LL;
    for (const auto& c : cases) {
        // Below-threshold: no stall.
        prime(t);
        c.trigger(c.justUnder);
        bool sawStallBelow = false;
        for (const auto& line : stallLines())
            if (fieldValue(line, QStringLiteral("kind")) == c.expectedStallKind)
                sawStallBelow = true;
        report((std::string(c.name) + " below threshold: no stall").c_str(),
               !sawStallBelow);

        // Above-threshold: stall fires.
        t += 5'000'000'000LL;
        prime(t);
        c.trigger(c.justOver);
        bool sawStallAbove = false;
        for (const auto& line : stallLines())
            if (fieldValue(line, QStringLiteral("kind")) == c.expectedStallKind)
                sawStallAbove = true;
        report((std::string(c.name) + " above threshold: stall fires").c_str(),
               sawStallAbove);

        t += 5'000'000'000LL;
    }
}

// --- Drag-aware UI lag thresholds -------------------------------------------
//
// Lag is computed as max(0, gap - kHeartbeatIntervalMs) where the heartbeat
// interval is 50 ms. Idle stall threshold is 50 ms (so gap > 100 ms); drag
// stall threshold is 33 ms (so gap > 83 ms). A 90 ms gap → lag=40, which
// trips drag but not idle.

void testUiHeartbeatDragThresholds()
{
    constexpr qint64 t0 = 200'000'000'000LL;

    // Idle, gap = 90 ms → lag = 40 ms < 50 ms idle threshold → no stall.
    prime(t0);
    PerfTelemetry::instance().setDragActive(false);
    PerfTelemetry::instance().recordUiHeartbeat();
    PerfTelemetry::setClockOverrideForTest(t0 + 90'000'000LL);
    PerfTelemetry::instance().recordUiHeartbeat();
    bool sawIdleStall = false;
    for (const auto& line : stallLines())
        if (fieldValue(line, QStringLiteral("kind")) == QLatin1String("uiHeartbeat"))
            sawIdleStall = true;
    report("uiHeartbeat idle 90ms gap (lag=40): no stall", !sawIdleStall);

    // Drag, same 90 ms gap → lag = 40 ms > 33 ms drag threshold → stall.
    const qint64 t1 = t0 + 5'000'000'000LL;
    prime(t1);
    PerfTelemetry::instance().setDragActive(true);
    PerfTelemetry::instance().recordUiHeartbeat();
    PerfTelemetry::setClockOverrideForTest(t1 + 90'000'000LL);
    PerfTelemetry::instance().recordUiHeartbeat();
    bool sawDragStall = false;
    for (const auto& line : stallLines())
        if (fieldValue(line, QStringLiteral("kind")) == QLatin1String("uiHeartbeat"))
            sawDragStall = true;
    report("uiHeartbeat drag 90ms gap (lag=40): stall fires", sawDragStall);

    // Idle, gap = 110 ms → lag = 60 ms > 50 ms idle threshold → stall.
    const qint64 t2 = t1 + 5'000'000'000LL;
    prime(t2);
    PerfTelemetry::instance().setDragActive(false);
    PerfTelemetry::instance().recordUiHeartbeat();
    PerfTelemetry::setClockOverrideForTest(t2 + 110'000'000LL);
    PerfTelemetry::instance().recordUiHeartbeat();
    bool sawIdleStallHigh = false;
    for (const auto& line : stallLines())
        if (fieldValue(line, QStringLiteral("kind")) == QLatin1String("uiHeartbeat"))
            sawIdleStallHigh = true;
    report("uiHeartbeat idle 110ms gap (lag=60): stall fires", sawIdleStallHigh);
}

// --- Frame-restart counter --------------------------------------------------

void testFrameRestartCounter()
{
    constexpr qint64 t0 = 300'000'000'000LL;
    prime(t0);

    PerfTelemetry::instance().recordFrameRestart(PerfTelemetry::FrameKind::Panadapter);
    PerfTelemetry::instance().recordFrameRestart(PerfTelemetry::FrameKind::Panadapter);
    PerfTelemetry::instance().recordFrameRestart(PerfTelemetry::FrameKind::Panadapter);

    const QString summary = flushSummary(t0);
    const QString restarts = fieldValue(summary, QStringLiteral("fftRestarts"));
    report("3x recordFrameRestart(Panadapter) shows fftRestarts=3",
           restarts == QLatin1String("3"),
           std::string("got '") + restarts.toStdString() + "'");
}

// --- Percentile-95 boundary cases (exercised via the public record path) ----

void testPercentile95Boundaries()
{
    // Empty input — no panUpdate samples, but a recordPanFrame to ensure
    // maybeLogSummary fires.
    {
        constexpr qint64 t0 = 400'000'000'000LL;
        prime(t0);
        const QString summary = flushSummary(t0);
        const QString p95 = fieldValue(summary, QStringLiteral("panUpdateP95Ms"));
        report("p95 empty input returns 0.0",
               p95 == QLatin1String("0.0"),
               std::string("got '") + p95.toStdString() + "'");
    }

    // Single value — keep it under the 8 ms stall threshold.
    {
        constexpr qint64 t0 = 410'000'000'000LL;
        prime(t0);
        PerfTelemetry::instance().recordPanUpdate(4.2);
        const QString summary = flushSummary(t0);
        const QString p95 = fieldValue(summary, QStringLiteral("panUpdateP95Ms"));
        report("p95 single value returns that value",
               p95 == QLatin1String("4.2"),
               std::string("got '") + p95.toStdString() + "'");
    }

    // 100 values [1..100] — render samples (threshold 16 ms) lets values
    // 1..15 pass without stall noise; for percentile coverage use frameAge
    // which has a 100 ms threshold — we want all 100 values under it.
    // frameAge stall fires at > 100, so 1..100 are all clean.
    {
        constexpr qint64 t0 = 420'000'000'000LL;
        prime(t0);
        for (int i = 1; i <= 100; ++i)
            PerfTelemetry::instance().recordFrameAge(PerfTelemetry::FrameKind::Panadapter,
                                                     static_cast<double>(i));
        const QString summary = flushSummary(t0);
        const QString p95 = fieldValue(summary, QStringLiteral("panAgeP95Ms"));
        // ceil(100*0.95)-1 = 94, sorted[94] = 95.
        report("p95 of [1..100] returns 95.0",
               p95 == QLatin1String("95.0"),
               std::string("got '") + p95.toStdString() + "'");
    }

    // Same 100 values fed in reverse — sort inside percentile95 should give
    // the same result.
    {
        constexpr qint64 t0 = 430'000'000'000LL;
        prime(t0);
        for (int i = 100; i >= 1; --i)
            PerfTelemetry::instance().recordFrameAge(PerfTelemetry::FrameKind::Panadapter,
                                                     static_cast<double>(i));
        const QString summary = flushSummary(t0);
        const QString p95 = fieldValue(summary, QStringLiteral("panAgeP95Ms"));
        report("p95 unsorted input matches sorted input (95.0)",
               p95 == QLatin1String("95.0"),
               std::string("got '") + p95.toStdString() + "'");
    }
}

// --- Window timing ----------------------------------------------------------

void testWindowTiming()
{
    constexpr qint64 t0 = 500'000'000'000LL;
    prime(t0);

    // Samples spread across 950 ms — no summary yet.
    PerfTelemetry::setClockOverrideForTest(t0);
    PerfTelemetry::instance().recordPanUpdate(1.0);
    PerfTelemetry::setClockOverrideForTest(t0 + 300'000'000LL);
    PerfTelemetry::instance().recordPanUpdate(2.0);
    PerfTelemetry::setClockOverrideForTest(t0 + 600'000'000LL);
    PerfTelemetry::instance().recordPanUpdate(3.0);
    PerfTelemetry::setClockOverrideForTest(t0 + 950'000'000LL);
    PerfTelemetry::instance().recordPanUpdate(4.0);

    report("no summary fires before 1s boundary",
           summaryLines().isEmpty(),
           std::string("got ") + std::to_string(summaryLines().size()) + " summary lines");

    // One more sample past the 1 s boundary — summary should fire and
    // include all five samples in panUpdateP95Ms.
    PerfTelemetry::setClockOverrideForTest(t0 + 1'100'000'000LL);
    PerfTelemetry::instance().recordPanUpdate(5.0);

    const auto summaries = summaryLines();
    report("summary fires once 1s window elapses",
           !summaries.isEmpty());

    if (!summaries.isEmpty()) {
        const QString p95 = fieldValue(summaries.last(), QStringLiteral("panUpdateP95Ms"));
        // ceil(5*0.95)-1 = 4, sorted[4] = 5.0
        report("summary captures all 5 samples (p95=5.0)",
               p95 == QLatin1String("5.0"),
               std::string("got '") + p95.toStdString() + "'");
    }
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QLoggingCategory::setFilterRules(QStringLiteral("aether.perf.debug=true"));
    qInstallMessageHandler(messageHandler);

    testDisabledHotPath();
    testWindowAggregation();
    testWindowResetsAfterSummary();
    testStallThresholdMatrix();
    testUiHeartbeatDragThresholds();
    testFrameRestartCounter();
    testPercentile95Boundaries();
    testWindowTiming();

    qInstallMessageHandler(nullptr);
    PerfTelemetry::setClockOverrideForTest(0);

    if (g_failed == 0) {
        std::printf("\nAll PerfTelemetry tests passed.\n");
        return 0;
    }
    std::printf("\n%d test(s) FAILED.\n", g_failed);
    return 1;
}
