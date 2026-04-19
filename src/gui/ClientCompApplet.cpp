#include "ClientCompApplet.h"
#include "ClientCompCurveWidget.h"
#include "core/AudioEngine.h"
#include "core/ClientComp.h"

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

// Gain-reduction mini-strip used by the applet.  Named at file scope
// (not in an anonymous namespace) so ClientCompApplet.h can forward-
// declare it and keep a typed pointer.  Fill animates with the same
// asymmetric attack/release ballistics as HGauge so the motion matches
// every other metering surface in the app (Phone/CW Level, etc.).
class ClientCompGrBar : public QWidget {
public:
    explicit ClientCompGrBar(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedHeight(10);

        m_animTimer.setTimerType(Qt::PreciseTimer);
        m_animTimer.setInterval(8);
        connect(&m_animTimer, &QTimer::timeout, this, [this]() {
            const qint64 ms = m_animElapsed.restart();
            if (ms <= 0) return;
            const float delta = m_targetFrac - m_displayFrac;
            if (std::fabs(delta) <= 0.001f) {
                m_displayFrac = m_targetFrac;
                m_animTimer.stop();
            } else {
                const float tau = (delta >= 0.0f) ? 0.030f : 0.180f;
                const float alpha = 1.0f - std::exp(
                    -static_cast<float>(ms) / 1000.0f / tau);
                m_displayFrac += delta * alpha;
            }
            update();
        });
    }

    void setGrDb(float grDb)
    {
        if (std::fabs(grDb - m_grDb) < 0.05f) return;
        m_grDb = grDb;
        constexpr float kMaxGr = 20.0f;
        m_targetFrac = std::clamp(-m_grDb, 0.0f, kMaxGr) / kMaxGr;
        if (std::fabs(m_targetFrac - m_displayFrac) <= 0.001f) {
            m_displayFrac = m_targetFrac;
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
        constexpr float kMaxGr = 20.0f;
        if (m_displayFrac > 0.0f) {
            const float w = m_displayFrac * r.width();
            QRectF fill(r.right() - w, r.top() + 1.0, w, r.height() - 2.0);
            p.fillRect(fill, QColor("#e8a540"));
        }
        const float tickX = r.right() - (6.0f / kMaxGr) * r.width();
        p.setPen(QPen(QColor("#2a4458"), 1.0));
        p.drawLine(QPointF(tickX, r.top()), QPointF(tickX, r.bottom()));
    }

private:
    float m_grDb{0.0f};
    QTimer        m_animTimer;
    QElapsedTimer m_animElapsed;
    float         m_displayFrac{0.0f};
    float         m_targetFrac{0.0f};
};

namespace {

// Enable/Bypass toggle — matches ClientEqApplet's style so the two
// applets sit visually side-by-side with identical button language.
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

ClientCompApplet::ClientCompApplet(QWidget* parent) : QWidget(parent)
{
    buildUI();
    hide();  // hidden until toggled on from the button tray
}

void ClientCompApplet::buildUI()
{
    setStyleSheet("QWidget { background: transparent; }");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(4, 4, 4, 4);
    outer->setSpacing(4);

    m_curve = new ClientCompCurveWidget;
    m_curve->setCompactMode(true);
    m_curve->setMinimumHeight(90);
    outer->addWidget(m_curve, 1);

    m_grBar = new ClientCompGrBar;
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
        m_edit->setToolTip("Open the client compressor editor");
        row->addWidget(m_edit);

        connect(m_enable, &QPushButton::toggled, this,
                &ClientCompApplet::onEnableToggled);
        connect(m_edit, &QPushButton::clicked, this, [this]() {
            emit editRequested();
        });

        outer->addLayout(row);
    }

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(33);
    connect(m_meterTimer, &QTimer::timeout, this, &ClientCompApplet::tickMeter);
}

void ClientCompApplet::setAudioEngine(AudioEngine* engine)
{
    m_audio = engine;
    if (!m_audio) return;
    m_curve->setComp(m_audio->clientCompTx());
    syncEnableFromEngine();
    m_meterTimer->start();
}

void ClientCompApplet::syncEnableFromEngine()
{
    if (!m_audio || !m_enable) return;
    ClientComp* c = m_audio->clientCompTx();
    QSignalBlocker b(m_enable);
    m_enable->setChecked(c && c->isEnabled());
    if (m_curve) m_curve->update();
}

void ClientCompApplet::refreshEnableFromEngine()
{
    syncEnableFromEngine();
}

void ClientCompApplet::onEnableToggled(bool on)
{
    if (!m_audio) return;
    ClientComp* c = m_audio->clientCompTx();
    if (!c) return;
    c->setEnabled(on);
    m_audio->saveClientCompSettings();
    if (m_curve) m_curve->update();
}

void ClientCompApplet::tickMeter()
{
    if (!m_audio || !m_grBar) return;
    ClientComp* c = m_audio->clientCompTx();
    if (!c) return;
    const float gr = c->gainReductionDb();
    m_grBar->setGrDb(gr);
    m_grDb = gr;
}

} // namespace AetherSDR
