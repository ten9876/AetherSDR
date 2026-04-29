#include "WaveApplet.h"

#include "ComboStyle.h"
#include "GuardedSlider.h"
#include "WaveformWidget.h"
#include "core/AppSettings.h"

#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace AetherSDR {

namespace {

constexpr int kMinZoomPercent = 100;
constexpr int kMaxZoomPercent = 600;
constexpr int kDefaultZoomPercent = 170;
constexpr int kMinFps = 5;
constexpr int kMaxFps = 30;
constexpr int kDefaultFps = 24;

const QString kSliderStyle =
    "QSlider::groove:horizontal { height: 4px; background: #203040; border-radius: 2px; }"
    "QSlider::sub-page:horizontal { background: #00b4d8; border-radius: 2px; }"
    "QSlider::handle:horizontal { width: 12px; height: 12px; margin: -4px 0; "
    "background: #c8d8e8; border: 1px solid #00b4d8; border-radius: 6px; }";

QLabel* makeSettingLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setFixedWidth(62);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setStyleSheet("QLabel { color: #8aa8c0; font-size: 10px; }");
    return label;
}

WaveformWidget::ViewMode viewModeFromSetting(const QString& value)
{
    if (value.compare("Bands", Qt::CaseInsensitive) == 0)
        return WaveformWidget::ViewMode::VerticalBars;
    if (value.compare("History", Qt::CaseInsensitive) == 0)
        return WaveformWidget::ViewMode::Bars;
    if (value.compare("Envelope", Qt::CaseInsensitive) == 0)
        return WaveformWidget::ViewMode::Envelope;
    return WaveformWidget::ViewMode::Graph;
}

QString settingForViewMode(WaveformWidget::ViewMode mode)
{
    switch (mode) {
    case WaveformWidget::ViewMode::Envelope:
        return QStringLiteral("Envelope");
    case WaveformWidget::ViewMode::Bars:
        return QStringLiteral("History");
    case WaveformWidget::ViewMode::VerticalBars:
        return QStringLiteral("Bands");
    case WaveformWidget::ViewMode::Graph:
        return QStringLiteral("Graph");
    }
    return QStringLiteral("Graph");
}

} // namespace

WaveApplet::WaveApplet(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(3);

    m_waveform = new WaveformWidget(this);
    layout->addWidget(m_waveform);

    buildSettingsDrawer();
    layout->addWidget(m_settingsDrawer);

    auto& settings = AppSettings::instance();

    const auto viewMode = viewModeFromSetting(
        settings.value("WaveApplet_ViewMode", "Graph").toString());
    m_waveform->setViewMode(viewMode);
    if (m_viewCombo) {
        const int idx = m_viewCombo->findData(settingForViewMode(viewMode));
        QSignalBlocker block(m_viewCombo);
        m_viewCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    }

    const int zoomPercent = std::clamp(
        settings.value("WaveApplet_ZoomPercent", kDefaultZoomPercent).toInt(),
        kMinZoomPercent,
        kMaxZoomPercent);
    {
        QSignalBlocker block(m_zoomSlider);
        m_zoomSlider->setValue(zoomPercent);
    }
    m_waveform->setAmplitudeZoom(zoomPercent / 100.0f);
    updateZoomLabel();

    const int refreshRate = std::clamp(
        settings.value("WaveApplet_RefreshRateHz", kDefaultFps).toInt(),
        kMinFps,
        kMaxFps);
    {
        QSignalBlocker block(m_refreshSlider);
        m_refreshSlider->setValue(refreshRate);
    }
    m_waveform->setRefreshRateHz(refreshRate);
    updateRefreshLabel();

    setSettingsExpanded(true);
    setMinimumHeight(minimumSizeHint().height());

    connect(m_waveform, &WaveformWidget::settingsDrawerToggleRequested,
            this, [this]() {
        setSettingsExpanded(m_settingsDrawer->isHidden());
    });
}

QSize WaveApplet::sizeHint() const
{
    const int waveformHeight = m_waveform ? m_waveform->sizeHint().height() : 160;
    const int drawerHeight = (m_settingsDrawer && !m_settingsDrawer->isHidden())
        ? m_settingsDrawer->sizeHint().height() + 3
        : 0;
    return {240, std::max(165, waveformHeight + drawerHeight + 4)};
}

QSize WaveApplet::minimumSizeHint() const
{
    const int waveformHeight = m_waveform ? m_waveform->minimumSizeHint().height() : 110;
    const int drawerHeight = (m_settingsDrawer && !m_settingsDrawer->isHidden())
        ? m_settingsDrawer->minimumSizeHint().height() + 3
        : 0;
    return {220, std::max(120, waveformHeight + drawerHeight + 4)};
}

void WaveApplet::buildSettingsDrawer()
{
    m_settingsDrawer = new QFrame(this);
    m_settingsDrawer->setObjectName("WaveSettingsDrawer");
    m_settingsDrawer->setStyleSheet(
        "QFrame#WaveSettingsDrawer { background-color: #0f1a24; "
        "border: 1px solid #20384d; border-radius: 3px; }");

    auto* drawer = new QVBoxLayout(m_settingsDrawer);
    drawer->setContentsMargins(5, 4, 5, 5);
    drawer->setSpacing(4);

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(5);
        row->addWidget(makeSettingLabel("View:", m_settingsDrawer));

        m_viewCombo = new GuardedComboBox(m_settingsDrawer);
        applyComboStyle(m_viewCombo);
        m_viewCombo->setFixedHeight(20);
        m_viewCombo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        m_viewCombo->setToolTip("Choose the WAVE visualization");
        m_viewCombo->addItem("Scope", QStringLiteral("Graph"));
        m_viewCombo->addItem("Envelope", QStringLiteral("Envelope"));
        m_viewCombo->addItem("History", QStringLiteral("History"));
        m_viewCombo->addItem("Bands", QStringLiteral("Bands"));
        row->addWidget(m_viewCombo, 1);

        connect(m_viewCombo, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int index) {
            if (index < 0)
                return;
            const auto mode = viewModeFromSetting(m_viewCombo->itemData(index).toString());
            m_waveform->setViewMode(mode);
            auto& settings = AppSettings::instance();
            settings.setValue("WaveApplet_ViewMode", settingForViewMode(mode));
            settings.save();
        });

        drawer->addLayout(row);
    }

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(5);
        row->addWidget(makeSettingLabel("Zoom:", m_settingsDrawer));

        m_zoomSlider = new GuardedSlider(Qt::Horizontal, m_settingsDrawer);
        m_zoomSlider->setRange(kMinZoomPercent, kMaxZoomPercent);
        m_zoomSlider->setSingleStep(10);
        m_zoomSlider->setPageStep(50);
        m_zoomSlider->setTickInterval(100);
        m_zoomSlider->setTickPosition(QSlider::NoTicks);
        m_zoomSlider->setStyleSheet(kSliderStyle);
        row->addWidget(m_zoomSlider, 1);

        m_zoomValue = new QLabel(m_settingsDrawer);
        m_zoomValue->setFixedWidth(38);
        m_zoomValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_zoomValue->setStyleSheet("QLabel { color: #c8d8e8; font-size: 10px; }");
        row->addWidget(m_zoomValue);

        connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int value) {
            m_waveform->setAmplitudeZoom(value / 100.0f);
            updateZoomLabel();
            auto& settings = AppSettings::instance();
            settings.setValue("WaveApplet_ZoomPercent", value);
            settings.save();
        });

        drawer->addLayout(row);
    }

    {
        auto* row = new QHBoxLayout;
        row->setSpacing(5);
        row->addWidget(makeSettingLabel("FPS:", m_settingsDrawer));

        m_refreshSlider = new GuardedSlider(Qt::Horizontal, m_settingsDrawer);
        m_refreshSlider->setRange(kMinFps, kMaxFps);
        m_refreshSlider->setSingleStep(5);
        m_refreshSlider->setPageStep(10);
        m_refreshSlider->setTickInterval(5);
        m_refreshSlider->setTickPosition(QSlider::NoTicks);
        m_refreshSlider->setStyleSheet(kSliderStyle);
        row->addWidget(m_refreshSlider, 1);

        m_refreshValue = new QLabel(m_settingsDrawer);
        m_refreshValue->setFixedWidth(38);
        m_refreshValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_refreshValue->setStyleSheet("QLabel { color: #c8d8e8; font-size: 10px; }");
        row->addWidget(m_refreshValue);

        connect(m_refreshSlider, &QSlider::valueChanged, this, [this](int value) {
            m_waveform->setRefreshRateHz(value);
            updateRefreshLabel();
            auto& settings = AppSettings::instance();
            settings.setValue("WaveApplet_RefreshRateHz", value);
            settings.save();
        });

        drawer->addLayout(row);
    }
}

void WaveApplet::setSettingsExpanded(bool expanded)
{
    if (!m_settingsDrawer)
        return;

    const bool currentlyExpanded = !m_settingsDrawer->isHidden();
    if (currentlyExpanded == expanded)
        return;

    m_settingsDrawer->setVisible(expanded);
    setMinimumHeight(minimumSizeHint().height());
    updateGeometry();
    adjustSize();
    if (auto* p = parentWidget())
        p->updateGeometry();
}

void WaveApplet::updateZoomLabel()
{
    if (!m_zoomSlider || !m_zoomValue)
        return;
    m_zoomValue->setText(QStringLiteral("%1x").arg(m_zoomSlider->value() / 100.0, 0, 'f', 1));
}

void WaveApplet::updateRefreshLabel()
{
    if (!m_refreshSlider || !m_refreshValue)
        return;
    m_refreshValue->setText(QStringLiteral("%1 fps").arg(m_refreshSlider->value()));
}

void WaveApplet::appendScopeSamples(const QByteArray& monoFloat32Pcm,
                                    int sampleRate,
                                    bool tx)
{
    if (m_waveform)
        m_waveform->appendScopeSamples(monoFloat32Pcm, sampleRate, tx);
}

void WaveApplet::setTransmitting(bool tx)
{
    if (m_waveform)
        m_waveform->setTransmitting(tx);
}

} // namespace AetherSDR
