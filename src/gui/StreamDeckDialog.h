#pragma once
#ifdef HAVE_HIDAPI

#include <QDialog>
#include <QComboBox>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QGridLayout>
#include <QVector>
#include <QImage>

namespace AetherSDR {

class StreamDeckManager;
struct StreamDeckDeviceInfo;

class StreamDeckDialog : public QDialog {
    Q_OBJECT

public:
    explicit StreamDeckDialog(StreamDeckManager* manager, QWidget* parent = nullptr);

private slots:
    void onDeviceConnected(const QString& serial, const QString& model,
                            int keys, int cols, int rows, int dials);
    void onDeviceDisconnected(const QString& serial);
    void onBrightnessChanged(int value);

private:
    void buildUI();
    void refreshDeviceList();
    void buildGrid(int rows, int cols, int keySize);
    void updateGridPreviews();

    StreamDeckManager* m_manager;

    QComboBox*    m_deviceCombo{nullptr};
    QLabel*       m_infoLabel{nullptr};
    QSlider*      m_brightnessSlider{nullptr};
    QLabel*       m_brightnessLabel{nullptr};
    QPushButton*  m_refreshBtn{nullptr};
    QWidget*      m_gridContainer{nullptr};
    QGridLayout*  m_gridLayout{nullptr};
    QLabel*       m_dialLabel{nullptr};

    struct KeyButton {
        QPushButton* btn{nullptr};
        int          index{0};
    };
    QVector<KeyButton> m_keyButtons;

    QString m_selectedSerial;
    int m_gridRows{0};
    int m_gridCols{0};
    QTimer* m_brightnessTimer{nullptr};
};

} // namespace AetherSDR
#endif
