#include "FrequencyEntryDialog.h"
#include "models/SliceModel.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QMouseEvent>
#include <QApplication>

#include <cmath>

namespace AetherSDR {

// ── Rotary VFO knob (drag to tune) ──────────────────────────────────────────

class VfoKnobWidget : public QWidget {
public:
    explicit VfoKnobWidget(QWidget* parent = nullptr) : QWidget(parent)
    {
        setFixedSize(180, 180);
        setCursor(Qt::OpenHandCursor);
    }

    void setAngle(double deg) { m_angle = deg; update(); }
    double angle() const { return m_angle; }

    // Returns accumulated step ticks since last call (positive = CW)
    int drainTicks()
    {
        int t = m_ticks;
        m_ticks = 0;
        return t;
    }

    std::function<void(int ticks)> onTick;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const int side = qMin(width(), height());
        const float r = side / 2.0f - 6;
        const QPointF c(width() / 2.0, height() / 2.0);

        // Outer ring
        p.setPen(QPen(QColor(0x40, 0x50, 0x60), 2));
        p.setBrush(QColor(0x14, 0x14, 0x28));
        p.drawEllipse(c, r, r);

        // Tick marks around the rim (every 30 degrees)
        p.setPen(QPen(QColor(0x50, 0x60, 0x70), 1));
        for (int i = 0; i < 12; ++i) {
            double a = i * 30.0 * M_PI / 180.0;
            QPointF outer(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
            QPointF inner(c.x() + (r - 8) * std::cos(a), c.y() + (r - 8) * std::sin(a));
            p.drawLine(outer, inner);
        }

        // Indicator line (shows current rotation)
        double aRad = m_angle * M_PI / 180.0 - M_PI / 2.0;
        QPointF tip(c.x() + (r - 14) * std::cos(aRad),
                    c.y() + (r - 14) * std::sin(aRad));
        p.setPen(QPen(QColor(0x00, 0xb4, 0xd8), 3));
        p.drawLine(c, tip);

        // Center cap
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0x20, 0x20, 0x38));
        p.drawEllipse(c, 12, 12);
    }

    void mousePressEvent(QMouseEvent* ev) override
    {
        if (ev->button() == Qt::LeftButton) {
            m_dragging = true;
            m_lastY = ev->position().y();
            setCursor(Qt::ClosedHandCursor);
            ev->accept();
        }
    }

    void mouseMoveEvent(QMouseEvent* ev) override
    {
        if (!m_dragging) return;
        double dy = m_lastY - ev->position().y(); // up = positive
        m_lastY = ev->position().y();

        m_accumulator += dy;
        // 20 pixels per tick — comfortable for both mouse and touch
        const double pixelsPerTick = 20.0;
        int ticks = static_cast<int>(m_accumulator / pixelsPerTick);
        if (ticks != 0) {
            m_accumulator -= ticks * pixelsPerTick;
            m_angle += ticks * 15.0; // visual rotation per tick
            m_ticks += ticks;
            update();
            if (onTick) onTick(ticks);
        }
        ev->accept();
    }

    void mouseReleaseEvent(QMouseEvent* ev) override
    {
        if (ev->button() == Qt::LeftButton) {
            m_dragging = false;
            m_accumulator = 0;
            setCursor(Qt::OpenHandCursor);
            ev->accept();
        }
    }

    void wheelEvent(QWheelEvent* ev) override
    {
        int raw = ev->angleDelta().y() / 120;
        int ticks = qBound(-3, raw, 3);
        if (ticks != 0) {
            m_angle += ticks * 15.0;
            m_ticks += ticks;
            update();
            if (onTick) onTick(ticks);
        }
        ev->accept();
    }

private:
    double m_angle{0};
    bool   m_dragging{false};
    double m_lastY{0};
    double m_accumulator{0};
    int    m_ticks{0};
};

// ── FrequencyEntryDialog ─────────────────────────────────────────────────────

FrequencyEntryDialog::FrequencyEntryDialog(SliceModel* slice, int stepHz,
                                           QWidget* parent)
    : QDialog(parent)
    , m_slice(slice)
    , m_stepHz(stepHz > 0 ? stepHz : 100)
    , m_liveFreqMhz(slice ? slice->frequency() : 14.0)
{
    setWindowTitle(QString("Tune Frequency – Slice %1")
        .arg(QChar('A' + (slice ? slice->sliceId() : 0))));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
    buildUI();
    updateDisplay();

    // Live-update when frequency changes externally (e.g. CAT)
    if (m_slice) {
        connect(m_slice, &SliceModel::frequencyChanged, this, [this](double mhz) {
            if (!m_keypadActive) {
                m_liveFreqMhz = mhz;
                updateDisplay();
            }
        });
    }
}

static QString formatFreqDisplay(double mhz)
{
    long long hz = static_cast<long long>(std::round(mhz * 1e6));
    int mhzPart = static_cast<int>(hz / 1000000);
    int khzPart = static_cast<int>((hz / 1000) % 1000);
    int hzPart  = static_cast<int>(hz % 1000);
    return QString("%1.%2.%3")
        .arg(mhzPart)
        .arg(khzPart, 3, 10, QChar('0'))
        .arg(hzPart, 3, 10, QChar('0'));
}

void FrequencyEntryDialog::buildUI()
{
    const QString panelStyle =
        "QDialog { background: #0f0f1a; }"
        "QLabel { color: #c8d8e8; background: transparent; }"
        "QPushButton { background: #1a1a2e; color: #c8d8e8; border: 1px solid #304060;"
        " border-radius: 4px; font-size: 16px; min-width: 48px; min-height: 44px; }"
        "QPushButton:hover { background: #252545; }"
        "QPushButton:pressed { background: #00b4d8; color: #0f0f1a; }";
    setStyleSheet(panelStyle);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(12);
    root->setContentsMargins(16, 16, 16, 16);

    // ── Frequency display ────────────────────────────────────────────────
    m_freqDisplay = new QLabel;
    m_freqDisplay->setAlignment(Qt::AlignCenter);
    m_freqDisplay->setStyleSheet(
        "QLabel { color: #00e5ff; font-size: 36px; font-weight: bold;"
        " background: #0a0a18; border: 1px solid #304060; border-radius: 6px;"
        " padding: 8px 16px; }");
    root->addWidget(m_freqDisplay);

    // ── Step size label ──────────────────────────────────────────────────
    auto* stepLabel = new QLabel;
    if (m_stepHz >= 1000)
        stepLabel->setText(QString("Step: %1 kHz").arg(m_stepHz / 1000.0, 0, 'g', 4));
    else
        stepLabel->setText(QString("Step: %1 Hz").arg(m_stepHz));
    stepLabel->setAlignment(Qt::AlignCenter);
    stepLabel->setStyleSheet("QLabel { color: #607090; font-size: 12px; }");
    root->addWidget(stepLabel);

    // ── VFO Knob ─────────────────────────────────────────────────────────
    auto* knobWidget = new VfoKnobWidget(this);
    knobWidget->onTick = [this](int ticks) {
        if (!m_slice) return;
        // If user was in keypad mode, exit and resume knob tuning from
        // whatever frequency was last applied
        if (m_keypadActive) {
            m_keypadActive = false;
            m_keypadText.clear();
        }
        double newMhz = m_liveFreqMhz + ticks * m_stepHz / 1.0e6;
        if (newMhz < 0.001) newMhz = 0.001;
        m_liveFreqMhz = newMhz;
        m_slice->setFrequency(newMhz);
        updateDisplay();
    };
    m_knob = knobWidget;

    auto* knobRow = new QHBoxLayout;
    knobRow->addStretch();
    knobRow->addWidget(m_knob);
    knobRow->addStretch();
    root->addLayout(knobRow);

    // ── Numeric keypad ───────────────────────────────────────────────────
    auto* keyGrid = new QGridLayout;
    keyGrid->setSpacing(4);

    auto makeBtn = [&](const QString& text) -> QPushButton* {
        auto* btn = new QPushButton(text);
        btn->setMinimumSize(52, 44);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return btn;
    };

    // 1-9
    for (int i = 1; i <= 9; ++i) {
        auto* btn = makeBtn(QString::number(i));
        keyGrid->addWidget(btn, (i - 1) / 3, (i - 1) % 3);
        connect(btn, &QPushButton::clicked, this, [this, i] {
            appendDigit(QString::number(i));
        });
    }
    // bottom row: . 0 ←
    auto* dotBtn = makeBtn(".");
    keyGrid->addWidget(dotBtn, 3, 0);
    connect(dotBtn, &QPushButton::clicked, this, [this] { appendDigit("."); });

    auto* zeroBtn = makeBtn("0");
    keyGrid->addWidget(zeroBtn, 3, 1);
    connect(zeroBtn, &QPushButton::clicked, this, [this] { appendDigit("0"); });

    auto* bsBtn = makeBtn(QChar(0x2190)); // ← arrow
    bsBtn->setStyleSheet(bsBtn->styleSheet() +
        "QPushButton { color: #ff6060; }");
    keyGrid->addWidget(bsBtn, 3, 2);
    connect(bsBtn, &QPushButton::clicked, this, [this] { backspace(); });

    // Clear button — full width row below keypad
    auto* clearBtn = makeBtn("CLR");
    clearBtn->setStyleSheet(clearBtn->styleSheet() +
        "QPushButton { color: #ffa040; font-size: 13px; min-height: 34px; }");
    keyGrid->addWidget(clearBtn, 4, 0, 1, 3);
    connect(clearBtn, &QPushButton::clicked, this, [this] {
        m_keypadActive = false;
        m_keypadText.clear();
        m_liveFreqMhz = m_slice ? m_slice->frequency() : m_liveFreqMhz;
        updateDisplay();
    });

    root->addLayout(keyGrid);

    // ── OK / Cancel buttons ──────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(8);

    m_cancelBtn = new QPushButton("Cancel");
    m_cancelBtn->setStyleSheet(
        "QPushButton { background: #2a1a1a; color: #ff6060; border: 1px solid #603030;"
        " border-radius: 4px; font-size: 15px; min-height: 40px; }");
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(m_cancelBtn);

    m_okBtn = new QPushButton("OK");
    m_okBtn->setStyleSheet(
        "QPushButton { background: #1a2a1a; color: #60ff60; border: 1px solid #306030;"
        " border-radius: 4px; font-size: 15px; font-weight: bold; min-height: 40px; }");
    m_okBtn->setDefault(true);
    connect(m_okBtn, &QPushButton::clicked, this, [this] {
        applyFrequency();
        accept();
    });
    btnRow->addWidget(m_okBtn);

    root->addLayout(btnRow);

    setFixedWidth(240);
}

void FrequencyEntryDialog::updateDisplay()
{
    if (m_keypadActive && !m_keypadText.isEmpty()) {
        // Show keypad entry with a cursor indicator
        m_freqDisplay->setText(m_keypadText + " MHz");
        m_freqDisplay->setStyleSheet(
            "QLabel { color: #ffcc00; font-size: 36px; font-weight: bold;"
            " background: #0a0a18; border: 1px solid #806000; border-radius: 6px;"
            " padding: 8px 16px; }");
    } else {
        m_freqDisplay->setText(formatFreqDisplay(m_liveFreqMhz));
        m_freqDisplay->setStyleSheet(
            "QLabel { color: #00e5ff; font-size: 36px; font-weight: bold;"
            " background: #0a0a18; border: 1px solid #304060; border-radius: 6px;"
            " padding: 8px 16px; }");
    }
}

void FrequencyEntryDialog::appendDigit(const QString& d)
{
    // Prevent multiple dots
    if (d == "." && m_keypadText.contains('.')) return;
    // Limit reasonable length (e.g. 450.123456)
    if (m_keypadText.length() >= 12) return;

    if (!m_keypadActive) {
        m_keypadActive = true;
        m_keypadText.clear();
    }
    m_keypadText += d;
    updateDisplay();
}

void FrequencyEntryDialog::backspace()
{
    if (!m_keypadActive || m_keypadText.isEmpty()) return;
    m_keypadText.chop(1);
    if (m_keypadText.isEmpty()) {
        m_keypadActive = false;
        m_liveFreqMhz = m_slice ? m_slice->frequency() : m_liveFreqMhz;
    }
    updateDisplay();
}

void FrequencyEntryDialog::applyFrequency()
{
    if (!m_slice) return;

    if (m_keypadActive && !m_keypadText.isEmpty()) {
        // Parse keypad input as MHz — same smart-parse logic as RxApplet
        QString clean = m_keypadText;
        int firstDot = clean.indexOf('.');
        if (firstDot >= 0)
            clean = clean.left(firstDot) + "." + clean.mid(firstDot + 1).remove('.');

        bool ok = false;
        double freqMhz = clean.toDouble(&ok);
        if (!ok) return;

        const bool onXvtr = m_slice->rxAntenna().startsWith("XVT")
                            || m_slice->frequency() > 54.0;
        const double maxMhz = onXvtr ? 450.0 : 54.0;

        if (onXvtr) {
            if (freqMhz > 450.0 && !clean.contains('.')) {
                int digits = clean.length();
                if (digits >= 4) {
                    clean.insert(3, '.');
                    freqMhz = clean.toDouble(&ok);
                    if (!ok) return;
                }
            }
        } else {
            if (freqMhz > 54000.0) freqMhz /= 1e6;
            else if (freqMhz > 54.0) freqMhz /= 1e3;
        }

        if (freqMhz >= 0.001 && freqMhz <= maxMhz)
            m_slice->tuneAndRecenter(freqMhz);
    }
    // If not in keypad mode, the knob has already been applying changes live
}

} // namespace AetherSDR
