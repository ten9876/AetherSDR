#include "StripRxOutputPanel.h"

#include "EditorFramelessTitleBar.h"
#include "core/AudioEngine.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

// Same dark panel chrome as the other strip cells — frameless title
// bar on transparent background, dark inner fill.
constexpr const char* kWindowStyle =
    "AetherSDR--StripRxOutputPanel { background: #0e1b28;"
    " border: 1px solid #2a3a4a; border-radius: 4px; }";

// dBFS → 0..1 fraction across the meter using a fixed -60..0 range.
float dbToFrac(float db)
{
    if (db < -60.0f) return 0.0f;
    if (db > 0.0f)   return 1.0f;
    return (db + 60.0f) / 60.0f;
}

// Horizontal peak / RMS bar — single child widget that draws itself
// from m_peakDb / m_rmsDb on the parent.  No internal state.
class PeakBar : public QWidget {
public:
    PeakBar(const float& peak, const float& rms, QWidget* parent)
        : QWidget(parent), m_peak(peak), m_rms(rms)
    {
        setMinimumHeight(14);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, false);
        const QRectF r = rect();

        // Background trough.
        p.setPen(QPen(QColor("#2a3a4a"), 1));
        p.setBrush(QColor("#0a141e"));
        p.drawRect(r.adjusted(0.5, 0.5, -0.5, -0.5));

        // Three-zone fill: green / amber / red bands at fixed dB
        // breakpoints (-60..-12 green, -12..-3 amber, -3..0 red).
        const float fracPeak = dbToFrac(m_peak);
        const QRectF inner = r.adjusted(1, 1, -1, -1);
        const qreal w = inner.width() * fracPeak;
        if (w > 0.0) {
            const qreal greenEnd = inner.width() * dbToFrac(-12.0f);
            const qreal amberEnd = inner.width() * dbToFrac(-3.0f);
            QRectF fill(inner.left(), inner.top(), w, inner.height());
            p.setPen(Qt::NoPen);
            // Green band.
            p.setBrush(QColor("#3a8a4a"));
            p.drawRect(QRectF(fill.left(), fill.top(),
                              std::min<qreal>(w, greenEnd), fill.height()));
            // Amber band.
            if (w > greenEnd) {
                p.setBrush(QColor("#c89030"));
                p.drawRect(QRectF(fill.left() + greenEnd, fill.top(),
                                  std::min<qreal>(w - greenEnd,
                                                  amberEnd - greenEnd),
                                  fill.height()));
            }
            // Red band.
            if (w > amberEnd) {
                p.setBrush(QColor("#c83030"));
                p.drawRect(QRectF(fill.left() + amberEnd, fill.top(),
                                  w - amberEnd, fill.height()));
            }
        }

        // RMS tick — vertical line over the peak bar at the RMS dBFS
        // position.  Always drawn so the user can see the relationship
        // between peak (instantaneous) and RMS (moving average).
        const float fracRms = dbToFrac(m_rms);
        if (fracRms > 0.0f) {
            const qreal x = inner.left() + inner.width() * fracRms;
            p.setPen(QPen(QColor("#e0e0e0"), 1.5));
            p.drawLine(QPointF(x, inner.top()),
                       QPointF(x, inner.bottom()));
        }
    }

private:
    const float& m_peak;
    const float& m_rms;
};

constexpr int kDecayIntervalMs = 33;       // ~30 Hz repaint
constexpr float kDecayPerTickDb = 1.5f;    // peak falls 1.5 dB per tick

} // namespace

namespace AetherSDR {

StripRxOutputPanel::StripRxOutputPanel(AudioEngine* engine, QWidget* parent)
    : QWidget(parent)
    , m_audio(engine)
{
    const QString title = QString::fromUtf8("Aetherial Output \xe2\x80\x94 RX");
    setWindowTitle(title);
    setStyleSheet(kWindowStyle);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 0, 8, 8);
    root->setSpacing(6);

    auto* titleBar = new EditorFramelessTitleBar;
    titleBar->setTitleText(title);
    titleBar->setControlsVisible(false);
    titleBar->setStyleSheet("background: transparent;");
    m_titleBar = titleBar;
    root->addWidget(titleBar);

    // Peak / RMS readout row.
    auto* readoutRow = new QHBoxLayout;
    readoutRow->setContentsMargins(0, 0, 0, 0);
    readoutRow->setSpacing(8);

    auto makeReadout = [&](const QString& label) {
        auto* col = new QVBoxLayout;
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(0);
        auto* hdr = new QLabel(label, this);
        hdr->setStyleSheet(
            "QLabel { background: transparent; color: #8aa8c0;"
            " font-size: 10px; font-weight: bold; }");
        hdr->setAlignment(Qt::AlignCenter);
        auto* val = new QLabel("-∞", this);
        val->setStyleSheet(
            "QLabel { background: transparent; color: #c8d8e8;"
            " font-size: 13px; font-weight: bold; }");
        val->setAlignment(Qt::AlignCenter);
        val->setMinimumWidth(56);
        col->addWidget(hdr);
        col->addWidget(val);
        readoutRow->addLayout(col);
        return val;
    };
    m_peakLbl = makeReadout(QStringLiteral("PK"));
    m_rmsLbl  = makeReadout(QStringLiteral("RMS"));

    readoutRow->addStretch(1);

    // MUTE / BOOST toggles on the right.
    const QString btnStyle =
        "QPushButton { background: #1a2a3a; border: 1px solid #2a4458;"
        "  border-radius: 3px; color: #c8d8e8;"
        "  font-size: 11px; font-weight: bold; padding: 3px 10px; }"
        "QPushButton:hover { background: #243a52; border-color: #4a7090; }"
        "QPushButton:checked { background: #c83030; color: #ffffff;"
        "  border-color: #ff5050; }";
    const QString boostStyle =
        "QPushButton { background: #1a2a3a; border: 1px solid #2a4458;"
        "  border-radius: 3px; color: #c8d8e8;"
        "  font-size: 11px; font-weight: bold; padding: 3px 10px; }"
        "QPushButton:hover { background: #243a52; border-color: #4a7090; }"
        "QPushButton:checked { background: #1a6030; color: #ffffff;"
        "  border-color: #20a040; }";

    m_muteBtn = new QPushButton(QStringLiteral("MUTE"), this);
    m_muteBtn->setCheckable(true);
    m_muteBtn->setStyleSheet(btnStyle);
    m_muteBtn->setToolTip(tr("Mute local audio output."));
    if (m_audio) m_muteBtn->setChecked(m_audio->isMuted());
    connect(m_muteBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_audio) m_audio->setMuted(on);
    });
    readoutRow->addWidget(m_muteBtn);

    m_boostBtn = new QPushButton(QStringLiteral("BOOST"), this);
    m_boostBtn->setCheckable(true);
    m_boostBtn->setStyleSheet(boostStyle);
    m_boostBtn->setToolTip(
        tr("Soft-knee tanh boost on the RX output (~2× gain on quiet "
           "passages, no hard clipping on loud ones)."));
    if (m_audio) m_boostBtn->setChecked(m_audio->rxBoost());
    connect(m_boostBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_audio) m_audio->setRxBoost(on);
    });
    readoutRow->addWidget(m_boostBtn);

    root->addLayout(readoutRow);

    // Peak meter bar.
    m_meter = new PeakBar(m_peakDb, m_rmsDb, this);
    root->addWidget(m_meter);

    // Wire scope-tap → meter.  scopeSamplesReady fires for both sides;
    // RX path filters tx=false.  Cross-thread queued connection — the
    // signal is emitted from the audio thread.
    if (m_audio) {
        connect(m_audio, &AudioEngine::scopeSamplesReady,
                this, &StripRxOutputPanel::onScopeSamples,
                Qt::QueuedConnection);
    }

    // Decay timer keeps the bar alive between scope updates and gives
    // the peak indicator a smooth fall when audio drops.
    m_decayTimer = new QTimer(this);
    m_decayTimer->setInterval(kDecayIntervalMs);
    connect(m_decayTimer, &QTimer::timeout,
            this, &StripRxOutputPanel::tick);
    m_decayTimer->start();
}

StripRxOutputPanel::~StripRxOutputPanel() = default;

void StripRxOutputPanel::showForRx()
{
    if (m_audio) {
        if (m_muteBtn) {
            QSignalBlocker b(m_muteBtn);
            m_muteBtn->setChecked(m_audio->isMuted());
        }
        if (m_boostBtn) {
            QSignalBlocker b(m_boostBtn);
            m_boostBtn->setChecked(m_audio->rxBoost());
        }
    }
    show();
    raise();
    activateWindow();
}

void StripRxOutputPanel::syncControlsFromEngine()
{
    if (!m_audio) return;
    if (m_muteBtn) {
        QSignalBlocker b(m_muteBtn);
        m_muteBtn->setChecked(m_audio->isMuted());
    }
    if (m_boostBtn) {
        QSignalBlocker b(m_boostBtn);
        m_boostBtn->setChecked(m_audio->rxBoost());
    }
}

void StripRxOutputPanel::onScopeSamples(const QByteArray& monoFloat32,
                                       int /*sampleRate*/, bool tx)
{
    if (tx) return;  // RX-only meter

    const int n = monoFloat32.size() / static_cast<int>(sizeof(float));
    if (n <= 0) return;

    const auto* samples = reinterpret_cast<const float*>(monoFloat32.constData());
    float blockPeak = 0.0f;
    double sumSq = 0.0;
    for (int i = 0; i < n; ++i) {
        const float a = std::fabs(samples[i]);
        if (a > blockPeak) blockPeak = a;
        sumSq += static_cast<double>(samples[i]) * samples[i];
    }
    const float blockRms = static_cast<float>(std::sqrt(sumSq / n));
    constexpr float kFloor = -120.0f;
    const float blockPeakDb = blockPeak > 0.0f
        ? 20.0f * std::log10(blockPeak) : kFloor;
    const float blockRmsDb  = blockRms > 0.0f
        ? 20.0f * std::log10(blockRms) : kFloor;

    // Peak holds the higher of stored vs. block — decay is in tick().
    if (blockPeakDb > m_peakDb) m_peakDb = blockPeakDb;
    // RMS uses a one-pole smoother so the readout doesn't jitter.
    constexpr float kRmsAlpha = 0.4f;
    m_rmsDb = kRmsAlpha * blockRmsDb + (1.0f - kRmsAlpha) * m_rmsDb;
}

void StripRxOutputPanel::tick()
{
    // Decay the peak indicator at ~kDecayPerTickDb per tick.  RMS
    // tracks via the smoother so it doesn't need decay here.
    constexpr float kFloor = -120.0f;
    if (m_peakDb > kFloor) {
        m_peakDb -= kDecayPerTickDb;
        if (m_peakDb < kFloor) m_peakDb = kFloor;
    }

    // Update text readouts and repaint the bar.
    auto fmt = [](float db) -> QString {
        if (db <= -60.0f) return QStringLiteral("-∞");
        return QString::number(db, 'f', 1) + QStringLiteral(" dB");
    };
    if (m_peakLbl) m_peakLbl->setText(fmt(m_peakDb));
    if (m_rmsLbl)  m_rmsLbl->setText(fmt(m_rmsDb));
    if (m_meter)   m_meter->update();
}

} // namespace AetherSDR
