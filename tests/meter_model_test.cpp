#include "models/MeterModel.h"

#include <QCoreApplication>
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

void testAfterEqFeedsDisplayedMicLevel()
{
    MeterModel model;
    model.defineMeter(txMeter(2, "MIC"));
    model.defineMeter(txMeter(1, "MICPEAK"));
    model.defineMeter(txMeter(27, "AFTEREQ"));

    bool seen = false;
    float level = 0.0f;
    float peak = 0.0f;
    QObject::connect(&model, &MeterModel::micMetersChanged,
                     [&seen, &level, &peak](float micLevel, float, float micPeak, float) {
        seen = true;
        level = micLevel;
        peak = micPeak;
    });

    model.updateValues({2, 1}, {rawDb(-35.0f), rawDb(-20.0f)});
    model.updateValues({27}, {rawDb(-18.0f)});

    report("AFTEREQ feeds displayed TX level when present",
           seen && model.hasRadioTxAudioLevelValue()
           && nearlyEqual(level, -18.0f)
           && nearlyEqual(peak, -18.0f));
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

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    testAdjacentMetersDoNotSynthesizeCompression();
    testCompPeakRequiresAfterEq();
    testAfterEqRequiresCompPeak();
    testAfterEqFeedsDisplayedMicLevel();
    testCompPeakMinusAfterEqDrivesGauge();
    testCompPeakAndAfterEqAreOrderIndependent();
    testNoStageLiftShowsNoCompression();
    testDerivedCompressionClampsToGaugeRange();
    testRemovingCompPeakMarksCompressionUnavailable();
    testRemovingAfterEqMarksCompressionUnavailable();

    return g_failed == 0 ? 0 : 1;
}
