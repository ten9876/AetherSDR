#include "core/PerfTelemetry.h"
#include "core/LogManager.h"

#include <QCoreApplication>
#include <QLatin1String>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace AetherSDR {

// Friend access shim — peeks into the singleton's private state so tests can
// drive synthetic time and reset between cases. Adding no production behavior
// keeps the hot path untouched (issue #2500 triage Option A).
class PerfTelemetryTestAccess {
public:
    static void resetInstance()
    {
        PerfTelemetry& t = PerfTelemetry::instance();
        QMutexLocker lock(&t.m_mutex);
        t.m_window = PerfTelemetry::Window{};
        t.m_windowStartNs = 0;
        t.m_lastHeartbeatNs = 0;
        t.m_wasEnabled.store(false, std::memory_order_relaxed);
        t.m_dragActive.store(false, std::memory_order_relaxed);
        t.m_waterfallLineDurationMs.store(0, std::memory_order_relaxed);
    }

    static void primeForRecording(qint64 now)
    {
        PerfTelemetry& t = PerfTelemetry::instance();
        t.m_wasEnabled.store(true, std::memory_order_relaxed);
        QMutexLocker lock(&t.m_mutex);
        t.m_window = PerfTelemetry::Window{};
        t.m_windowStartNs = now;
        t.m_lastHeartbeatNs = 0;
    }

    static void setWindowStart(qint64 ns)
    {
        PerfTelemetry& t = PerfTelemetry::instance();
        QMutexLocker lock(&t.m_mutex);
        t.m_windowStartNs = ns;
    }

    static void setLastHeartbeat(qint64 ns)
    {
        PerfTelemetry& t = PerfTelemetry::instance();
        QMutexLocker lock(&t.m_mutex);
        t.m_lastHeartbeatNs = ns;
    }

    static int panUpdateSampleCount()
    {
        PerfTelemetry& t = PerfTelemetry::instance();
        QMutexLocker lock(&t.m_mutex);
        return static_cast<int>(t.m_window.panUpdateMs.size());
    }

    static void triggerSummary(qint64 now)
    {
        PerfTelemetry::instance().maybeLogSummary(now);
    }

    static double callPercentile95(QVector<double> values)
    {
        return PerfTelemetry::percentile95(std::move(values));
    }
};

} // namespace AetherSDR

namespace {

using AetherSDR::PerfTelemetry;
using AetherSDR::PerfTelemetryTestAccess;

int g_failed = 0;

struct CapturedLines {
    static CapturedLines& instance()
    {
        static CapturedLines cap;
        return cap;
    }

    QMutex mutex;
    QStringList lines;

    void clear()
    {
        QMutexLocker lock(&mutex);
        lines.clear();
    }

    QStringList snapshot()
    {
        QMutexLocker lock(&mutex);
        return lines;
    }
};

void perfMessageHandler(QtMsgType, const QMessageLogContext& ctx, const QString& msg)
{
    if (ctx.category && QLatin1String(ctx.category) == QLatin1String("aether.perf")) {
        QMutexLocker lock(&CapturedLines::instance().mutex);
        CapturedLines::instance().lines.append(msg);
    }
}

void report(const char* name, bool ok)
{
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", name);
    if (!ok)
        ++g_failed;
}

void enablePerf()
{
    QLoggingCategory::setFilterRules(QStringLiteral("aether.perf.debug=true"));
}

void disablePerf()
{
    QLoggingCategory::setFilterRules(QStringLiteral("aether.perf.debug=false"));
}

bool nearlyEqual(double a, double b, double eps = 0.001)
{
    return std::fabs(a - b) < eps;
}

QString fieldValue(const QString& line, const QString& key)
{
    const QString prefix = key + QLatin1Char('=');
    const auto tokens = line.split(QLatin1Char(' '));
    for (const QString& token : tokens) {
        if (token.startsWith(prefix))
            return token.mid(prefix.size());
    }
    return {};
}

QString findLineStartingWith(const QStringList& lines, const QString& prefix)
{
    for (const QString& line : lines) {
        if (line.startsWith(prefix))
            return line;
    }
    return {};
}

QString findStallOfKind(const QStringList& lines, const QString& kind)
{
    for (const QString& l : lines) {
        if (l.startsWith(QStringLiteral("PerfStall")) &&
            fieldValue(l, QStringLiteral("kind")) == kind) {
            return l;
        }
    }
    return {};
}

void testDisabledHotPathIsNoOp()
{
    disablePerf();
    PerfTelemetryTestAccess::resetInstance();
    CapturedLines::instance().clear();

    for (int i = 0; i < 100; ++i)
        PerfTelemetry::instance().recordPanUpdate(50.0);

    const QStringList lines = CapturedLines::instance().snapshot();
    const int samples = PerfTelemetryTestAccess::panUpdateSampleCount();

    report("disabled lcPerf emits no perf log lines",
           lines.isEmpty());
    report("disabled lcPerf mutates no window state",
           samples == 0);
}

void testStallThresholdPanUpdateAbove()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    PerfTelemetryTestAccess::primeForRecording(PerfTelemetry::nowNs());
    CapturedLines::instance().clear();

    PerfTelemetry::instance().recordPanUpdate(9.0);

    const QString stall = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("updateSpectrum"));
    report("panUpdate 9.0ms emits PerfStall kind=updateSpectrum (>8ms)",
           !stall.isEmpty() &&
           fieldValue(stall, QStringLiteral("durationMs")) == QStringLiteral("9.0"));
}

void testStallThresholdPanUpdateBelow()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    PerfTelemetryTestAccess::primeForRecording(PerfTelemetry::nowNs());
    CapturedLines::instance().clear();

    PerfTelemetry::instance().recordPanUpdate(7.0);

    report("panUpdate 7.0ms emits no PerfStall (<8ms threshold)",
           findLineStartingWith(CapturedLines::instance().snapshot(),
                                QStringLiteral("PerfStall")).isEmpty());
}

void testStallThresholdFrameAge()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    PerfTelemetryTestAccess::primeForRecording(PerfTelemetry::nowNs());

    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordFrameAge(PerfTelemetry::FrameKind::Panadapter, 101.0);
    const QString above = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("frameAge"));

    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordFrameAge(PerfTelemetry::FrameKind::Panadapter, 99.0);
    const QString below = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("frameAge"));

    report("frameAge 101ms emits PerfStall kind=frameAge (>100ms)",
           !above.isEmpty() &&
           fieldValue(above, QStringLiteral("stream")) == QStringLiteral("panadapter"));
    report("frameAge 99ms emits no PerfStall (<100ms threshold)",
           below.isEmpty());
}

void testStallThresholdRender()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    PerfTelemetryTestAccess::primeForRecording(PerfTelemetry::nowNs());

    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordRender(17.0);
    const QString above = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("render"));

    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordRender(15.0);
    const QString below = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("render"));

    report("render 17.0ms emits PerfStall kind=render (>16ms)",
           !above.isEmpty());
    report("render 15.0ms emits no PerfStall (<16ms threshold)",
           below.isEmpty());
}

void testStallThresholdUdpDrain()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    PerfTelemetryTestAccess::primeForRecording(PerfTelemetry::nowNs());

    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordUdpBatch(5, 1500, 9.0);
    const QString above = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("udpDrain"));

    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordUdpBatch(5, 1500, 7.0);
    const QString below = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("udpDrain"));

    report("udpDrain 9.0ms emits PerfStall kind=udpDrain (>8ms)",
           !above.isEmpty());
    report("udpDrain 7.0ms emits no PerfStall (<8ms threshold)",
           below.isEmpty());
}

void testStallThresholdWaterfallUpdate()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    PerfTelemetryTestAccess::primeForRecording(PerfTelemetry::nowNs());

    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordWaterfallUpdate(13.0);
    const QString above = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("updateWaterfallRow"));

    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordWaterfallUpdate(11.0);
    const QString below = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("updateWaterfallRow"));

    report("waterfallUpdate 13.0ms emits PerfStall kind=updateWaterfallRow (>12ms)",
           !above.isEmpty());
    report("waterfallUpdate 11.0ms emits no PerfStall (<12ms threshold)",
           below.isEmpty());
}

void testStallThresholdInput()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    PerfTelemetryTestAccess::primeForRecording(PerfTelemetry::nowNs());

    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordInputEvent("mouseMove", 17.0);
    const QString above = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("input"));

    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordInputEvent("mouseMove", 15.0);
    const QString below = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("input"));

    report("input 17.0ms emits PerfStall kind=input (>16ms)",
           !above.isEmpty() &&
           fieldValue(above, QStringLiteral("event")) == QStringLiteral("mouseMove"));
    report("input 15.0ms emits no PerfStall (<16ms threshold)",
           below.isEmpty());
}

void testDragAwareUiLagIdleBelowBoth()
{
    // gap=80ms → lag=max(0, 80-50)=30ms. Below idle (50) and drag (33). No stall.
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    PerfTelemetryTestAccess::primeForRecording(PerfTelemetry::nowNs());
    PerfTelemetry::instance().setDragActive(false);

    const qint64 now = PerfTelemetry::nowNs();
    PerfTelemetryTestAccess::setLastHeartbeat(now - 80LL * 1000000LL);
    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordUiHeartbeat();

    report("idle UI heartbeat gap=80ms (lag=30) emits no stall",
           findStallOfKind(CapturedLines::instance().snapshot(),
                           QStringLiteral("uiHeartbeat")).isEmpty());
}

void testDragAwareUiLagDragStall()
{
    // gap=90ms → lag=40ms. Below idle (50), above drag (33). Drag stall fires.
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    PerfTelemetryTestAccess::primeForRecording(PerfTelemetry::nowNs());
    PerfTelemetry::instance().setDragActive(true);

    const qint64 now = PerfTelemetry::nowNs();
    PerfTelemetryTestAccess::setLastHeartbeat(now - 90LL * 1000000LL);
    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordUiHeartbeat();

    const QString stall = findStallOfKind(CapturedLines::instance().snapshot(),
                                          QStringLiteral("uiHeartbeat"));
    report("drag-active UI heartbeat gap=90ms (lag=40) emits stall (>33ms drag threshold)",
           !stall.isEmpty() &&
           fieldValue(stall, QStringLiteral("drag")) == QStringLiteral("1"));
}

void testDragAwareUiLagIdleSameGapNoStall()
{
    // gap=90ms idle → lag=40, < 50 idle threshold. No stall.
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    PerfTelemetryTestAccess::primeForRecording(PerfTelemetry::nowNs());
    PerfTelemetry::instance().setDragActive(false);

    const qint64 now = PerfTelemetry::nowNs();
    PerfTelemetryTestAccess::setLastHeartbeat(now - 90LL * 1000000LL);
    CapturedLines::instance().clear();
    PerfTelemetry::instance().recordUiHeartbeat();

    report("idle UI heartbeat gap=90ms (lag=40) emits no stall (<50ms idle threshold)",
           findStallOfKind(CapturedLines::instance().snapshot(),
                           QStringLiteral("uiHeartbeat")).isEmpty());
}

void testFrameRestartCounter()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    const qint64 baseNs = PerfTelemetry::nowNs();
    PerfTelemetryTestAccess::primeForRecording(baseNs);

    for (int i = 0; i < 3; ++i)
        PerfTelemetry::instance().recordFrameRestart(PerfTelemetry::FrameKind::Panadapter);

    PerfTelemetryTestAccess::setWindowStart(baseNs - 2LL * 1000000000LL);
    CapturedLines::instance().clear();
    PerfTelemetryTestAccess::triggerSummary(baseNs);

    const QString summary = findLineStartingWith(CapturedLines::instance().snapshot(),
                                                 QStringLiteral("PerfSummary"));
    report("3 panadapter frame restarts produce fftRestarts=3 in next summary",
           !summary.isEmpty() &&
           fieldValue(summary, QStringLiteral("fftRestarts")) == QStringLiteral("3"));
}

void testPercentile95Empty()
{
    report("percentile95 of empty vector returns 0.0",
           nearlyEqual(PerfTelemetryTestAccess::callPercentile95(QVector<double>{}), 0.0));
}

void testPercentile95Single()
{
    report("percentile95 of single value returns that value",
           nearlyEqual(PerfTelemetryTestAccess::callPercentile95(QVector<double>{42.5}), 42.5));
}

void testPercentile95Hundred()
{
    QVector<double> values;
    for (int i = 1; i <= 100; ++i)
        values.append(static_cast<double>(i));
    report("percentile95 of [1..100] returns 95.0",
           nearlyEqual(PerfTelemetryTestAccess::callPercentile95(values), 95.0));
}

void testPercentile95OrderInvariant()
{
    QVector<double> sorted;
    for (int i = 1; i <= 50; ++i)
        sorted.append(static_cast<double>(i));
    QVector<double> shuffled = sorted;
    for (int i = 0; i + 1 < shuffled.size(); i += 2)
        std::swap(shuffled[i], shuffled[i + 1]);

    const double a = PerfTelemetryTestAccess::callPercentile95(sorted);
    const double b = PerfTelemetryTestAccess::callPercentile95(shuffled);
    report("percentile95 is invariant to input ordering",
           nearlyEqual(a, b));
}

void testWindowTimingBefore1s()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    const qint64 baseNs = PerfTelemetry::nowNs();
    PerfTelemetryTestAccess::primeForRecording(baseNs);

    PerfTelemetry::instance().recordPanFrame();
    CapturedLines::instance().clear();

    PerfTelemetryTestAccess::triggerSummary(baseNs + 950LL * 1000000LL);

    report("elapsed 950ms does not trigger summary",
           findLineStartingWith(CapturedLines::instance().snapshot(),
                                QStringLiteral("PerfSummary")).isEmpty());
}

void testWindowTimingAfter1s()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    const qint64 baseNs = PerfTelemetry::nowNs();
    PerfTelemetryTestAccess::primeForRecording(baseNs);

    PerfTelemetry::instance().recordPanFrame();
    PerfTelemetry::instance().recordPanFrame();
    PerfTelemetry::instance().recordPanFrame();
    CapturedLines::instance().clear();

    PerfTelemetryTestAccess::triggerSummary(baseNs + 1100LL * 1000000LL);

    const QString summary = findLineStartingWith(CapturedLines::instance().snapshot(),
                                                 QStringLiteral("PerfSummary"));
    report("elapsed 1100ms triggers summary",
           !summary.isEmpty());
}

void testWindowResetsAfterSummary()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    const qint64 baseNs = PerfTelemetry::nowNs();
    PerfTelemetryTestAccess::primeForRecording(baseNs);

    PerfTelemetry::instance().recordPanFrame();
    PerfTelemetry::instance().recordPanFrame();

    PerfTelemetryTestAccess::triggerSummary(baseNs + 1100LL * 1000000LL);
    CapturedLines::instance().clear();

    PerfTelemetryTestAccess::triggerSummary(baseNs + 2200LL * 1000000LL);

    const QString summary = findLineStartingWith(CapturedLines::instance().snapshot(),
                                                 QStringLiteral("PerfSummary"));
    report("window resets after summary - second summary has panFps=0.0",
           !summary.isEmpty() &&
           fieldValue(summary, QStringLiteral("panFps")) == QStringLiteral("0.0"));
}

void testWindowAggregationPanUpdateP95()
{
    enablePerf();
    PerfTelemetryTestAccess::resetInstance();
    const qint64 baseNs = PerfTelemetry::nowNs();
    PerfTelemetryTestAccess::primeForRecording(baseNs);

    // Feed 20 sub-threshold values 0.1..2.0; p95 index = ceil(20*0.95)-1 = 18 → 1.9.
    for (int i = 1; i <= 20; ++i)
        PerfTelemetry::instance().recordPanUpdate(static_cast<double>(i) * 0.1);

    CapturedLines::instance().clear();
    PerfTelemetryTestAccess::triggerSummary(baseNs + 1100LL * 1000000LL);

    const QString summary = findLineStartingWith(CapturedLines::instance().snapshot(),
                                                 QStringLiteral("PerfSummary"));
    report("aggregation: panUpdateP95Ms reflects p95 of 20 recorded samples",
           !summary.isEmpty() &&
           fieldValue(summary, QStringLiteral("panUpdateP95Ms")) == QStringLiteral("1.9"));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    qInstallMessageHandler(perfMessageHandler);

    testDisabledHotPathIsNoOp();
    testStallThresholdPanUpdateAbove();
    testStallThresholdPanUpdateBelow();
    testStallThresholdFrameAge();
    testStallThresholdRender();
    testStallThresholdUdpDrain();
    testStallThresholdWaterfallUpdate();
    testStallThresholdInput();
    testDragAwareUiLagIdleBelowBoth();
    testDragAwareUiLagDragStall();
    testDragAwareUiLagIdleSameGapNoStall();
    testFrameRestartCounter();
    testPercentile95Empty();
    testPercentile95Single();
    testPercentile95Hundred();
    testPercentile95OrderInvariant();
    testWindowTimingBefore1s();
    testWindowTimingAfter1s();
    testWindowResetsAfterSummary();
    testWindowAggregationPanUpdateP95();

    qInstallMessageHandler(nullptr);

    return g_failed == 0 ? 0 : 1;
}
