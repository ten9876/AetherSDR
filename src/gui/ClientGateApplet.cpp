#include "ClientGateApplet.h"
#include "ClientGateCurveWidget.h"
#include "MeterSmoother.h"
#include "core/AudioEngine.h"
#include "core/ClientGate.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPushButton>
#include <QSignalBlocker>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>

namespace AetherSDR {

// Gain-reduction mini-strip for the applet.  Named at file scope (not
// an anonymous namespace) so ClientGateApplet.h can forward-declare it
// and keep a typed pointer.  Fill motion uses the shared MeterSmoother
// ballistics so it reads identically to the compressor GR strip.
class ClientGateGrBar : public QWidget {
public:
    explicit ClientGateGrBar(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(10);

        m_animTimer.setTimerType(Qt::PreciseTimer);
        m_animTimer.setInterval(kMeterSmootherIntervalMs);
        connect(&m_animTimer, &QTimer::timeout, this, [this]() {
            if (!m_smooth.tick(m_animElapsed.restart()))
                m_animTimer.stop();
            update();
        });
    }

    void setGrDb(float grDb)
    {
        if (std::fabs(grDb - m_grDb) < 0.05f) return;
        m_grDb = grDb;
        // Gate attenuation can be much deeper than comp — 40 dB scale
        // reads better for a gate tile.
        constexpr float kMaxGr = 40.0f;
        m_smooth.setTarget(std::clamp(-m_grDb, 0.0f, kMaxGr) / kMaxGr);
        if (!m_smooth.needsAnimation()) {
            if (m_animTimer.isActive()) m_animTimer.stop();
            update();
        } else if (!m_animTimer.isActive()) {
            m_animElapsed.restart();
            m_animTimer.start();
        }
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        const QRectF r = rect();
        p.fillRect(r, QColor("#0a1420"));
        constexpr float kMaxGr = 40.0f;
        const float frac = m_smooth.value();
        if (frac > 0.0f) {
            const float w = frac * r.width();
            QRectF fill(r.right() - w, r.top() + 1.0, w, r.height() - 2.0);
            p.fillRect(fill, QColor("#c8a040"));   // amber — matches curve
        }
        // -15 dB tick (soft-expander default floor)
        const float tickX = r.right() - (15.0f / kMaxGr) * r.width();
        p.setPen(QPen(QColor("#2a4458"), 1.0));
        p.drawLine(QPointF(tickX, r.top()), QPointF(tickX, r.bottom()));
    }

private:
    float         m_grDb{0.0f};
    MeterSmoother m_smooth;
    QTimer        m_animTimer;
    QElapsedTimer m_animElapsed;
};

namespace {

const QString kEnableStyle =
    "QPushButton {"
    "  background: #1a2a3a; border: 1px solid #205070; border-radius: 3px;"
    "  color: #c8d8e8; font-size: 11px; font-weight: bold; padding: 2px 10px;"
    "}"
    "QPushButton:hover { background: #204060; }"
    "QPushButton:checked {"
    "  background: #006040; color: #00ff88; border: 1px solid #00a060;"
    "}";

constexpr const char* kEditStyle =
    "QPushButton {"
    "  background: #1a2a3a; border: 1px solid #205070; border-radius: 3px;"
    "  color: #c8d8e8; font-size: 11px; padding: 2px 10px;"
    "}"
    "QPushButton:hover { background: #204060; }";

} // namespace

ClientGateApplet::ClientGateApplet(QWidget* parent) : QWidget(parent)
{
    buildUI();
    hide();
}

void ClientGateApplet::buildUI()
{
    setStyleSheet("QWidget { background: transparent; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    m_curve = new ClientGateCurveWidget;
    m_curve->setCompactMode(true);
    m_curve->setMinimumHeight(90);
    outer->addWidget(m_curve, 1);

    m_grBar = new ClientGateGrBar;
    outer->addWidget(m_grBar);

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        m_enable = new QPushButton("Enable");
        m_enable->setCheckable(true);
        m_enable->setStyleSheet(kEnableStyle);
        m_enable->setFixedHeight(22);
        row->addWidget(m_enable);

        row->addStretch();

        m_edit = new QPushButton("Edit…");
        m_edit->setStyleSheet(kEditStyle);
        m_edit->setFixedHeight(22);
        m_edit->setToolTip("Open the client gate editor");
        row->addWidget(m_edit);

        connect(m_enable, &QPushButton::toggled, this,
                &ClientGateApplet::onEnableToggled);
        connect(m_edit, &QPushButton::clicked, this, [this]() {
            emit editRequested();
        });

        outer->addLayout(row);
    }

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(33);
    connect(m_meterTimer, &QTimer::timeout, this, &ClientGateApplet::tickMeter);
}

void ClientGateApplet::setAudioEngine(AudioEngine* engine)
{
    m_audio = engine;
    if (!m_audio) return;
    m_curve->setGate(m_audio->clientGateTx());
    syncEnableFromEngine();
    m_meterTimer->start();
}

void ClientGateApplet::syncEnableFromEngine()
{
    if (!m_audio || !m_enable) return;
    ClientGate* g = m_audio->clientGateTx();
    QSignalBlocker b(m_enable);
    m_enable->setChecked(g && g->isEnabled());
    if (m_curve) m_curve->update();
}

void ClientGateApplet::refreshEnableFromEngine()
{
    syncEnableFromEngine();
}

void ClientGateApplet::onEnableToggled(bool on)
{
    if (!m_audio) return;
    ClientGate* g = m_audio->clientGateTx();
    if (!g) return;
    g->setEnabled(on);
    m_audio->saveClientGateSettings();
    if (m_curve) m_curve->update();
}

void ClientGateApplet::tickMeter()
{
    if (!m_audio || !m_grBar) return;
    ClientGate* g = m_audio->clientGateTx();
    if (!g) return;
    const float gr = g->gainReductionDb();
    m_grBar->setGrDb(gr);
    m_grDb = gr;
}

} // namespace AetherSDR
