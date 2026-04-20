#include "ClientDeEssApplet.h"
#include "ClientDeEssCurveWidget.h"
#include "MeterSmoother.h"
#include "core/AudioEngine.h"
#include "core/ClientDeEss.h"

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

// GR mini-strip for the de-esser.  File-scope so the applet can hold
// a typed pointer.  Max reduction ≤ 24 dB so scale the full bar to
// that range.
class ClientDeEssGrBar : public QWidget {
public:
    explicit ClientDeEssGrBar(QWidget* parent = nullptr) : QWidget(parent)
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
        constexpr float kMaxGr = 24.0f;
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
        constexpr float kMaxGr = 24.0f;
        const float frac = m_smooth.value();
        if (frac > 0.0f) {
            const float w = frac * r.width();
            QRectF fill(r.right() - w, r.top() + 1.0, w, r.height() - 2.0);
            p.fillRect(fill, QColor("#d47272"));   // soft red — matches curve
        }
        // -6 dB tick (typical amount)
        const float tickX = r.right() - (6.0f / kMaxGr) * r.width();
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

ClientDeEssApplet::ClientDeEssApplet(QWidget* parent) : QWidget(parent)
{
    buildUI();
    hide();
}

void ClientDeEssApplet::buildUI()
{
    setStyleSheet("QWidget { background: transparent; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    m_curve = new ClientDeEssCurveWidget;
    m_curve->setCompactMode(true);
    m_curve->setMinimumHeight(90);
    outer->addWidget(m_curve, 1);

    m_grBar = new ClientDeEssGrBar;
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
        m_edit->setToolTip("Open the de-esser editor");
        row->addWidget(m_edit);

        connect(m_enable, &QPushButton::toggled, this,
                &ClientDeEssApplet::onEnableToggled);
        connect(m_edit, &QPushButton::clicked, this, [this]() {
            emit editRequested();
        });

        outer->addLayout(row);
    }

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(33);
    connect(m_meterTimer, &QTimer::timeout, this, &ClientDeEssApplet::tickMeter);
}

void ClientDeEssApplet::setAudioEngine(AudioEngine* engine)
{
    m_audio = engine;
    if (!m_audio) return;
    m_curve->setDeEss(m_audio->clientDeEssTx());
    syncEnableFromEngine();
    m_meterTimer->start();
}

void ClientDeEssApplet::syncEnableFromEngine()
{
    if (!m_audio || !m_enable) return;
    ClientDeEss* d = m_audio->clientDeEssTx();
    QSignalBlocker b(m_enable);
    m_enable->setChecked(d && d->isEnabled());
    if (m_curve) m_curve->update();
}

void ClientDeEssApplet::refreshEnableFromEngine()
{
    syncEnableFromEngine();
}

void ClientDeEssApplet::onEnableToggled(bool on)
{
    if (!m_audio) return;
    ClientDeEss* d = m_audio->clientDeEssTx();
    if (!d) return;
    d->setEnabled(on);
    m_audio->saveClientDeEssSettings();
    if (m_curve) m_curve->update();
}

void ClientDeEssApplet::tickMeter()
{
    if (!m_audio || !m_grBar) return;
    ClientDeEss* d = m_audio->clientDeEssTx();
    if (!d) return;
    const float gr = d->gainReductionDb();
    m_grBar->setGrDb(gr);
    m_grDb = gr;
}

} // namespace AetherSDR
