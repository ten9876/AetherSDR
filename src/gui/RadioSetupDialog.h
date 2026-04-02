#pragma once

#include <QDialog>

class QTabWidget;
class QLabel;
class QLineEdit;
class QGroupBox;
class QProgressBar;
class QPushButton;

namespace AetherSDR {

class RadioModel;
class AudioEngine;
class FirmwareUploader;
class FirmwareStager;

// Radio Setup dialog — tabbed configuration window matching SmartSDR's
// Settings → Radio Setup. Shows radio info, GPS, TX, RX, filters, etc.
class RadioSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit RadioSetupDialog(RadioModel* model, AudioEngine* audio = nullptr,
                              QWidget* parent = nullptr);
    void selectTab(const QString& tabName);

private:
    QWidget* buildRadioTab();
    QWidget* buildNetworkTab();
    QGroupBox* buildIpConfigGroup();
    QWidget* buildGpsTab();
    QWidget* buildTxTab();
    QWidget* buildPhoneCwTab();
    QWidget* buildRxTab();
    QWidget* buildAudioTab();
    QWidget* buildFiltersTab();
    QWidget* buildXvtrTab();
    QWidget* buildUsbCablesTab();
#ifdef HAVE_SERIALPORT
    QWidget* buildSerialTab();
#endif

    RadioModel*  m_model;
    AudioEngine* m_audio{nullptr};
    QTabWidget*  m_tabs{nullptr};

    // Radio tab fields
    QLabel* m_serialLabel{nullptr};
    QLabel* m_hwVersionLabel{nullptr};
    QLabel* m_regionLabel{nullptr};
    QLabel* m_optionsLabel{nullptr};
    QLabel* m_remoteOnLabel{nullptr};
    QLabel* m_modelLabel{nullptr};
    QLineEdit* m_nicknameEdit{nullptr};
    QLineEdit* m_callsignEdit{nullptr};
    QPushButton* m_remoteOnBtn{nullptr};

    // Firmware update
    QLabel*       m_fwStatusLabel{nullptr};
    QProgressBar* m_fwProgress{nullptr};
    QPushButton*  m_fwUploadBtn{nullptr};
    QString       m_fwFilePath;
    FirmwareUploader* m_uploader{nullptr};
    FirmwareStager*   m_stager{nullptr};
};

} // namespace AetherSDR
