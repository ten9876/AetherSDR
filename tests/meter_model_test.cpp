#include "models/MeterModel.h"

#include <QCoreApplication>
#include <QThread>
#include <QVector>

#include <cmath>
#include <cstdio>

using namespace AetherSDR;

namespace {

int g_failed = 0;

void report(const char* name, bool ok)
{
    std::printf("%s %s\n", ok ? "[ OK ]" : "[FAIL]", name);
    if (!ok) ++g_failed;
}

bool nearlyEqual(float a, float b)
{
    return std::fabs(a - b) < 0.01f;
}

qint16 rawDb(float db)
{
    return static_cast<qint16>(std::lround(db * 128.0f));
}

MeterDef txMeter(int index, const QString& name, const QString& unit = QStringLiteral("dBFS"))
{
    MeterDef def;
    def.index = index;
    def.source = "TX-";
    def.name = name;
    def.unit = unit;
    def.low = -150.0;
    def.high = 20.0;
    return def;
}

void testAdjacentMetersDoNotSynthesizeCompression()
{
    MeterModel model;
    model.defineMeter(txMeter(22, "SC_MIC", "dBFS"));

    model.updateValues({22}, {rawDb(-10.0f)});

    report("adjacent TX audio meters do not synthesize compression",
           !model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), 0.0f));
}

void testCompPeakRequiresAfterEq()
{
    MeterModel model;
    model.defineMeter(txMeter(28, "COMPPEAK"));

    model.updateValues({28}, {rawDb(-40.0f)});

    report("COMPPEAK alone does not expose compression",
           !model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), 0.0f));
}

void testAfterEqRequiresCompPeak()
{
    MeterModel model;
    model.defineMeter(txMeter(27, "AFTEREQ"));

    model.updateValues({27}, {rawDb(-60.0f)});

    report("AFTEREQ alone does not expose compression",
           !model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), 0.0f));
}

void testCompPeakMinusAfterEqDrivesGauge()
{
    MeterModel model;
    model.defineMeter(txMeter(27, "AFTEREQ"));
    model.defineMeter(txMeter(28, "COMPPEAK"));

    model.updateValues({27, 28}, {rawDb(-60.0f), rawDb(-40.0f)});

    report("COMPPEAK minus AFTEREQ maps to negative gauge value",
           model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), -20.0f));
}

void testCompPeakMinusScMicDrivesGaugeWhenAfterEqMissing()
{
    MeterModel model;
    model.defineMeter(txMeter(22, "SC_MIC"));
    model.defineMeter(txMeter(23, "COMPPEAK"));

    model.updateValues({22, 23}, {rawDb(-42.0f), rawDb(-22.0f)});

    report("COMPPEAK minus SC_MIC maps 6000-series compression",
           model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), -20.0f));
}

void testScMicCompressionDerivationIsOrderIndependent()
{
    MeterModel model;
    model.defineMeter(txMeter(22, "SC_MIC"));
    model.defineMeter(txMeter(23, "COMPPEAK"));

    model.updateValues({23}, {rawDb(-22.0f)});
    model.updateValues({22}, {rawDb(-42.0f)});

    report("SC_MIC compression derivation is order independent",
           model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), -20.0f));
}

void testAfterEqPreferredOverScMicWhenBothAreDefined()
{
    MeterModel model;
    model.defineMeter(txMeter(22, "SC_MIC"));
    model.defineMeter(txMeter(27, "AFTEREQ"));
    model.defineMeter(txMeter(28, "COMPPEAK"));

    model.updateValues({22, 27, 28}, {rawDb(-80.0f), rawDb(-20.0f), rawDb(-10.0f)});

    report("AFTEREQ is preferred over SC_MIC when both are present",
           model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), -10.0f));
}

void testAfterEqDefinitionDoesNotFallbackToScMicWithoutAfterEqValue()
{
    MeterModel model;
    model.defineMeter(txMeter(22, "SC_MIC"));
    model.defineMeter(txMeter(27, "AFTEREQ"));
    model.defineMeter(txMeter(28, "COMPPEAK"));

    model.updateValues({22, 28}, {rawDb(-80.0f), rawDb(-40.0f)});

    report("AFTEREQ definition requires AFTEREQ data and does not fallback",
           !model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), 0.0f));
}

void testCompPeakAndAfterEqAreOrderIndependent()
{
    MeterModel model;
    model.defineMeter(txMeter(27, "AFTEREQ"));
    model.defineMeter(txMeter(28, "COMPPEAK"));

    model.updateValues({28}, {rawDb(-40.0f)});
    model.updateValues({27}, {rawDb(-60.0f)});

    report("compression derivation is order independent",
           model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), -20.0f));
}

void testNoStageLiftShowsNoCompression()
{
    MeterModel model;
    model.defineMeter(txMeter(27, "AFTEREQ"));
    model.defineMeter(txMeter(28, "COMPPEAK"));

    model.updateValues({27, 28}, {rawDb(-30.0f), rawDb(-45.0f)});

    report("COMPPEAK below AFTEREQ shows no compression",
           model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), 0.0f));
}

void testDerivedCompressionClampsToGaugeRange()
{
    MeterModel model;
    model.defineMeter(txMeter(27, "AFTEREQ"));
    model.defineMeter(txMeter(28, "COMPPEAK"));

    model.updateValues({27, 28}, {rawDb(-80.0f), rawDb(-40.0f)});

    report("derived compression clamps to the compression gauge range",
           model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), -25.0f));
}

void testStaleScMicReferenceDoesNotDriveCompression()
{
    MeterModel model;
    model.defineMeter(txMeter(22, "SC_MIC"));
    model.defineMeter(txMeter(23, "COMPPEAK"));

    model.updateValues({22}, {rawDb(-42.0f)});
    QThread::msleep(300);
    model.updateValues({23}, {rawDb(-22.0f)});

    report("stale SC_MIC reference does not drive compression",
           !model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), 0.0f));

    model.updateValues({22}, {rawDb(-42.0f)});
    report("fresh SC_MIC reference restores compression",
           model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), -20.0f));
}

void testRemovingCompPeakMarksCompressionUnavailable()
{
    MeterModel model;
    model.defineMeter(txMeter(27, "AFTEREQ"));
    model.defineMeter(txMeter(28, "COMPPEAK"));
    model.updateValues({27, 28}, {rawDb(-60.0f), rawDb(-40.0f)});
    model.removeMeter(28);

    report("removing COMPPEAK marks compression unavailable",
           !model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), 0.0f));
}

void testRemovingAfterEqMarksCompressionUnavailable()
{
    MeterModel model;
    model.defineMeter(txMeter(27, "AFTEREQ"));
    model.defineMeter(txMeter(28, "COMPPEAK"));
    model.updateValues({27, 28}, {rawDb(-60.0f), rawDb(-40.0f)});
    model.removeMeter(27);

    report("removing AFTEREQ marks compression unavailable",
           !model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), 0.0f));
}

void testRemovingScMicMarks6000CompressionUnavailable()
{
    MeterModel model;
    model.defineMeter(txMeter(22, "SC_MIC"));
    model.defineMeter(txMeter(23, "COMPPEAK"));
    model.updateValues({22, 23}, {rawDb(-42.0f), rawDb(-22.0f)});
    model.removeMeter(22);

    report("removing SC_MIC marks 6000-series compression unavailable",
           !model.hasCompressionMeterValue() && nearlyEqual(model.compPeak(), 0.0f));
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testAdjacentMetersDoNotSynthesizeCompression();
    testCompPeakRequiresAfterEq();
    testAfterEqRequiresCompPeak();
    testCompPeakMinusAfterEqDrivesGauge();
    testCompPeakMinusScMicDrivesGaugeWhenAfterEqMissing();
    testScMicCompressionDerivationIsOrderIndependent();
    testAfterEqPreferredOverScMicWhenBothAreDefined();
    testAfterEqDefinitionDoesNotFallbackToScMicWithoutAfterEqValue();
    testCompPeakAndAfterEqAreOrderIndependent();
    testNoStageLiftShowsNoCompression();
    testDerivedCompressionClampsToGaugeRange();
    testStaleScMicReferenceDoesNotDriveCompression();
    testRemovingCompPeakMarksCompressionUnavailable();
    testRemovingAfterEqMarksCompressionUnavailable();
    testRemovingScMicMarks6000CompressionUnavailable();

    return g_failed == 0 ? 0 : 1;
}
