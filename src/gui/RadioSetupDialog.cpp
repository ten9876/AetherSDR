#include "RadioSetupDialog.h"
#include "ComboStyle.h"
#include "models/RadioModel.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#ifdef HAVE_SERIALPORT
#include "core/SerialPortController.h"
#include "core/FlexControlManager.h"
#include <QSerialPortInfo>
#endif
#include "core/FirmwareUploader.h"
#include "core/FirmwareStager.h"

#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QSpinBox>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QTimer>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressBar>
#include <QProcess>
#include <QListWidget>
#include <QStackedWidget>
#include <QPlainTextEdit>
#include <QSplitter>

namespace AetherSDR {

static const QString kGroupStyle =
    "QGroupBox { border: 1px solid #304050; border-radius: 4px; "
    "margin-top: 8px; padding-top: 12px; font-weight: bold; color: #8aa8c0; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; "
    "padding: 0 4px; }";

static const QString kLabelStyle =
    "QLabel { color: #c8d8e8; font-size: 12px; }";

static const QString kValueStyle =
    "QLabel { color: #00c8ff; font-size: 12px; font-weight: bold; }";

static const QString kEditStyle =
    "QLineEdit { background: #1a2a3a; border: 1px solid #304050; "
    "border-radius: 3px; color: #c8d8e8; font-size: 12px; padding: 2px 4px; }";

RadioSetupDialog::RadioSetupDialog(RadioModel* model, AudioEngine* audio, QWidget* parent)
    : QDialog(parent), m_model(model), m_audio(audio)
{
    setWindowTitle("Radio Setup");
    setMinimumSize(820, 520);
    setStyleSheet("QDialog { background: #0f0f1a; }");

    auto* layout = new QVBoxLayout(this);

    auto* tabs = new QTabWidget;
    m_tabs = tabs;
    tabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #304050; background: #0f0f1a; }"
        "QTabBar::tab { background: #1a2a3a; color: #8aa8c0; "
        "border: 1px solid #304050; padding: 4px 12px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #0f0f1a; color: #c8d8e8; "
        "border-bottom-color: #0f0f1a; }");

    tabs->addTab(buildRadioTab(), "Radio");
    tabs->addTab(buildNetworkTab(), "Network");
    tabs->addTab(buildGpsTab(), "GPS");
    tabs->addTab(buildAudioTab(), "Audio");
    tabs->addTab(buildTxTab(), "TX");
    tabs->addTab(buildPhoneCwTab(), "Phone/CW");
    tabs->addTab(buildRxTab(), "RX");
    tabs->addTab(buildFiltersTab(), "Filters");
    tabs->addTab(buildXvtrTab(), "XVTR");
    tabs->addTab(buildUsbCablesTab(), "USB Cables");
#ifdef HAVE_SERIALPORT
    tabs->addTab(buildSerialTab(), "Serial");
#endif

    layout->addWidget(tabs);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    buttons->setStyleSheet(
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; padding: 4px 16px; }"
        "QPushButton:hover { background: #203040; }");
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
}

// ── Radio tab ─────────────────────────────────────────────────────────────────

QWidget* RadioSetupDialog::buildRadioTab()
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setSpacing(8);

    // Toggle button style: green when on, red when off
    static const QString kToggleStyle =
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
        "padding: 3px 10px; }"
        "QPushButton:checked { background: #1a5030; color: #00e060; "
        "border: 1px solid #20a040; }";

    auto makeToggle = [](const QString& text, bool checked) {
        auto* btn = new QPushButton(checked ? "Enabled" : "Disabled");
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setStyleSheet(kToggleStyle);
        QObject::connect(btn, &QPushButton::toggled, btn, [btn](bool on) {
            btn->setText(on ? "Enabled" : "Disabled");
        });
        return btn;
    };

    // Radio Information group
    {
        auto* group = new QGroupBox("Radio Information");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(6);

        grid->addWidget(new QLabel("Radio SN:"), 0, 0);
        m_serialLabel = new QLabel(m_model->chassisSerial().isEmpty()
            ? m_model->serial() : m_model->chassisSerial());
        m_serialLabel->setStyleSheet(kValueStyle);
        grid->addWidget(m_serialLabel, 0, 1);

        grid->addWidget(new QLabel("Region:"), 0, 2);
        m_regionLabel = new QLabel(m_model->region().isEmpty() ? "USA" : m_model->region());
        m_regionLabel->setStyleSheet(
            "QLabel { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #00c8ff; font-size: 11px; font-weight: bold; "
            "padding: 3px 10px; }");
        m_regionLabel->setAlignment(Qt::AlignCenter);
        grid->addWidget(m_regionLabel, 0, 3);

        grid->addWidget(new QLabel("HW Version:"), 1, 0);
        m_hwVersionLabel = new QLabel("v" + m_model->version());
        m_hwVersionLabel->setStyleSheet(kValueStyle);
        grid->addWidget(m_hwVersionLabel, 1, 1);

        grid->addWidget(new QLabel("Remote On:"), 1, 2);
        m_remoteOnBtn = makeToggle("", m_model->remoteOnEnabled());
        connect(m_remoteOnBtn, &QPushButton::toggled, this, [this](bool on) {
            m_model->setRemoteOnEnabled(on);
        });
        grid->addWidget(m_remoteOnBtn, 1, 3);

        grid->addWidget(new QLabel("Options:"), 2, 0);
        m_optionsLabel = new QLabel(m_model->radioOptions().isEmpty()
            ? (m_model->hasAmplifier() ? "GPS, PGXL" : "GPS")
            : m_model->radioOptions());
        m_optionsLabel->setStyleSheet(kValueStyle);
        grid->addWidget(m_optionsLabel, 2, 1);

        grid->addWidget(new QLabel("FlexControl:"), 2, 2);
        auto* fcBtn = makeToggle("", true);
        grid->addWidget(fcBtn, 2, 3);

        grid->addWidget(new QLabel("multiFLEX:"), 3, 2);
        auto* mfBtn = makeToggle("", m_model->multiFlexEnabled());
        connect(mfBtn, &QPushButton::toggled, this, [this](bool on) {
            m_model->setMultiFlexEnabled(on);
        });
        grid->addWidget(mfBtn, 3, 3);

        for (auto* lbl : group->findChildren<QLabel*>()) {
            if (lbl->styleSheet().isEmpty())
                lbl->setStyleSheet(kLabelStyle);
        }

        vbox->addWidget(group);
    }

    // Radio Identification group
    {
        auto* group = new QGroupBox("Radio Identification");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(6);

        grid->addWidget(new QLabel("Model:"), 0, 0);
        m_modelLabel = new QLabel(m_model->model());
        m_modelLabel->setStyleSheet(kValueStyle);
        grid->addWidget(m_modelLabel, 0, 1);

        grid->addWidget(new QLabel("Nickname:"), 0, 2);
        m_nicknameEdit = new QLineEdit(m_model->nickname().isEmpty()
            ? m_model->name() : m_model->nickname());
        m_nicknameEdit->setStyleSheet(kEditStyle);
        grid->addWidget(m_nicknameEdit, 0, 3);

        grid->addWidget(new QLabel("Callsign:"), 1, 0);
        m_callsignEdit = new QLineEdit(m_model->callsign());
        m_callsignEdit->setStyleSheet(kEditStyle);
        grid->addWidget(m_callsignEdit, 1, 1);

        connect(m_nicknameEdit, &QLineEdit::editingFinished, this, [this] {
            m_model->sendCommand("radio name " + m_nicknameEdit->text());
        });
        connect(m_callsignEdit, &QLineEdit::editingFinished, this, [this] {
            m_model->sendCommand("radio callsign " + m_callsignEdit->text());
        });

        grid->addWidget(new QLabel("Station Name:"), 1, 2);
        auto* stationEdit = new QLineEdit(
            AppSettings::instance().value("StationName", "AetherSDR").toString());
        stationEdit->setStyleSheet(kEditStyle);
        stationEdit->setToolTip("Identifies this client to other Multi-Flex stations");
        grid->addWidget(stationEdit, 1, 3);
        connect(stationEdit, &QLineEdit::editingFinished, this, [this, stationEdit] {
            auto& s = AppSettings::instance();
            s.setValue("StationName", stationEdit->text());
            s.save();
            m_model->sendCommand("client station " + stationEdit->text());
        });

        for (auto* lbl : group->findChildren<QLabel*>()) {
            if (lbl->styleSheet().isEmpty())
                lbl->setStyleSheet(kLabelStyle);
        }

        vbox->addWidget(group);
    }

    // Firmware Update group
    {
        auto* group = new QGroupBox("Firmware Update");
        group->setStyleSheet(kGroupStyle);
        auto* vlay = new QVBoxLayout(group);
        vlay->setSpacing(6);

        // Current version row
        auto* infoRow = new QHBoxLayout;
        infoRow->addWidget(new QLabel("Current:"));
        auto* curFw = new QLabel(m_model->softwareVersion());
        curFw->setStyleSheet(kValueStyle);
        infoRow->addWidget(curFw);
        infoRow->addStretch(1);
        vlay->addLayout(infoRow);

        // Progress bar
        m_fwProgress = new QProgressBar;
        m_fwProgress->setRange(0, 100);
        m_fwProgress->setValue(0);
        m_fwProgress->setTextVisible(true);
        m_fwProgress->setFixedHeight(20);
        m_fwProgress->setStyleSheet(
            "QProgressBar { text-align: center; font-size: 11px; color: #c8d8e8;"
            " background: #1a2a3a; border: 1px solid #2e4e6e; border-radius: 3px; }"
            "QProgressBar::chunk { background: #00b4d8; }");
        m_fwProgress->hide();
        vlay->addWidget(m_fwProgress);

        // Status label (multi-line)
        m_fwStatusLabel = new QLabel("");
        m_fwStatusLabel->setStyleSheet("QLabel { color: #6888a0; font-size: 10px; }");
        m_fwStatusLabel->setWordWrap(true);
        vlay->addWidget(m_fwStatusLabel);

        // Button row
        auto* btnRow = new QHBoxLayout;
        auto* checkBtn = new QPushButton("Check for Update");
        checkBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #2e4e6e;"
            " border-radius: 3px; padding: 4px 8px; }"
            "QPushButton:hover { background: #2a3a4a; }");
        btnRow->addWidget(checkBtn);

        auto* browseBtn = new QPushButton("Browse .ssdr...");
        browseBtn->setStyleSheet(checkBtn->styleSheet());
        btnRow->addWidget(browseBtn);

        m_fwUploadBtn = new QPushButton("Upload Firmware");
        m_fwUploadBtn->setEnabled(false);
        m_fwUploadBtn->setStyleSheet(
            "QPushButton { background: #1a3a1a; color: #80e080; border: 1px solid #2e6e2e;"
            " border-radius: 3px; padding: 4px 8px; }"
            "QPushButton:hover { background: #2a4a2a; }"
            "QPushButton:disabled { background: #1a1a2a; color: #405060; border-color: #203040; }");
        btnRow->addWidget(m_fwUploadBtn);
        vlay->addLayout(btnRow);

        // ── Stager wiring ─────────────────────────────────────────────
        m_stager = new FirmwareStager(this);

        connect(checkBtn, &QPushButton::clicked, this, [this, checkBtn] {
            checkBtn->setEnabled(false);
            m_fwStatusLabel->setStyleSheet("QLabel { color: #6888a0; font-size: 10px; }");
            m_stager->checkForUpdate(m_model->softwareVersion());
        });

        connect(m_stager, &FirmwareStager::updateCheckComplete, this,
            [this, checkBtn](const QString& latest, bool avail) {
            checkBtn->setEnabled(true);
            if (avail) {
                m_fwStatusLabel->setStyleSheet("QLabel { color: #f0c040; font-size: 10px; }");
                m_fwStatusLabel->setText(QString("Update available: v%1\n"
                    "Click 'Check for Update' again to download and stage.").arg(latest));
                // Re-wire the check button to trigger download
                checkBtn->disconnect();
                checkBtn->setText("Download v" + latest);
                connect(checkBtn, &QPushButton::clicked, this, [this, checkBtn, latest] {
                    checkBtn->setEnabled(false);
                    m_fwProgress->show();
                    m_fwProgress->setValue(0);
                    m_stager->downloadAndStage(latest,
                        FirmwareStager::modelToFamily(m_model->model()));
                });
            } else {
                m_fwStatusLabel->setStyleSheet("QLabel { color: #80e080; font-size: 10px; }");
                m_fwStatusLabel->setText("Firmware is up to date (v" + latest + ").");
            }
        });

        connect(m_stager, &FirmwareStager::updateCheckFailed, this,
            [this, checkBtn](const QString& err) {
            checkBtn->setEnabled(true);
            m_fwStatusLabel->setStyleSheet("QLabel { color: #e08080; font-size: 10px; }");
            m_fwStatusLabel->setText(err);
        });

        connect(m_stager, &FirmwareStager::stageProgress, this,
            [this](int pct, const QString& status) {
            m_fwProgress->setValue(pct);
            m_fwStatusLabel->setStyleSheet("QLabel { color: #6888a0; font-size: 10px; }");
            m_fwStatusLabel->setText(status);
        });

        connect(m_stager, &FirmwareStager::stageComplete, this,
            [this](const QString& path, const QString&) {
            m_fwFilePath = path;
            m_fwUploadBtn->setEnabled(true);
            m_fwStatusLabel->setStyleSheet("QLabel { color: #80e080; font-size: 10px; }");
        });

        connect(m_stager, &FirmwareStager::stageFailed, this,
            [this](const QString& err) {
            m_fwProgress->hide();
            m_fwStatusLabel->setStyleSheet("QLabel { color: #e08080; font-size: 10px; }");
            m_fwStatusLabel->setText(err);
        });

        // ── Browse .ssdr manually ─────────────────────────────────────
        connect(browseBtn, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(
                this, "Select Firmware File", QString(),
                "SmartSDR Firmware (*.ssdr);;All Files (*)");
            if (path.isEmpty()) return;
            m_fwFilePath = path;
            m_fwUploadBtn->setEnabled(true);
            m_fwProgress->show();
            m_fwProgress->setValue(100);
            m_fwStatusLabel->setStyleSheet("QLabel { color: #80e080; font-size: 10px; }");
            m_fwStatusLabel->setText(QString("Ready to upload: %1 (%2 MB)")
                .arg(QFileInfo(path).fileName())
                .arg(QFileInfo(path).size() / (1024*1024)));
        });

        // ── Upload ────────────────────────────────────────────────────
        connect(m_fwUploadBtn, &QPushButton::clicked, this, [this] {
            if (m_fwFilePath.isEmpty()) return;

            const auto reply = QMessageBox::warning(this, "Firmware Update",
                QString("Upload %1 to %2?\n\n"
                        "The radio will reboot after the update.\n"
                        "Do not disconnect during the upload.")
                    .arg(QFileInfo(m_fwFilePath).fileName(), m_model->model()),
                QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
            if (reply != QMessageBox::Ok) return;

            if (!m_uploader)
                m_uploader = new FirmwareUploader(m_model, this);

            m_fwProgress->show();
            m_fwProgress->setValue(0);
            m_fwUploadBtn->setEnabled(false);
            m_fwStatusLabel->setStyleSheet("QLabel { color: #6888a0; font-size: 10px; }");

            connect(m_uploader, &FirmwareUploader::progressChanged, this,
                [this](int pct, const QString& status) {
                    m_fwProgress->setValue(pct);
                    m_fwStatusLabel->setText(status);
                });
            connect(m_uploader, &FirmwareUploader::finished, this,
                [this](bool ok, const QString& msg) {
                    m_fwStatusLabel->setText(msg);
                    m_fwUploadBtn->setEnabled(!ok);
                    if (ok) {
                        m_fwProgress->setValue(100);
                        m_fwStatusLabel->setStyleSheet("QLabel { color: #80e080; font-size: 10px; }");
                    } else {
                        m_fwProgress->hide();
                        m_fwStatusLabel->setStyleSheet("QLabel { color: #e08080; font-size: 10px; }");
                    }
                });

            m_uploader->upload(m_fwFilePath);
        });

        for (auto* lbl : group->findChildren<QLabel*>()) {
            if (lbl->styleSheet().isEmpty())
                lbl->setStyleSheet(kLabelStyle);
        }

        vbox->addWidget(group);
    }

    // Firmware disclaimer
    auto* disclaimer = new QLabel(
        "⚠ CAUTION: Firmware update is currently a highly experimental feature. "
        "Use at your own risk. At this time we still recommend updating "
        "via the SmartSDR Windows application.");
    disclaimer->setWordWrap(true);
    disclaimer->setStyleSheet(
        "QLabel { color: #c08040; font-size: 11px; font-style: italic;"
        " padding: 4px 8px; }");
    vbox->addWidget(disclaimer);

    vbox->addStretch(1);
    return page;
}


QWidget* RadioSetupDialog::buildNetworkTab()
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setSpacing(8);

    // Model header
    {
        auto* hdr = new QHBoxLayout;
        hdr->addStretch(1);
        auto* modelLbl = new QLabel(m_model->model());
        modelLbl->setStyleSheet("QLabel { color: #00c8ff; font-size: 20px; font-weight: bold; }");
        hdr->addWidget(modelLbl);
        vbox->addLayout(hdr);
    }

    // Network group
    {
        auto* group = new QGroupBox("Network");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(6);

        grid->addWidget(new QLabel("IP Address:"), 0, 0);
        auto* ipLbl = new QLabel(m_model->ip());
        ipLbl->setStyleSheet(kValueStyle);
        grid->addWidget(ipLbl, 0, 1);

        grid->addWidget(new QLabel("Mask:"), 0, 2);
        auto* maskLbl = new QLabel(m_model->netmask());
        maskLbl->setStyleSheet(kValueStyle);
        grid->addWidget(maskLbl, 0, 3);

        grid->addWidget(new QLabel("MAC Address:"), 1, 0);
        auto* macLbl = new QLabel(m_model->mac());
        macLbl->setStyleSheet(kValueStyle);
        grid->addWidget(macLbl, 1, 1);

        for (auto* lbl : group->findChildren<QLabel*>())
            if (lbl->styleSheet().isEmpty()) lbl->setStyleSheet(kLabelStyle);

        vbox->addWidget(group);
    }

    // Advanced group
    {
        auto* group = new QGroupBox("Advanced");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(6);

        grid->addWidget(new QLabel("Enforce Private IP Connections:"), 0, 0);
        auto* enforceBtn = new QPushButton(m_model->enforcePrivateIp() ? "Enabled" : "Disabled");
        enforceBtn->setCheckable(true);
        enforceBtn->setChecked(m_model->enforcePrivateIp());
        enforceBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
            "padding: 3px 10px; }"
            "QPushButton:checked { background: #1a5030; color: #00e060; "
            "border: 1px solid #20a040; }");
        connect(enforceBtn, &QPushButton::toggled, this, [this, enforceBtn](bool on) {
            enforceBtn->setText(on ? "Enabled" : "Disabled");
            m_model->sendCommand(
                QString("radio set enforce_private_ip_connections=%1").arg(on ? 1 : 0));
        });
        grid->addWidget(enforceBtn, 0, 1);

        grid->addWidget(new QLabel("Network MTU:"), 1, 0);
        auto* mtuSpin = new QSpinBox;
        mtuSpin->setRange(576, 9000);
        mtuSpin->setValue(AppSettings::instance().value("NetworkMtu", "1450").toInt());
        mtuSpin->setSuffix(" bytes");
        mtuSpin->setToolTip("Maximum Transmission Unit for VITA-49 UDP packets.\nDefault: 1450 (compatible with most VPN/SD-WAN tunnels).");
        connect(mtuSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
            m_model->sendCommand(
                QString("client set enforce_network_mtu=1 network_mtu=%1").arg(val));
            AppSettings::instance().setValue("NetworkMtu", QString::number(val));
            AppSettings::instance().save();
        });
        grid->addWidget(mtuSpin, 1, 1);

        for (auto* lbl : group->findChildren<QLabel*>())
            if (lbl->styleSheet().isEmpty()) lbl->setStyleSheet(kLabelStyle);

        vbox->addWidget(group);
    }

    // DHCP / Static IP group
    vbox->addWidget(buildIpConfigGroup());

    vbox->addStretch(1);
    return page;
}

// Extracted to reduce lambda nesting depth in buildNetworkTab()
// (avoids GCC 13 internal compiler error on Ubuntu 24.04)
QGroupBox* RadioSetupDialog::buildIpConfigGroup()
{
    auto* group = new QGroupBox("IP Configuration");
    group->setStyleSheet(kGroupStyle);
    auto* gvbox = new QVBoxLayout(group);
    gvbox->setSpacing(6);

    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);

    const bool isStatic = m_model->hasStaticIp();

    auto* dhcpBtn = new QPushButton("DHCP");
    dhcpBtn->setCheckable(true);
    dhcpBtn->setChecked(!isStatic);
    dhcpBtn->setStyleSheet(
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
        "padding: 4px 16px; }"
        "QPushButton:checked { background: #0070c0; color: #ffffff; "
        "border: 1px solid #0090e0; }");
    btnRow->addWidget(dhcpBtn);

    auto* staticBtn = new QPushButton("Static");
    staticBtn->setCheckable(true);
    staticBtn->setChecked(isStatic);
    staticBtn->setStyleSheet(dhcpBtn->styleSheet());
    btnRow->addWidget(staticBtn);

    btnRow->addStretch(1);
    gvbox->addLayout(btnRow);

    auto* fieldsGrid = new QGridLayout;
    fieldsGrid->setSpacing(4);

    fieldsGrid->addWidget(new QLabel("IP Address:"), 0, 0);
    auto* staticIp = new QLineEdit(isStatic ? m_model->staticIp() : m_model->ip());
    staticIp->setStyleSheet(kEditStyle);
    staticIp->setEnabled(isStatic);
    fieldsGrid->addWidget(staticIp, 0, 1);

    fieldsGrid->addWidget(new QLabel("Mask:"), 1, 0);
    auto* staticMask = new QLineEdit(isStatic ? m_model->staticNetmask() : m_model->netmask());
    staticMask->setStyleSheet(kEditStyle);
    staticMask->setEnabled(isStatic);
    fieldsGrid->addWidget(staticMask, 1, 1);

    fieldsGrid->addWidget(new QLabel("Gateway:"), 2, 0);
    auto* staticGw = new QLineEdit(isStatic ? m_model->staticGateway() : m_model->gateway());
    staticGw->setStyleSheet(kEditStyle);
    staticGw->setEnabled(isStatic);
    fieldsGrid->addWidget(staticGw, 2, 1);

    for (auto* lbl : group->findChildren<QLabel*>())
        if (lbl->styleSheet().isEmpty()) lbl->setStyleSheet(kLabelStyle);

    gvbox->addLayout(fieldsGrid);

    auto* applyBtn = new QPushButton("Apply");
    applyBtn->setEnabled(false);
    applyBtn->setStyleSheet(
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
        "padding: 4px 16px; }"
        "QPushButton:hover { background: #203040; }");
    gvbox->addWidget(applyBtn, 0, Qt::AlignLeft);

    connect(dhcpBtn, &QPushButton::clicked, this,
            [dhcpBtn, staticBtn, staticIp, staticMask, staticGw, applyBtn] {
        dhcpBtn->setChecked(true);
        staticBtn->setChecked(false);
        staticIp->setEnabled(false);
        staticMask->setEnabled(false);
        staticGw->setEnabled(false);
        applyBtn->setEnabled(true);
    });
    connect(staticBtn, &QPushButton::clicked, this,
            [dhcpBtn, staticBtn, staticIp, staticMask, staticGw, applyBtn] {
        dhcpBtn->setChecked(false);
        staticBtn->setChecked(true);
        staticIp->setEnabled(true);
        staticMask->setEnabled(true);
        staticGw->setEnabled(true);
        applyBtn->setEnabled(true);
    });
    connect(applyBtn, &QPushButton::clicked, this,
            [this, dhcpBtn, staticIp, staticMask, staticGw, applyBtn] {
        if (dhcpBtn->isChecked()) {
            m_model->sendCommand("radio static_net_params reset");
            qDebug() << "RadioSetupDialog: network set to DHCP";
        } else {
            const QString cmd = QString("radio static_net_params ip=%1 gateway=%2 netmask=%3")
                .arg(staticIp->text(), staticGw->text(), staticMask->text());
            m_model->sendCommand(cmd);
            qDebug() << "RadioSetupDialog: static IP applied" << cmd;
        }
        applyBtn->setEnabled(false);
    });

    return group;
}
QWidget* RadioSetupDialog::buildGpsTab()
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setSpacing(8);

    // Model header
    {
        auto* hdr = new QHBoxLayout;
        hdr->addStretch(1);
        auto* modelLbl = new QLabel(m_model->model());
        modelLbl->setStyleSheet("QLabel { color: #00c8ff; font-size: 20px; font-weight: bold; }");
        hdr->addWidget(modelLbl);
        vbox->addLayout(hdr);
    }

    // GPS installed status
    {
        const bool installed = (m_model->gpsStatus() != "Not Present"
                                && !m_model->gpsStatus().isEmpty());
        auto* statusLbl = new QLabel(installed ? "GPS is installed" : "GPS is not installed");
        statusLbl->setStyleSheet(installed
            ? "QLabel { color: #00c040; font-size: 16px; font-weight: bold; }"
            : "QLabel { color: #c04040; font-size: 16px; font-weight: bold; }");
        statusLbl->setAlignment(Qt::AlignCenter);
        vbox->addWidget(statusLbl);
        vbox->addSpacing(16);
    }

    // GPS data grid
    {
        auto* grid = new QGridLayout;
        grid->setSpacing(8);

        auto addField = [&](int row, int col, const QString& label, const QString& value) {
            auto* lbl = new QLabel(label);
            lbl->setStyleSheet(kLabelStyle);
            grid->addWidget(lbl, row, col * 2);
            auto* val = new QLabel(value);
            val->setStyleSheet(kValueStyle);
            grid->addWidget(val, row, col * 2 + 1);
        };

        addField(0, 0, "Latitude:",     m_model->gpsLat());
        addField(0, 1, "Longitude:",    m_model->gpsLon());
        addField(1, 0, "Grid Square:",  m_model->gpsGrid());
        addField(1, 1, "Altitude:",     m_model->gpsAltitude());
        addField(2, 0, "Sat Tracked:",  QString::number(m_model->gpsTracked()));
        addField(2, 1, "Sat Visible:",  QString::number(m_model->gpsVisible()));
        addField(3, 0, "Speed:",        m_model->gpsSpeed());
        addField(3, 1, "Freq Error:",   m_model->gpsFreqError());
        addField(4, 0, "Status:",       m_model->gpsStatus());
        addField(4, 1, "UTC Time:",     m_model->gpsTime());

        vbox->addLayout(grid);
    }

    vbox->addStretch(1);
    return page;
}
QWidget* RadioSetupDialog::buildTxTab()
{
    auto& tx = m_model->transmitModel();
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setSpacing(8);

    // Model header
    {
        auto* hdr = new QHBoxLayout;
        hdr->addStretch(1);
        auto* modelLbl = new QLabel(m_model->model());
        modelLbl->setStyleSheet("QLabel { color: #00c8ff; font-size: 20px; font-weight: bold; }");
        hdr->addWidget(modelLbl);
        vbox->addLayout(hdr);
    }

    // Timings group
    {
        auto* group = new QGroupBox("Timings (in ms)");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(6);

        auto addTimingField = [&](int row, int col, const QString& label, int value) {
            auto* lbl = new QLabel(label);
            lbl->setStyleSheet(kLabelStyle);
            grid->addWidget(lbl, row, col * 2);
            auto* edit = new QLineEdit(QString::number(value));
            edit->setStyleSheet(kEditStyle);
            edit->setFixedWidth(60);
            grid->addWidget(edit, row, col * 2 + 1);
            return edit;
        };

        addTimingField(0, 0, "ACC TX:",  tx.accTxDelay());
        addTimingField(0, 1, "TX Delay:", tx.txDelay());
        addTimingField(1, 0, "RCA TX1:", tx.tx1Delay());
        addTimingField(1, 1, "Timeout(min):", tx.interlockTimeout());
        addTimingField(2, 0, "RCA TX2:", tx.tx2Delay());

        // TX Profile dropdown (below Timeout, right column)
        auto* profCmb = new QComboBox;
        profCmb->addItems(tx.profileList());
        profCmb->setCurrentText(tx.activeProfile());
        AetherSDR::applyComboStyle(profCmb);
        grid->addWidget(profCmb, 2, 2, 1, 2);
        connect(profCmb, &QComboBox::currentTextChanged, this, [this](const QString& name) {
            m_model->transmitModel().loadProfile(name);
        });

        addTimingField(3, 0, "RCA TX3:", tx.tx3Delay());

        // TX Band Settings button
        auto* bandSetBtn = new QPushButton("TX Band Settings");
        bandSetBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
            "padding: 4px 12px; }"
            "QPushButton:hover { background: #203040; }");
        connect(bandSetBtn, &QPushButton::clicked, this, [this] {
            // Build TX Band Settings popup
            auto* dlg = new QDialog(this);
            dlg->setWindowTitle(QString("TX Band Settings (Current TX Profile: %1)")
                .arg(m_model->transmitModel().activeProfile()));
            dlg->setMinimumSize(700, 450);
            dlg->setStyleSheet("QDialog { background: #0f0f1a; }");
            dlg->setAttribute(Qt::WA_DeleteOnClose);

            auto* vb = new QVBoxLayout(dlg);

            // Table header
            auto* headerGrid = new QGridLayout;
            headerGrid->setSpacing(1);
            const QStringList headers = {"Band", "RF PWR(%)", "Tune PWR(%)", "PTT Inhibit",
                                          "ACC TX", "RCA TX Req", "ACC TX Req",
                                          "RCA TX1", "RCA TX2", "RCA TX3", "HWALC"};
            for (int c = 0; c < headers.size(); ++c) {
                auto* lbl = new QLabel(headers[c]);
                lbl->setStyleSheet("QLabel { color: #8aa8c0; font-size: 10px; "
                                    "font-weight: bold; background: #1a2a3a; "
                                    "border: 1px solid #304050; padding: 2px 4px; }");
                lbl->setAlignment(Qt::AlignCenter);
                headerGrid->addWidget(lbl, 0, c);
            }

            // Data rows — sorted by band order
            const auto& bands = m_model->txBandSettings();
            QList<int> sortedIds = bands.keys();
            std::sort(sortedIds.begin(), sortedIds.end());

            int row = 1;
            for (int id : sortedIds) {
                const auto& b = bands[id];
                int col = 0;

                // Band name
                auto* nameLbl = new QLabel(b.bandName);
                nameLbl->setStyleSheet("QLabel { color: #c8d8e8; font-size: 11px; "
                                        "font-weight: bold; background: #0f0f1a; "
                                        "border: 1px solid #203040; padding: 2px 4px; }");
                headerGrid->addWidget(nameLbl, row, col++);

                // RF Power
                auto* rfEdit = new QLineEdit(QString::number(b.rfPower));
                rfEdit->setStyleSheet(kEditStyle);
                rfEdit->setFixedWidth(50);
                rfEdit->setAlignment(Qt::AlignCenter);
                int bandId = id;
                connect(rfEdit, &QLineEdit::editingFinished, dlg, [this, rfEdit, bandId] {
                    m_model->sendCommand(
                        QString("transmit bandset %1 rfpower=%2").arg(bandId).arg(rfEdit->text()));
                });
                headerGrid->addWidget(rfEdit, row, col++);

                // Tune Power
                auto* tuneEdit = new QLineEdit(QString::number(b.tunePower));
                tuneEdit->setStyleSheet(kEditStyle);
                tuneEdit->setFixedWidth(50);
                tuneEdit->setAlignment(Qt::AlignCenter);
                connect(tuneEdit, &QLineEdit::editingFinished, dlg, [this, tuneEdit, bandId] {
                    m_model->sendCommand(
                        QString("transmit bandset %1 tunepower=%2").arg(bandId).arg(tuneEdit->text()));
                });
                headerGrid->addWidget(tuneEdit, row, col++);

                // Checkboxes: PTT Inhibit, ACC TX, RCA TX Req, ACC TX Req, TX1, TX2, TX3, HWALC
                struct CbDef { bool val; const char* txCmd; const char* ilCmd; };
                CbDef cbs[] = {
                    {b.inhibit, "inhibit", nullptr},
                    {b.accTx,   nullptr, "acc_tx_enabled"},
                    {b.rcaTxReq,nullptr, "rca_txreq_enable"},
                    {b.accTxReq,nullptr, "acc_txreq_enable"},
                    {b.tx1,     nullptr, "tx1_enabled"},
                    {b.tx2,     nullptr, "tx2_enabled"},
                    {b.tx3,     nullptr, "tx3_enabled"},
                    {b.hwAlc,   "hwalc_enabled", nullptr},
                };

                for (const auto& cb : cbs) {
                    auto* chk = new QCheckBox;
                    chk->setChecked(cb.val);
                    chk->setStyleSheet(
                        "QCheckBox { background: transparent; spacing: 0; }"
                        "QCheckBox::indicator { width: 16px; height: 16px; "
                        "border: 2px solid #506070; border-radius: 3px; background: #0a0a18; }"
                        "QCheckBox::indicator:checked { background: #0070c0; "
                        "border: 2px solid #00a0e0; }");
                    auto cmdKey = cb.txCmd ? QString(cb.txCmd) : QString(cb.ilCmd);
                    auto prefix = cb.txCmd ? QString("transmit") : QString("interlock");
                    connect(chk, &QCheckBox::toggled, dlg, [this, bandId, prefix, cmdKey](bool on) {
                        m_model->sendCommand(
                            QString("%1 bandset %2 %3=%4").arg(prefix).arg(bandId).arg(cmdKey).arg(on ? 1 : 0));
                    });
                    auto* container = new QWidget;
                    auto* hb = new QHBoxLayout(container);
                    hb->setContentsMargins(0, 0, 0, 0);
                    hb->setAlignment(Qt::AlignCenter);
                    hb->addWidget(chk);
                    headerGrid->addWidget(container, row, col++);
                }

                ++row;
            }

            vb->addLayout(headerGrid);
            vb->addStretch(1);

            auto* closeBtn = new QPushButton("Close");
            closeBtn->setStyleSheet(
                "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
                "border-radius: 3px; color: #c8d8e8; padding: 4px 16px; }"
                "QPushButton:hover { background: #203040; }");
            connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
            vb->addWidget(closeBtn, 0, Qt::AlignRight);

            dlg->show();
        });
        grid->addWidget(bandSetBtn, 3, 2, 1, 2);

        for (auto* lbl : group->findChildren<QLabel*>())
            if (lbl->styleSheet().isEmpty()) lbl->setStyleSheet(kLabelStyle);

        vbox->addWidget(group);
    }

    // Interlocks group
    {
        auto* group = new QGroupBox("Interlocks - TX REQ");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(6);

        auto* rcaLbl = new QLabel("RCA:");
        rcaLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(rcaLbl, 0, 0);
        auto* rcaCmb = new QComboBox;
        rcaCmb->addItems({"Active Low", "Active High"});
        rcaCmb->setCurrentIndex(tx.rcaTxReqPolarity());
        AetherSDR::applyComboStyle(rcaCmb);
        grid->addWidget(rcaCmb, 0, 1);

        auto* accLbl = new QLabel("Accessory:");
        accLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(accLbl, 0, 2);
        auto* accCmb = new QComboBox;
        accCmb->addItems({"Active Low", "Active High"});
        accCmb->setCurrentIndex(tx.accTxReqPolarity());
        AetherSDR::applyComboStyle(accCmb);
        grid->addWidget(accCmb, 0, 3);

        vbox->addWidget(group);
    }

    // Max Power / Tune Mode / Show TX in Waterfall
    {
        auto* grid = new QGridLayout;
        grid->setSpacing(6);

        auto* mpLbl = new QLabel("Max Power:");
        mpLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(mpLbl, 0, 0);
        auto* mpRow = new QHBoxLayout;
        auto* mpEdit = new QLineEdit(QString::number(tx.maxPowerLevel()));
        mpEdit->setStyleSheet(kEditStyle);
        mpEdit->setFixedWidth(50);
        mpRow->addWidget(mpEdit);
        auto* mpUnit = new QLabel("%");
        mpUnit->setStyleSheet(kLabelStyle);
        mpRow->addWidget(mpUnit);
        mpRow->addStretch(1);
        grid->addLayout(mpRow, 0, 1);

        connect(mpEdit, &QLineEdit::editingFinished, this, [this, mpEdit] {
            int val = qBound(0, mpEdit->text().toInt(), 100);
            mpEdit->setText(QString::number(val));
            m_model->sendCommand(
                QString("transmit set max_power_level=%1").arg(val));
        });

        auto* tmLbl = new QLabel("Tune Mode:");
        tmLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(tmLbl, 1, 0);
        auto* tmCmb = new QComboBox;
        tmCmb->addItems({"Single Tone", "Two Tone"});
        tmCmb->setCurrentText(tx.tuneMode() == "single_tone" ? "Single Tone" : "Two Tone");
        AetherSDR::applyComboStyle(tmCmb);
        connect(tmCmb, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            QString mode = (text == "Single Tone") ? "single_tone" : "two_tone";
            m_model->sendCommand("transmit set tune_mode=" + mode);
        });
        grid->addWidget(tmCmb, 1, 1);

        auto* swLbl = new QLabel("Show TX in Waterfall:");
        swLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(swLbl, 2, 0);
        auto* swBtn = new QPushButton(tx.showTxInWaterfall() ? "Enabled" : "Disabled");
        swBtn->setCheckable(true);
        swBtn->setChecked(tx.showTxInWaterfall());
        swBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
            "padding: 3px 10px; }"
            "QPushButton:checked { background: #1a5030; color: #00e060; "
            "border: 1px solid #20a040; }");
        connect(swBtn, &QPushButton::toggled, this, [this, swBtn](bool on) {
            swBtn->setText(on ? "Enabled" : "Disabled");
            m_model->sendCommand(
                QString("transmit set show_tx_in_waterfall=%1").arg(on ? 1 : 0));
        });
        grid->addWidget(swBtn, 2, 1);

        vbox->addLayout(grid);
    }

    vbox->addStretch(1);
    return page;
}
QWidget* RadioSetupDialog::buildPhoneCwTab()
{
    auto& tx = m_model->transmitModel();
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setSpacing(8);

    // Model header
    {
        auto* hdr = new QHBoxLayout;
        hdr->addStretch(1);
        auto* modelLbl = new QLabel(m_model->model());
        modelLbl->setStyleSheet("QLabel { color: #00c8ff; font-size: 20px; font-weight: bold; }");
        hdr->addWidget(modelLbl);
        vbox->addLayout(hdr);
    }

    static const QString kTogBtnStyle =
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
        "padding: 3px 10px; }"
        "QPushButton:checked { background: #0070c0; color: #ffffff; "
        "border: 1px solid #0090e0; }";

    auto mkTogBtn = [&](const QString& text, bool checked) {
        auto* btn = new QPushButton(text);
        btn->setCheckable(true);
        btn->setChecked(checked);
        btn->setStyleSheet(kTogBtnStyle);
        return btn;
    };

    // Microphone group
    {
        auto* group = new QGroupBox("Microphone");
        group->setStyleSheet(kGroupStyle);
        auto* gvb = new QVBoxLayout(group);
        gvb->setSpacing(4);

        // BIAS / +20dB row
        auto* row1 = new QHBoxLayout;
        row1->setSpacing(4);
        auto* biasBtn = mkTogBtn("BIAS", tx.micBias());
        connect(biasBtn, &QPushButton::toggled, this, [this](bool on) {
            m_model->sendCommand(QString("mic bias %1").arg(on ? 1 : 0));
        });
        row1->addWidget(biasBtn);
        auto* boostBtn = mkTogBtn("+20dB", tx.micBoost());
        connect(boostBtn, &QPushButton::toggled, this, [this](bool on) {
            m_model->sendCommand(QString("mic boost %1").arg(on ? 1 : 0));
        });
        row1->addWidget(boostBtn);
        row1->addStretch(1);
        gvb->addLayout(row1);

        // Meter in RX
        auto* row2 = new QHBoxLayout;
        row2->setSpacing(4);
        auto* metBtn = mkTogBtn("Enabled", tx.metInRx());
        connect(metBtn, &QPushButton::toggled, this, [this, metBtn](bool on) {
            metBtn->setText(on ? "Enabled" : "Disabled");
            m_model->sendCommand(QString("transmit set met_in_rx=%1").arg(on ? 1 : 0));
        });
        row2->addWidget(metBtn);
        auto* metLbl = new QLabel("Enable/Disable the Level Meter During Receive");
        metLbl->setStyleSheet(kLabelStyle);
        row2->addWidget(metLbl);
        row2->addStretch(1);
        gvb->addLayout(row2);

        vbox->addWidget(group);
    }

    // CW group
    {
        auto* group = new QGroupBox("CW");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(6);

        // Iambic: Enabled | A | B
        auto* iamLbl = new QLabel("Iambic:");
        iamLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(iamLbl, 0, 0);
        auto* iamBtn = mkTogBtn("Enabled", tx.cwIambic());
        connect(iamBtn, &QPushButton::toggled, this, [this, iamBtn](bool on) {
            iamBtn->setText(on ? "Enabled" : "Disabled");
            m_model->sendCommand(QString("cw iambic %1").arg(on ? 1 : 0));
        });
        grid->addWidget(iamBtn, 0, 1);
        auto* modeA = mkTogBtn("A", tx.cwIambicMode() == 0);
        auto* modeB = mkTogBtn("B", tx.cwIambicMode() == 1);
        connect(modeA, &QPushButton::clicked, this, [this, modeA, modeB] {
            modeA->setChecked(true); modeB->setChecked(false);
            m_model->sendCommand("cw mode 0");
        });
        connect(modeB, &QPushButton::clicked, this, [this, modeA, modeB] {
            modeA->setChecked(false); modeB->setChecked(true);
            m_model->sendCommand("cw mode 1");
        });
        grid->addWidget(modeA, 0, 2);
        grid->addWidget(modeB, 0, 3);

        // Swap: Dot/Dash button
        auto* swapLbl = new QLabel("Swap:");
        swapLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(swapLbl, 0, 4);
        auto* swapBtn = mkTogBtn("Dot/Dash", tx.cwSwapPaddles());
        connect(swapBtn, &QPushButton::toggled, this, [this](bool on) {
            m_model->sendCommand(QString("cw swap %1").arg(on ? 1 : 0));
        });
        grid->addWidget(swapBtn, 0, 5);

        // Sideband: CWU | CWL
        auto* sbLbl = new QLabel("Sideband:");
        sbLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(sbLbl, 1, 0);
        auto* cwuBtn = mkTogBtn("CWU", !tx.cwlEnabled());
        auto* cwlBtn = mkTogBtn("CWL", tx.cwlEnabled());
        connect(cwuBtn, &QPushButton::clicked, this, [this, cwuBtn, cwlBtn] {
            cwuBtn->setChecked(true); cwlBtn->setChecked(false);
            m_model->sendCommand("cw cwl_enabled 0");
        });
        connect(cwlBtn, &QPushButton::clicked, this, [this, cwuBtn, cwlBtn] {
            cwuBtn->setChecked(false); cwlBtn->setChecked(true);
            m_model->sendCommand("cw cwl_enabled 1");
        });
        grid->addWidget(cwuBtn, 1, 1);
        grid->addWidget(cwlBtn, 1, 2);

        // CWX: Sync
        auto* cwxLbl = new QLabel("CWX:");
        cwxLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(cwxLbl, 1, 4);
        auto* syncBtn = mkTogBtn("Sync", tx.syncCwx());
        connect(syncBtn, &QPushButton::toggled, this, [this](bool on) {
            m_model->sendCommand(QString("cw synccwx %1").arg(on ? 1 : 0));
        });
        grid->addWidget(syncBtn, 1, 5);

        // CW Decode overlay toggle
        auto* decodeLbl = new QLabel("Decode:");
        decodeLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(decodeLbl, 2, 4);
        bool decodeOn = AppSettings::instance().value("CwDecodeOverlay", "True").toString() == "True";
        auto* decodeBtn = mkTogBtn("On", decodeOn);
        connect(decodeBtn, &QPushButton::toggled, this, [](bool on) {
            auto& s = AppSettings::instance();
            s.setValue("CwDecodeOverlay", on ? "True" : "False");
            s.save();
        });
        grid->addWidget(decodeBtn, 2, 5);



        vbox->addWidget(group);
    }

    // Digital group
    {
        auto* group = new QGroupBox("Digital");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(6);

        auto* markLbl = new QLabel("RTTY Mark Default:");
        markLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(markLbl, 0, 0);
        auto* markEdit = new QLineEdit(QString::number(m_model->rttyMarkDefault()));
        markEdit->setStyleSheet(kEditStyle);
        markEdit->setFixedWidth(60);
        connect(markEdit, &QLineEdit::editingFinished, this, [this, markEdit] {
            m_model->sendCommand(
                "radio set rtty_mark_default=" + markEdit->text());
        });
        grid->addWidget(markEdit, 0, 1);

        vbox->addWidget(group);
    }

    vbox->addStretch(1);
    return page;
}
QWidget* RadioSetupDialog::buildRxTab()
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setSpacing(8);

    // Model header
    {
        auto* hdr = new QHBoxLayout;
        hdr->addStretch(1);
        auto* modelLbl = new QLabel(m_model->model());
        modelLbl->setStyleSheet("QLabel { color: #00c8ff; font-size: 20px; font-weight: bold; }");
        hdr->addWidget(modelLbl);
        vbox->addLayout(hdr);
    }

    static const QString kTogStyle =
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
        "padding: 3px 10px; }"
        "QPushButton:checked { background: #1a5030; color: #00e060; "
        "border: 1px solid #20a040; }";

    // Frequency Offset group
    {
        auto* group = new QGroupBox("Frequency Offset");
        group->setStyleSheet(kGroupStyle);
        auto* gvb = new QVBoxLayout(group);
        gvb->setSpacing(4);

        if (m_model->gpsdoPresent()) {
            auto* lbl = new QLabel("GPSDO is installed. Frequency error correction is not required.");
            lbl->setStyleSheet("QLabel { color: #00c040; font-size: 12px; }");
            lbl->setWordWrap(true);
            gvb->addWidget(lbl);
        } else {
            auto* lbl = new QLabel("No GPSDO installed. Manual frequency offset calibration available.");
            lbl->setStyleSheet("QLabel { color: #c0a000; font-size: 12px; }");
            lbl->setWordWrap(true);
            gvb->addWidget(lbl);

            // Cal Frequency row
            auto* calRow = new QHBoxLayout;
            calRow->setSpacing(4);
            auto* calLbl = new QLabel("Cal Frequency (MHz):");
            calLbl->setStyleSheet(kLabelStyle);
            calRow->addWidget(calLbl);
            auto* calEdit = new QLineEdit(QString::number(m_model->calFreqMhz(), 'f', 6));
            calEdit->setStyleSheet(kEditStyle);
            calEdit->setFixedWidth(100);
            connect(calEdit, &QLineEdit::editingFinished, this, [this, calEdit] {
                m_model->sendCommand(
                    "radio set cal_freq=" + calEdit->text());
            });
            calRow->addWidget(calEdit);

            auto* startBtn = new QPushButton("Start");
            startBtn->setStyleSheet(kTogStyle);
            startBtn->setFixedWidth(60);
            connect(startBtn, &QPushButton::clicked, this, [this, calEdit] {
                // Set cal_freq and trigger calibration
                m_model->sendCommand(
                    "radio set cal_freq=" + calEdit->text());
            });
            calRow->addWidget(startBtn);
            calRow->addStretch(1);
            gvb->addLayout(calRow);

            // Freq Error PPB row
            auto* row = new QHBoxLayout;
            row->setSpacing(4);
            auto* ppbLbl = new QLabel("Freq Offset (ppb):");
            ppbLbl->setStyleSheet(kLabelStyle);
            row->addWidget(ppbLbl);
            auto* ppbEdit = new QLineEdit(QString::number(m_model->freqErrorPpb()));
            ppbEdit->setStyleSheet(kEditStyle);
            ppbEdit->setFixedWidth(80);
            connect(ppbEdit, &QLineEdit::editingFinished, this, [this, ppbEdit] {
                m_model->sendCommand(
                    "radio set freq_error_ppb=" + ppbEdit->text());
            });
            row->addWidget(ppbEdit);
            auto* ppbUnitLbl = new QLabel("ppb");
            ppbUnitLbl->setStyleSheet(kLabelStyle);
            row->addWidget(ppbUnitLbl);
            row->addStretch(1);
            gvb->addLayout(row);
        }

        vbox->addWidget(group);
    }

    // 10 MHz Reference group
    {
        auto* group = new QGroupBox("10 MHz Reference");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(6);

        auto* srcLbl = new QLabel("Source:");
        srcLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(srcLbl, 0, 0);

        auto* srcCmb = new QComboBox;
        AetherSDR::applyComboStyle(srcCmb);
        srcCmb->addItem("Auto", "auto");
        if (m_model->tcxoPresent())  srcCmb->addItem("TCXO", "tcxo");
        if (m_model->gpsdoPresent()) srcCmb->addItem("GPSDO", "gpsdo");
        if (m_model->extPresent())   srcCmb->addItem("External", "external");
        // Select current setting
        int idx = srcCmb->findData(m_model->oscSetting());
        if (idx >= 0) srcCmb->setCurrentIndex(idx);
        connect(srcCmb, &QComboBox::currentIndexChanged, this, [this, srcCmb](int i) {
            m_model->sendCommand(
                "radio oscillator " + srcCmb->itemData(i).toString());
        });
        grid->addWidget(srcCmb, 0, 1);

        // Lock status
        auto* lockLbl = new QLabel(
            m_model->oscState().toUpper() + (m_model->oscLocked() ? " Locked" : " Unlocked"));
        lockLbl->setStyleSheet(m_model->oscLocked()
            ? "QLabel { color: #00c040; font-size: 12px; font-weight: bold; }"
            : "QLabel { color: #c04040; font-size: 12px; font-weight: bold; }");
        grid->addWidget(lockLbl, 0, 2);

        vbox->addWidget(group);
    }

    // General RX settings
    {
        auto* grid = new QGridLayout;
        grid->setSpacing(6);

        auto addToggle = [&](int row, const QString& label, bool checked,
                              const QString& cmd) {
            auto* lbl = new QLabel(label);
            lbl->setStyleSheet(kLabelStyle);
            grid->addWidget(lbl, row, 0);
            auto* btn = new QPushButton(checked ? "Enabled" : "Disabled");
            btn->setCheckable(true);
            btn->setChecked(checked);
            btn->setStyleSheet(kTogStyle);
            connect(btn, &QPushButton::toggled, this, [this, btn, cmd](bool on) {
                btn->setText(on ? "Enabled" : "Disabled");
                m_model->sendCommand(
                    QString("%1=%2").arg(cmd).arg(on ? 1 : 0));
            });
            grid->addWidget(btn, row, 1);
        };

        addToggle(0, "Mute local audio when remote:", m_model->muteLocalWhenRemote(),
                  "radio set mute_local_audio_when_remote");
        addToggle(1, "Binaural audio:", m_model->binauralRx(),
                  "radio set binaural_rx");

        vbox->addLayout(grid);
    }

    vbox->addStretch(1);
    return page;
}
// ── Audio tab ────────────────────────────────────────────────────────────────

QWidget* RadioSetupDialog::buildAudioTab()
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);

    // ── Radio Audio Outputs ──────────────────────────────────────────────
    auto* outGroup = new QGroupBox("Radio Audio Outputs");
    outGroup->setStyleSheet(kGroupStyle);
    auto* outLayout = new QVBoxLayout(outGroup);

    // Line Out
    auto* lineoutRow = new QHBoxLayout;
    auto* lineoutLabel = new QLabel("Line Out:");
    lineoutLabel->setStyleSheet(kLabelStyle);
    lineoutLabel->setFixedWidth(90);
    auto* lineoutSlider = new QSlider(Qt::Horizontal);
    lineoutSlider->setRange(0, 100);
    lineoutSlider->setValue(m_model->lineoutGain());
    auto* lineoutValue = new QLabel(QString::number(m_model->lineoutGain()));
    lineoutValue->setStyleSheet(kValueStyle);
    lineoutValue->setFixedWidth(30);
    auto* lineoutMute = new QPushButton("Mute");
    lineoutMute->setCheckable(true);
    lineoutMute->setChecked(m_model->lineoutMute());
    lineoutMute->setFixedWidth(50);
    lineoutMute->setStyleSheet(
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 11px; padding: 2px; }"
        "QPushButton:checked { background: #8b0000; color: #fff; }");
    lineoutRow->addWidget(lineoutLabel);
    lineoutRow->addWidget(lineoutSlider, 1);
    lineoutRow->addWidget(lineoutValue);
    lineoutRow->addWidget(lineoutMute);
    outLayout->addLayout(lineoutRow);

    connect(lineoutSlider, &QSlider::valueChanged, this, [this, lineoutValue](int v) {
        lineoutValue->setText(QString::number(v));
        m_model->setLineoutGain(v);
    });
    connect(lineoutMute, &QPushButton::toggled, m_model, &RadioModel::setLineoutMute);

    // Headphone
    auto* hpRow = new QHBoxLayout;
    auto* hpLabel = new QLabel("Headphone:");
    hpLabel->setStyleSheet(kLabelStyle);
    hpLabel->setFixedWidth(90);
    auto* hpSlider = new QSlider(Qt::Horizontal);
    hpSlider->setRange(0, 100);
    hpSlider->setValue(m_model->headphoneGain());
    auto* hpValue = new QLabel(QString::number(m_model->headphoneGain()));
    hpValue->setStyleSheet(kValueStyle);
    hpValue->setFixedWidth(30);
    auto* hpMute = new QPushButton("Mute");
    hpMute->setCheckable(true);
    hpMute->setChecked(m_model->headphoneMute());
    hpMute->setFixedWidth(50);
    hpMute->setStyleSheet(
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 11px; padding: 2px; }"
        "QPushButton:checked { background: #8b0000; color: #fff; }");
    hpRow->addWidget(hpLabel);
    hpRow->addWidget(hpSlider, 1);
    hpRow->addWidget(hpValue);
    hpRow->addWidget(hpMute);
    outLayout->addLayout(hpRow);

    connect(hpSlider, &QSlider::valueChanged, this, [this, hpValue](int v) {
        hpValue->setText(QString::number(v));
        m_model->setHeadphoneGain(v);
    });
    connect(hpMute, &QPushButton::toggled, m_model, &RadioModel::setHeadphoneMute);

    // Front Speaker (mute only) — only on M-suffix models with built-in speaker
    // M-suffix models have a built-in front speaker (6400M, 6600M, 8400M, 8600M, AU-510M, AU-520M)
    bool hasFrontSpeaker = m_model->model().endsWith("M", Qt::CaseInsensitive);
    if (hasFrontSpeaker) {
        auto* spkRow = new QHBoxLayout;
        auto* spkLabel = new QLabel("Front Speaker:");
        spkLabel->setStyleSheet(kLabelStyle);
        spkLabel->setFixedWidth(90);
        auto* spkMute = new QPushButton("Mute");
        spkMute->setCheckable(true);
        spkMute->setChecked(m_model->frontSpeakerMute());
        spkMute->setFixedWidth(50);
        spkMute->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; padding: 2px; }"
            "QPushButton:checked { background: #8b0000; color: #fff; }");
        spkRow->addWidget(spkLabel);
        spkRow->addStretch(1);
        spkRow->addWidget(spkMute);
        outLayout->addLayout(spkRow);
        connect(spkMute, &QPushButton::toggled, m_model, &RadioModel::setFrontSpeakerMute);
    }

    // Update from radio status
    connect(m_model, &RadioModel::audioOutputChanged, this,
            [this, lineoutSlider, lineoutValue, lineoutMute,
             hpSlider, hpValue, hpMute] {
        QSignalBlocker b1(lineoutSlider), b2(lineoutMute),
                       b3(hpSlider), b4(hpMute);
        lineoutSlider->setValue(m_model->lineoutGain());
        lineoutValue->setText(QString::number(m_model->lineoutGain()));
        lineoutMute->setChecked(m_model->lineoutMute());
        hpSlider->setValue(m_model->headphoneGain());
        hpValue->setText(QString::number(m_model->headphoneGain()));
        hpMute->setChecked(m_model->headphoneMute());
    });

    vbox->addWidget(outGroup);

    // ── Audio Compression ────────────────────────────────────────────────
    {
        auto* compGroup = new QGroupBox("Audio Compression (SmartLink)");
        compGroup->setStyleSheet(kGroupStyle);
        auto* compLayout = new QHBoxLayout(compGroup);
        compLayout->setSpacing(4);

        QString current = AppSettings::instance().value("AudioCompression", "None").toString();

        const QString btnStyle =
            "QPushButton { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050; "
            "border-radius: 3px; padding: 2px 10px; font-size: 11px; }"
            "QPushButton:checked { background: #00607a; color: #e0f0ff; border-color: #00b4d8; }";

        auto* autoBtn = new QPushButton("Auto");
        autoBtn->setCheckable(true); autoBtn->setChecked(current == "Auto");
        autoBtn->setStyleSheet(btnStyle);
        auto* noneBtn = new QPushButton("Uncompressed");
        noneBtn->setCheckable(true); noneBtn->setChecked(current == "None");
        noneBtn->setStyleSheet(btnStyle);
        auto* opusBtn = new QPushButton("Opus");
        opusBtn->setCheckable(true); opusBtn->setChecked(current == "Opus");
        opusBtn->setStyleSheet(btnStyle);

        auto setComp = [autoBtn, noneBtn, opusBtn](const QString& val) {
            QSignalBlocker b1(autoBtn), b2(noneBtn), b3(opusBtn);
            autoBtn->setChecked(val == "Auto");
            noneBtn->setChecked(val == "None");
            opusBtn->setChecked(val == "Opus");
            auto& s = AppSettings::instance();
            s.setValue("AudioCompression", val);
            s.save();
        };

        connect(autoBtn, &QPushButton::clicked, this, [setComp]() { setComp("Auto"); });
        connect(noneBtn, &QPushButton::clicked, this, [setComp]() { setComp("None"); });
        connect(opusBtn, &QPushButton::clicked, this, [setComp]() { setComp("Opus"); });

        compLayout->addWidget(autoBtn);
        compLayout->addWidget(noneBtn);
        compLayout->addWidget(opusBtn);
        compLayout->addStretch();

        auto* hint = new QLabel("Auto = Opus on SmartLink, uncompressed on LAN");
        hint->setStyleSheet("QLabel { color: #607080; font-size: 10px; }");
        compLayout->addWidget(hint);

        vbox->addWidget(compGroup);
    }

    // ── PC Audio Devices ────────────────────────────────────────────────
    auto* pcGroup = new QGroupBox("PC Audio Devices");
    pcGroup->setStyleSheet(kGroupStyle);
    auto* pcLayout = new QVBoxLayout(pcGroup);

    // Input device
    auto* inRow = new QHBoxLayout;
    auto* inLabel = new QLabel("Input:");
    inLabel->setStyleSheet(kLabelStyle);
    inLabel->setFixedWidth(90);
    auto* inCombo = new QComboBox;
    AetherSDR::applyComboStyle(inCombo);
    const auto inDevices = QMediaDevices::audioInputs();
    for (const auto& dev : inDevices)
        inCombo->addItem(dev.description(), dev.id());
    const auto defaultIn = QMediaDevices::defaultAudioInput();
    int inIdx = inCombo->findData(defaultIn.id());
    if (inIdx >= 0) inCombo->setCurrentIndex(inIdx);
    inRow->addWidget(inLabel);
    inRow->addWidget(inCombo, 1);
    pcLayout->addLayout(inRow);

    // Output device
    auto* outRow = new QHBoxLayout;
    auto* outLabel = new QLabel("Output:");
    outLabel->setStyleSheet(kLabelStyle);
    outLabel->setFixedWidth(90);
    auto* outCombo = new QComboBox;
    AetherSDR::applyComboStyle(outCombo);
    const auto outDevices = QMediaDevices::audioOutputs();
    for (const auto& dev : outDevices)
        outCombo->addItem(dev.description(), dev.id());
    // Select current device (or system default)
    const auto curOut = m_audio ? m_audio->outputDevice() : QAudioDevice();
    const auto selOut = curOut.isNull() ? QMediaDevices::defaultAudioOutput() : curOut;
    int outIdx = outCombo->findData(selOut.id());
    if (outIdx >= 0) outCombo->setCurrentIndex(outIdx);
    outRow->addWidget(outLabel);
    outRow->addWidget(outCombo, 1);
    pcLayout->addLayout(outRow);

    // Wire device changes to AudioEngine
    if (m_audio) {
        connect(inCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, inCombo, inDevices](int idx) {
            if (idx >= 0 && idx < inDevices.size())
                m_audio->setInputDevice(inDevices[idx]);
        });
        connect(outCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, [this, outCombo, outDevices](int idx) {
            if (idx >= 0 && idx < outDevices.size())
                m_audio->setOutputDevice(outDevices[idx]);
        });
    }

    vbox->addWidget(pcGroup);

    // ── NVIDIA BNR (GPU Noise Removal) ──────────────────────────────────
#ifdef HAVE_BNR
    {
        auto* bnrGroup = new QGroupBox("NVIDIA BNR (GPU Noise Removal)");
        bnrGroup->setStyleSheet(kGroupStyle);
        auto* bnrLayout = new QVBoxLayout(bnrGroup);

        // Autostart checkbox
        auto* autoRow = new QHBoxLayout;
        auto* autoStart = new QPushButton("Autostart Container");
        autoStart->setCheckable(true);
        autoStart->setChecked(
            AppSettings::instance().value("BnrAutostart", "False").toString() == "True");
        autoStart->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; padding: 2px 10px; }"
            "QPushButton:checked { background: #00607a; color: #e0f0ff; border-color: #00b4d8; }");
        autoRow->addWidget(autoStart);

        // Container name
        auto* nameLbl = new QLabel("Container:");
        nameLbl->setStyleSheet(kLabelStyle);
        auto* nameEdit = new QLineEdit(
            AppSettings::instance().value("BnrContainerName", "maxine-bnr").toString());
        nameEdit->setFixedWidth(120);
        nameEdit->setStyleSheet(
            "QLineEdit { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; padding: 2px 4px; }");
        autoRow->addWidget(nameLbl);
        autoRow->addWidget(nameEdit);
        autoRow->addStretch(1);
        bnrLayout->addLayout(autoRow);

        // Status row
        auto* statusRow = new QHBoxLayout;
        auto* statusDot = new QLabel("\u2B24");  // filled circle
        statusDot->setStyleSheet("QLabel { color: #404040; font-size: 10px; }");
        auto* statusLbl = new QLabel("Unknown");
        statusLbl->setStyleSheet(kLabelStyle);
        statusRow->addWidget(statusDot);
        statusRow->addWidget(statusLbl);

        auto* checkBtn = new QPushButton("Check Status");
        checkBtn->setFixedWidth(90);
        checkBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; padding: 2px; }"
            "QPushButton:hover { background: #203040; }");
        statusRow->addWidget(checkBtn);

        auto* startBtn = new QPushButton("Start");
        startBtn->setFixedWidth(50);
        startBtn->setStyleSheet(checkBtn->styleSheet());
        statusRow->addWidget(startBtn);

        auto* stopBtn = new QPushButton("Stop");
        stopBtn->setFixedWidth(50);
        stopBtn->setStyleSheet(checkBtn->styleSheet());
        statusRow->addWidget(stopBtn);
        statusRow->addStretch(1);
        bnrLayout->addLayout(statusRow);

        // Check container status
        auto checkStatus = [statusDot, statusLbl, nameEdit]() {
            QProcess proc;
            proc.start("docker", {"inspect", "-f", "{{.State.Status}}", nameEdit->text()});
            proc.waitForFinished(3000);
            QString out = proc.readAllStandardOutput().trimmed();
            QString err = proc.readAllStandardError().trimmed();
            qDebug() << "BNR checkStatus:" << out << err << "exit:" << proc.exitCode();
            if (out == "running") {
                statusDot->setStyleSheet("QLabel { color: #00d860; font-size: 10px; }");
                statusLbl->setText("Running");
            } else if (!out.isEmpty()) {
                statusDot->setStyleSheet("QLabel { color: #d8a000; font-size: 10px; }");
                statusLbl->setText("Stopped (" + out + ")");
            } else {
                statusDot->setStyleSheet("QLabel { color: #d83030; font-size: 10px; }");
                statusLbl->setText("Not found");
            }
        };

        connect(checkBtn, &QPushButton::clicked, this, checkStatus);

        connect(startBtn, &QPushButton::clicked, this,
                [nameEdit, statusLbl, checkStatus]() {
            auto* proc = new QProcess;
            connect(proc, &QProcess::finished, statusLbl, [proc, checkStatus]() {
                proc->deleteLater();
                checkStatus();
            });
            proc->start("docker", {"start", nameEdit->text()});
        });

        connect(stopBtn, &QPushButton::clicked, this,
                [nameEdit, statusLbl, checkStatus]() {
            auto* proc = new QProcess;
            connect(proc, &QProcess::finished, statusLbl, [proc, checkStatus]() {
                proc->deleteLater();
                checkStatus();
            });
            proc->start("docker", {"stop", nameEdit->text()});
        });

        connect(autoStart, &QPushButton::toggled, this, [](bool on) {
            AppSettings::instance().setValue("BnrAutostart", on ? "True" : "False");
        });
        connect(nameEdit, &QLineEdit::textChanged, this, [](const QString& name) {
            AppSettings::instance().setValue("BnrContainerName", name);
        });

        vbox->addWidget(bnrGroup);

        // Check on dialog open (context object ensures timer is cancelled if dialog closes)
        QTimer::singleShot(0, statusLbl, checkStatus);
    }
#endif

    vbox->addStretch(1);
    return page;
}

// ── Filters tab ─────────────────────────────────────────────────────────────

QWidget* RadioSetupDialog::buildFiltersTab()
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setSpacing(8);

    // Model header
    {
        auto* hdr = new QHBoxLayout;
        hdr->addStretch(1);
        auto* modelLbl = new QLabel(m_model->model());
        modelLbl->setStyleSheet("QLabel { color: #00c8ff; font-size: 20px; font-weight: bold; }");
        hdr->addWidget(modelLbl);
        vbox->addLayout(hdr);
    }

    static const QString kAutoBtn =
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
        "padding: 3px 10px; }"
        "QPushButton:checked { background: #0070c0; color: #ffffff; "
        "border: 1px solid #0090e0; }";

    static const QString kFilterSlider =
        "QSlider::groove:horizontal { background: #1a2a3a; height: 6px; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #c8d8e8; width: 14px; "
        "margin: -5px 0; border-radius: 7px; }";

    // Filter Options group
    {
        auto* group = new QGroupBox("Filter Options");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(8);

        // Column headers
        auto* lowLbl = new QLabel("Low Latency");
        lowLbl->setStyleSheet(kLabelStyle);
        lowLbl->setAlignment(Qt::AlignCenter);
        grid->addWidget(lowLbl, 0, 1);
        auto* sharpLbl = new QLabel("Sharp Filters");
        sharpLbl->setStyleSheet(kLabelStyle);
        sharpLbl->setAlignment(Qt::AlignCenter);
        grid->addWidget(sharpLbl, 0, 2);

        struct FilterRow {
            const char* label;
            const char* modeCmd;   // voice, cw, digital
            int level;
            bool autoOn;
        };
        FilterRow rows[] = {
            {"Voice:",   "voice",   m_model->filterSharpnessVoice(),   m_model->filterSharpnessVoiceAuto()},
            {"CW:",      "cw",      m_model->filterSharpnessCw(),      m_model->filterSharpnessCwAuto()},
            {"Digital:", "digital", m_model->filterSharpnessDigital(), m_model->filterSharpnessDigitalAuto()},
        };

        for (int i = 0; i < 3; ++i) {
            auto& r = rows[i];
            int row = i + 1;

            auto* lbl = new QLabel(r.label);
            lbl->setStyleSheet(kLabelStyle);
            grid->addWidget(lbl, row, 0);

            auto* slider = new QSlider(Qt::Horizontal);
            slider->setRange(0, 3);
            slider->setValue(r.level);
            slider->setStyleSheet(kFilterSlider);
            slider->setEnabled(!r.autoOn);
            grid->addWidget(slider, row, 1, 1, 2);

            auto* autoBtn = new QPushButton("Auto");
            autoBtn->setCheckable(true);
            autoBtn->setChecked(r.autoOn);
            autoBtn->setStyleSheet(kAutoBtn);
            grid->addWidget(autoBtn, row, 3);

            QString mode = QString::fromLatin1(r.modeCmd);
            connect(slider, &QSlider::valueChanged, this, [this, mode](int v) {
                m_model->sendCommand(
                    QString("radio filter_sharpness %1 level=%2").arg(mode).arg(v));
            });
            connect(autoBtn, &QPushButton::toggled, this, [this, slider, mode](bool on) {
                slider->setEnabled(!on);
                m_model->sendCommand(
                    QString("radio filter_sharpness %1 auto_level=%2").arg(mode).arg(on ? 1 : 0));
            });
        }

        vbox->addWidget(group);
    }

    // Low Latency Digital checkbox
    {
        auto* group = new QGroupBox;
        group->setStyleSheet(kGroupStyle);
        auto* hb = new QHBoxLayout(group);

        auto* chk = new QCheckBox("Use Low Latency Filters for Digital Modes");
        chk->setChecked(m_model->lowLatencyDigital());
        chk->setStyleSheet(
            "QCheckBox { color: #c8d8e8; font-size: 12px; spacing: 8px; }"
            "QCheckBox::indicator { width: 16px; height: 16px; "
            "border: 2px solid #506070; border-radius: 3px; background: #0a0a18; }"
            "QCheckBox::indicator:checked { background: #0070c0; border: 2px solid #00a0e0; }");
        connect(chk, &QCheckBox::toggled, this, [this](bool on) {
            m_model->sendCommand(
                QString("radio set low_latency_digital_modes=%1").arg(on ? 1 : 0));
        });
        hb->addWidget(chk);

        vbox->addWidget(group);
    }

    vbox->addStretch(1);
    return page;
}
QWidget* RadioSetupDialog::buildXvtrTab()
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setSpacing(8);

    // Model header
    {
        auto* hdr = new QHBoxLayout;
        hdr->addStretch(1);
        auto* modelLbl = new QLabel(m_model->model());
        modelLbl->setStyleSheet("QLabel { color: #00c8ff; font-size: 20px; font-weight: bold; }");
        hdr->addWidget(modelLbl);
        vbox->addLayout(hdr);
    }

    // Sub-tabs: one per XVTR + a "+" tab to add new
    auto* xvtrTabs = new QTabWidget;
    xvtrTabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #304050; background: #0f0f1a; }"
        "QTabBar::tab { background: #1a2a3a; color: #8aa8c0; "
        "border: 1px solid #304050; padding: 3px 10px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #0f0f1a; color: #c8d8e8; "
        "border-bottom-color: #0f0f1a; }");

    auto buildXvtrPage = [this, xvtrTabs](int idx, const RadioModel::XvtrInfo& x) {
        auto* pg = new QWidget;
        auto* grid = new QGridLayout(pg);
        grid->setSpacing(6);

        auto addField = [&](int row, int col, const QString& label, const QString& value,
                             bool editable = true) -> QLineEdit* {
            auto* lbl = new QLabel(label);
            lbl->setStyleSheet(kLabelStyle);
            grid->addWidget(lbl, row, col * 2);
            auto* edit = new QLineEdit(value);
            edit->setStyleSheet(kEditStyle);
            edit->setFixedWidth(100);
            edit->setReadOnly(!editable);
            grid->addWidget(edit, row, col * 2 + 1);
            return edit;
        };

        auto* nameEdit   = addField(0, 0, "Name:", x.name);
        auto* validLbl   = new QLabel(x.isValid ? "Valid" : "Invalid");
        validLbl->setStyleSheet(x.isValid
            ? "QLabel { color: #00c040; font-size: 12px; font-weight: bold; }"
            : "QLabel { color: #c04040; font-size: 12px; font-weight: bold; }");
        grid->addWidget(validLbl, 0, 3);

        auto* rfEdit     = addField(1, 0, "RF Freq (MHz):", QString::number(x.rfFreq, 'f', 3));
        auto* ifEdit     = addField(1, 1, "IF Freq (MHz):", QString::number(x.ifFreq, 'f', 3));
        auto* loEdit     = addField(2, 0, "LO Freq (MHz):", QString::number(x.rfFreq - x.ifFreq, 'f', 3), false);
        auto* errEdit    = addField(2, 1, "LO Error (Hz):", QString::number(x.loError, 'f', 0));
        auto* rxGainEdit = addField(3, 0, "RX Gain (dB):", QString::number(x.rxGain, 'f', 1));

        // RX Only toggle
        auto* rxOnlyLbl = new QLabel("RX Only:");
        rxOnlyLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(rxOnlyLbl, 3, 2);
        auto* rxOnlyBtn = new QPushButton(x.rxOnly ? "Enabled" : "Disabled");
        rxOnlyBtn->setCheckable(true);
        rxOnlyBtn->setChecked(x.rxOnly);
        rxOnlyBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
            "padding: 3px 10px; }"
            "QPushButton:checked { background: #1a5030; color: #00e060; "
            "border: 1px solid #20a040; }");
        connect(rxOnlyBtn, &QPushButton::toggled, this, [this, rxOnlyBtn, idx](bool on) {
            rxOnlyBtn->setText(on ? "Enabled" : "Disabled");
            m_model->sendCommand(
                QString("xvtr set %1 rx_only=%2").arg(idx).arg(on ? 1 : 0));
        });
        grid->addWidget(rxOnlyBtn, 3, 3);

        auto* maxPwrEdit = addField(4, 0, "Max Power (dBm):", QString::number(x.maxPower, 'f', 1));

        // Remove button
        auto* removeBtn = new QPushButton("Remove");
        removeBtn->setStyleSheet(
            "QPushButton { background: #3a1a1a; border: 1px solid #504040; "
            "border-radius: 3px; color: #ff6060; font-size: 11px; font-weight: bold; "
            "padding: 4px 16px; }"
            "QPushButton:hover { background: #502020; }");
        connect(removeBtn, &QPushButton::clicked, pg, [this, idx, xvtrTabs, pg] {
            m_model->sendCommand(QString("xvtr remove %1").arg(idx));
            int tabIdx = xvtrTabs->indexOf(pg);
            if (tabIdx >= 0) xvtrTabs->removeTab(tabIdx);
        });
        grid->addWidget(removeBtn, 4, 3);

        // Wire editable fields
        connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit, idx] {
            m_model->sendCommand(
                QString("xvtr set %1 name=%2").arg(idx).arg(nameEdit->text()));
        });
        auto updateLo = [rfEdit, ifEdit, loEdit] {
            double rf = rfEdit->text().toDouble();
            double ifF = ifEdit->text().toDouble();
            loEdit->setText(QString::number(rf - ifF, 'f', 3));
        };
        connect(rfEdit, &QLineEdit::editingFinished, this, [this, rfEdit, idx, updateLo] {
            m_model->sendCommand(
                QString("xvtr set %1 rf_freq=%2").arg(idx).arg(rfEdit->text()));
            updateLo();
        });
        connect(ifEdit, &QLineEdit::editingFinished, this, [this, ifEdit, idx, updateLo] {
            m_model->sendCommand(
                QString("xvtr set %1 if_freq=%2").arg(idx).arg(ifEdit->text()));
            updateLo();
        });
        connect(errEdit, &QLineEdit::editingFinished, this, [this, errEdit, idx] {
            m_model->sendCommand(
                QString("xvtr set %1 lo_error=%2").arg(idx).arg(errEdit->text()));
        });
        connect(rxGainEdit, &QLineEdit::editingFinished, this, [this, rxGainEdit, idx] {
            m_model->sendCommand(
                QString("xvtr set %1 rx_gain=%2").arg(idx).arg(rxGainEdit->text()));
        });
        connect(maxPwrEdit, &QLineEdit::editingFinished, this, [this, maxPwrEdit, idx] {
            m_model->sendCommand(
                QString("xvtr set %1 max_power=%2").arg(idx).arg(maxPwrEdit->text()));
        });

        return pg;
    };

    // Add existing XVTR pages
    const auto& xvtrs = m_model->xvtrList();
    for (auto it = xvtrs.constBegin(); it != xvtrs.constEnd(); ++it) {
        xvtrTabs->addTab(buildXvtrPage(it.key(), it.value()),
                          it.value().name.isEmpty() ? QString::number(it.key()) : it.value().name);
    }

    // "+" tab to add new
    auto* addPage = new QWidget;
    auto* addVb = new QVBoxLayout(addPage);
    auto* addBtn = new QPushButton("Create New Transverter");
    addBtn->setStyleSheet(
        "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 12px; font-weight: bold; "
        "padding: 8px 20px; }"
        "QPushButton:hover { background: #203040; }");
    connect(addBtn, &QPushButton::clicked, this, [this, xvtrTabs, buildXvtrPage] {
        m_model->sendCmdPublic("xvtr create",
            [this, xvtrTabs, buildXvtrPage](int code, const QString& body) {
                if (code != 0) return;
                // Wait briefly for the radio's status update to arrive
                QTimer::singleShot(300, this, [this, xvtrTabs, buildXvtrPage] {
                    // Find the newest XVTR that doesn't have a tab yet
                    const auto& xvtrs = m_model->xvtrList();
                    for (auto it = xvtrs.constBegin(); it != xvtrs.constEnd(); ++it) {
                        // Check if we already have a tab for this index
                        bool found = false;
                        for (int t = 0; t < xvtrTabs->count() - 1; ++t) {
                            if (xvtrTabs->tabText(t) == it.value().name ||
                                xvtrTabs->tabText(t) == QString::number(it.key()))
                                found = true;
                        }
                        if (!found) {
                            QString tabName = it.value().name.isEmpty()
                                ? QString("New") : it.value().name;
                            int insertIdx = xvtrTabs->count() - 1; // before "+"
                            xvtrTabs->insertTab(insertIdx,
                                buildXvtrPage(it.key(), it.value()), tabName);
                            xvtrTabs->setCurrentIndex(insertIdx);
                        }
                    }
                });
            });
    });
    addVb->addWidget(addBtn, 0, Qt::AlignCenter);
    addVb->addStretch(1);
    xvtrTabs->addTab(addPage, "+");

    vbox->addWidget(xvtrTabs);
    return page;
}

// ── USB Cables tab ───────────────────────────────────────────────────────────

QWidget* RadioSetupDialog::buildUsbCablesTab()
{
    auto* page = new QWidget;
    auto* hbox = new QHBoxLayout(page);
    hbox->setSpacing(6);

    auto* cableModel = &m_model->usbCableModel();

    // Style constants
    static const QString kCombo =
        "QComboBox { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 11px; padding: 2px 4px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #1a2a3a; color: #c8d8e8; "
        "selection-background-color: #00b4d8; }";
    static const QString kEdit =
        "QLineEdit { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 11px; padding: 2px 4px; }";
    static const QString kSpin =
        "QSpinBox { background: #1a2a3a; border: 1px solid #304050; "
        "color: #c8d8e8; font-size: 11px; padding: 2px; }";
    static const QString kCheck =
        "QCheckBox { color: #c8d8e8; font-size: 11px; }";

    // ── Left: cable list ────────────────────────────────────────────────
    auto* listGroup = new QGroupBox("Cables");
    listGroup->setStyleSheet(kGroupStyle);
    listGroup->setFixedWidth(180);
    auto* listLayout = new QVBoxLayout(listGroup);

    auto* cableList = new QListWidget;
    cableList->setStyleSheet(
        "QListWidget { background: #0a0a14; color: #c8d8e8; border: 1px solid #203040; "
        "font-size: 11px; }"
        "QListWidget::item { padding: 4px; }"
        "QListWidget::item:selected { background: #00b4d8; color: #0f0f1a; }");
    listLayout->addWidget(cableList);
    hbox->addWidget(listGroup);

    // ── Right: stacked property panels ──────────────────────────────────
    auto* stack = new QStackedWidget;

    // Page 0: No cable selected
    {
        auto* empty = new QWidget;
        auto* emptyLayout = new QVBoxLayout(empty);
        auto* lbl = new QLabel("No USB cables detected.\n\nPlug a USB-serial adapter\n"
                               "into the radio's rear USB port.");
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet("QLabel { color: #606880; font-size: 12px; }");
        emptyLayout->addWidget(lbl);
        stack->addWidget(empty);  // index 0
    }

    // Helper: create source combo (shared across CAT, BCD, Bit)
    auto makeSourceCombo = []() {
        auto* combo = new QComboBox;
        combo->addItems({"None", "TX Pan", "TX Slice", "Active Slice",
                         "TX Ant", "RX Ant", "Ordinal Slice"});
        combo->setStyleSheet(kCombo);
        return combo;
    };
    // Map source display name → protocol value
    auto sourceToProto = [](const QString& display) -> QString {
        if (display == "TX Pan")        return "tx_pan";
        if (display == "TX Slice")      return "tx_slice";
        if (display == "Active Slice")  return "active_slice";
        if (display == "TX Ant")        return "tx_ant";
        if (display == "RX Ant")        return "rx_ant";
        if (display == "Ordinal Slice") return "ordinal_slice";
        return "None";
    };
    auto protoToSource = [](const QString& proto) -> int {
        if (proto == "tx_pan")        return 1;
        if (proto == "tx_slice")      return 2;
        if (proto == "active_slice")  return 3;
        if (proto == "tx_ant")        return 4;
        if (proto == "rx_ant")        return 5;
        if (proto == "ordinal_slice") return 6;
        return 0;  // None
    };

    // Helper: serial parameter group (shared by CAT and Passthrough)
    auto makeSerialGroup = [](const QString& title) {
        auto* group = new QGroupBox(title);
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(4);

        auto* speedCombo = new QComboBox;
        for (int s : {300,600,1200,2400,4800,9600,14400,19200,38400,57600,115200,230400,460800,921600})
            speedCombo->addItem(QString::number(s));
        speedCombo->setCurrentText("9600");
        speedCombo->setStyleSheet(kCombo);
        grid->addWidget(new QLabel("Speed:"), 0, 0);
        grid->addWidget(speedCombo, 0, 1);

        auto* dataCombo = new QComboBox;
        dataCombo->addItems({"7", "8"});
        dataCombo->setCurrentText("8");
        dataCombo->setStyleSheet(kCombo);
        grid->addWidget(new QLabel("Data Bits:"), 1, 0);
        grid->addWidget(dataCombo, 1, 1);

        auto* parityCombo = new QComboBox;
        parityCombo->addItems({"none", "odd", "even", "mark", "space"});
        parityCombo->setStyleSheet(kCombo);
        grid->addWidget(new QLabel("Parity:"), 2, 0);
        grid->addWidget(parityCombo, 2, 1);

        auto* stopCombo = new QComboBox;
        stopCombo->addItems({"1", "2"});
        stopCombo->setStyleSheet(kCombo);
        grid->addWidget(new QLabel("Stop Bits:"), 3, 0);
        grid->addWidget(stopCombo, 3, 1);

        auto* flowCombo = new QComboBox;
        flowCombo->addItems({"none", "rts_cts", "dtr_dsr", "xon_xoff"});
        flowCombo->setStyleSheet(kCombo);
        grid->addWidget(new QLabel("Flow:"), 4, 0);
        grid->addWidget(flowCombo, 4, 1);

        struct SerialWidgets { QComboBox *speed, *data, *parity, *stop, *flow; QGroupBox* group; };
        auto* w = new SerialWidgets{speedCombo, dataCombo, parityCombo, stopCombo, flowCombo, group};
        group->setProperty("_widgets", QVariant::fromValue(static_cast<void*>(w)));
        return group;
    };

    // Page 1: CAT cable
    QWidget* catPage;
    QLineEdit* catNameEdit;
    QCheckBox* catEnabledCheck;
    QLabel*    catStatusLabel;
    QComboBox* catSourceCombo;
    QCheckBox* catAutoReportCheck;
    QGroupBox* catSerialGroup;
    {
        catPage = new QWidget;
        auto* vbox = new QVBoxLayout(catPage);
        vbox->setSpacing(6);

        // Common header
        auto* headerGroup = new QGroupBox("Cable Settings");
        headerGroup->setStyleSheet(kGroupStyle);
        auto* hg = new QGridLayout(headerGroup);
        hg->setSpacing(4);
        hg->addWidget(new QLabel("Name:"), 0, 0);
        catNameEdit = new QLineEdit;
        catNameEdit->setStyleSheet(kEdit);
        hg->addWidget(catNameEdit, 0, 1);
        catEnabledCheck = new QCheckBox("Enabled");
        catEnabledCheck->setStyleSheet(kCheck);
        hg->addWidget(catEnabledCheck, 1, 0, 1, 2);
        catStatusLabel = new QLabel("Unplugged");
        catStatusLabel->setStyleSheet("QLabel { color: #808080; font-size: 11px; }");
        hg->addWidget(new QLabel("Status:"), 2, 0);
        hg->addWidget(catStatusLabel, 2, 1);
        vbox->addWidget(headerGroup);

        // Serial params
        catSerialGroup = makeSerialGroup("Serial Parameters");
        vbox->addWidget(catSerialGroup);

        // CAT source
        auto* srcGroup = new QGroupBox("CAT Source");
        srcGroup->setStyleSheet(kGroupStyle);
        auto* sg = new QGridLayout(srcGroup);
        sg->setSpacing(4);
        sg->addWidget(new QLabel("Source:"), 0, 0);
        catSourceCombo = makeSourceCombo();
        sg->addWidget(catSourceCombo, 0, 1);
        catAutoReportCheck = new QCheckBox("Auto Report");
        catAutoReportCheck->setStyleSheet(kCheck);
        sg->addWidget(catAutoReportCheck, 1, 0, 1, 2);
        vbox->addWidget(srcGroup);

        vbox->addStretch();
        stack->addWidget(catPage);  // index 1
    }

    // Page 2: BCD cable
    QWidget* bcdPage;
    QLineEdit* bcdNameEdit;
    QCheckBox* bcdEnabledCheck;
    QLabel*    bcdStatusLabel;
    QComboBox* bcdSourceCombo;
    QComboBox* bcdTypeCombo;
    QComboBox* bcdPolarityCombo;
    {
        bcdPage = new QWidget;
        auto* vbox = new QVBoxLayout(bcdPage);
        vbox->setSpacing(6);

        auto* headerGroup = new QGroupBox("Cable Settings");
        headerGroup->setStyleSheet(kGroupStyle);
        auto* hg = new QGridLayout(headerGroup);
        hg->setSpacing(4);
        hg->addWidget(new QLabel("Name:"), 0, 0);
        bcdNameEdit = new QLineEdit;
        bcdNameEdit->setStyleSheet(kEdit);
        hg->addWidget(bcdNameEdit, 0, 1);
        bcdEnabledCheck = new QCheckBox("Enabled");
        bcdEnabledCheck->setStyleSheet(kCheck);
        hg->addWidget(bcdEnabledCheck, 1, 0, 1, 2);
        bcdStatusLabel = new QLabel("Unplugged");
        bcdStatusLabel->setStyleSheet("QLabel { color: #808080; font-size: 11px; }");
        hg->addWidget(new QLabel("Status:"), 2, 0);
        hg->addWidget(bcdStatusLabel, 2, 1);
        vbox->addWidget(headerGroup);

        auto* bcdGroup = new QGroupBox("BCD Settings");
        bcdGroup->setStyleSheet(kGroupStyle);
        auto* bg = new QGridLayout(bcdGroup);
        bg->setSpacing(4);
        bg->addWidget(new QLabel("BCD Type:"), 0, 0);
        bcdTypeCombo = new QComboBox;
        bcdTypeCombo->addItems({"HF (bcd)", "VHF (vbcd)", "HF+VHF (bcd_vbcd)"});
        bcdTypeCombo->setStyleSheet(kCombo);
        bg->addWidget(bcdTypeCombo, 0, 1);
        bg->addWidget(new QLabel("Polarity:"), 1, 0);
        bcdPolarityCombo = new QComboBox;
        bcdPolarityCombo->addItems({"Active High", "Active Low"});
        bcdPolarityCombo->setStyleSheet(kCombo);
        bg->addWidget(bcdPolarityCombo, 1, 1);
        bg->addWidget(new QLabel("Source:"), 2, 0);
        bcdSourceCombo = makeSourceCombo();
        bg->addWidget(bcdSourceCombo, 2, 1);
        vbox->addWidget(bcdGroup);

        vbox->addStretch();
        stack->addWidget(bcdPage);  // index 2
    }

    // Page 3: Bit cable
    QWidget* bitPage;
    QLineEdit* bitNameEdit;
    QCheckBox* bitEnabledCheck;
    QLabel*    bitStatusLabel;
    {
        bitPage = new QWidget;
        auto* vbox = new QVBoxLayout(bitPage);
        vbox->setSpacing(6);

        auto* headerGroup = new QGroupBox("Cable Settings");
        headerGroup->setStyleSheet(kGroupStyle);
        auto* hg = new QGridLayout(headerGroup);
        hg->setSpacing(4);
        hg->addWidget(new QLabel("Name:"), 0, 0);
        bitNameEdit = new QLineEdit;
        bitNameEdit->setStyleSheet(kEdit);
        hg->addWidget(bitNameEdit, 0, 1);
        bitEnabledCheck = new QCheckBox("Enabled");
        bitEnabledCheck->setStyleSheet(kCheck);
        hg->addWidget(bitEnabledCheck, 1, 0, 1, 2);
        bitStatusLabel = new QLabel("Unplugged");
        bitStatusLabel->setStyleSheet("QLabel { color: #808080; font-size: 11px; }");
        hg->addWidget(new QLabel("Status:"), 2, 0);
        hg->addWidget(bitStatusLabel, 2, 1);
        vbox->addWidget(headerGroup);

        // 8-row bit grid
        auto* bitGroup = new QGroupBox("Bit Configuration (0-7)");
        bitGroup->setStyleSheet(kGroupStyle);
        auto* bitGrid = new QGridLayout(bitGroup);
        bitGrid->setSpacing(2);

        // Header row
        int col = 0;
        for (const auto& h : {"Bit", "En", "Source", "Output", "Polarity", "Band"}) {
            auto* lbl = new QLabel(h);
            lbl->setStyleSheet("QLabel { color: #8aa8c0; font-size: 10px; font-weight: bold; }");
            lbl->setAlignment(Qt::AlignCenter);
            bitGrid->addWidget(lbl, 0, col++);
        }

        for (int b = 0; b < 8; ++b) {
            int row = b + 1;
            auto* bitLabel = new QLabel(QString::number(b));
            bitLabel->setAlignment(Qt::AlignCenter);
            bitLabel->setStyleSheet("QLabel { color: #c8d8e8; font-size: 10px; }");
            bitGrid->addWidget(bitLabel, row, 0);

            auto* enCheck = new QCheckBox;
            bitGrid->addWidget(enCheck, row, 1, Qt::AlignCenter);

            auto* srcCombo = new QComboBox;
            srcCombo->addItems({"None", "Active Slice", "TX Slice"});
            srcCombo->setStyleSheet(kCombo + "QComboBox { font-size: 9px; }");
            srcCombo->setFixedWidth(90);
            bitGrid->addWidget(srcCombo, row, 2);

            auto* outCombo = new QComboBox;
            outCombo->addItems({"band", "freq_range"});
            outCombo->setStyleSheet(kCombo + "QComboBox { font-size: 9px; }");
            outCombo->setFixedWidth(80);
            bitGrid->addWidget(outCombo, row, 3);

            auto* polCombo = new QComboBox;
            polCombo->addItems({"High", "Low"});
            polCombo->setStyleSheet(kCombo + "QComboBox { font-size: 9px; }");
            polCombo->setFixedWidth(50);
            bitGrid->addWidget(polCombo, row, 4);

            auto* bandEdit = new QLineEdit;
            bandEdit->setPlaceholderText("e.g. 20");
            bandEdit->setFixedWidth(50);
            bandEdit->setStyleSheet(kEdit + "QLineEdit { font-size: 9px; }");
            bitGrid->addWidget(bandEdit, row, 5);

            // Wire signals to send commands
            connect(enCheck, &QCheckBox::toggled, this, [cableModel, cableList, b](bool on) {
                auto* item = cableList->currentItem();
                if (!item) return;
                cableModel->sendSetBit(item->data(Qt::UserRole).toString(), b,
                                       "enable", on ? "1" : "0");
            });
            connect(outCombo, &QComboBox::currentTextChanged, this,
                    [cableModel, cableList, b](const QString& text) {
                auto* item = cableList->currentItem();
                if (!item) return;
                cableModel->sendSetBit(item->data(Qt::UserRole).toString(), b, "output", text);
            });
            connect(polCombo, &QComboBox::currentTextChanged, this,
                    [cableModel, cableList, b](const QString& text) {
                auto* item = cableList->currentItem();
                if (!item) return;
                cableModel->sendSetBit(item->data(Qt::UserRole).toString(), b,
                                       "polarity", text == "High" ? "active_high" : "active_low");
            });
            connect(bandEdit, &QLineEdit::editingFinished, this,
                    [cableModel, cableList, b, bandEdit]() {
                auto* item = cableList->currentItem();
                if (!item) return;
                cableModel->sendSetBit(item->data(Qt::UserRole).toString(), b,
                                       "band", bandEdit->text());
            });
        }

        vbox->addWidget(bitGroup);
        vbox->addStretch();
        stack->addWidget(bitPage);  // index 3
    }

    // Page 4: Passthrough cable
    QWidget* ptPage;
    QLineEdit* ptNameEdit;
    QCheckBox* ptEnabledCheck;
    QLabel*    ptStatusLabel;
    QGroupBox* ptSerialGroup;
    {
        ptPage = new QWidget;
        auto* vbox = new QVBoxLayout(ptPage);
        vbox->setSpacing(6);

        auto* headerGroup = new QGroupBox("Cable Settings");
        headerGroup->setStyleSheet(kGroupStyle);
        auto* hg = new QGridLayout(headerGroup);
        hg->setSpacing(4);
        hg->addWidget(new QLabel("Name:"), 0, 0);
        ptNameEdit = new QLineEdit;
        ptNameEdit->setStyleSheet(kEdit);
        hg->addWidget(ptNameEdit, 0, 1);
        ptEnabledCheck = new QCheckBox("Enabled");
        ptEnabledCheck->setStyleSheet(kCheck);
        hg->addWidget(ptEnabledCheck, 1, 0, 1, 2);
        ptStatusLabel = new QLabel("Unplugged");
        ptStatusLabel->setStyleSheet("QLabel { color: #808080; font-size: 11px; }");
        hg->addWidget(new QLabel("Status:"), 2, 0);
        hg->addWidget(ptStatusLabel, 2, 1);
        vbox->addWidget(headerGroup);

        ptSerialGroup = makeSerialGroup("Serial Parameters");
        vbox->addWidget(ptSerialGroup);

        vbox->addStretch();
        stack->addWidget(ptPage);  // index 4
    }

    hbox->addWidget(stack, 1);

    // ── Populate cable list from model ──────────────────────────────────
    auto refreshList = [cableList, cableModel]() {
        QString prevSn;
        if (cableList->currentItem())
            prevSn = cableList->currentItem()->data(Qt::UserRole).toString();
        cableList->clear();
        for (auto it = cableModel->cables().begin(); it != cableModel->cables().end(); ++it) {
            const auto& cable = it.value();
            QString label = cable.name.isEmpty() ? cable.serialNumber : cable.name;
            label += QString(" [%1]").arg(cable.type.toUpper());
            if (!cable.present)
                label += " (unplugged)";
            auto* item = new QListWidgetItem(label);
            item->setData(Qt::UserRole, cable.serialNumber);
            if (cable.enabled && cable.present)
                item->setForeground(QColor("#30d050"));
            else if (cable.enabled)
                item->setForeground(QColor("#d0d030"));
            else
                item->setForeground(QColor("#808080"));
            cableList->addItem(item);
            if (cable.serialNumber == prevSn)
                cableList->setCurrentItem(item);
        }
    };

    // ── Select cable → show properties ──────────────────────────────────
    auto showCableProps = [=](const QString& sn) {
        if (sn.isEmpty() || !cableModel->cables().contains(sn)) {
            stack->setCurrentIndex(0);
            return;
        }
        const auto& cable = cableModel->cables()[sn];
        const QString& t = cable.type;

        if (t == "cat") {
            stack->setCurrentIndex(1);
            QSignalBlocker b1(catNameEdit), b2(catEnabledCheck), b3(catSourceCombo), b4(catAutoReportCheck);
            catNameEdit->setText(cable.name);
            catEnabledCheck->setChecked(cable.enabled);
            catStatusLabel->setText(cable.present ? "Plugged In" : "Unplugged");
            catStatusLabel->setStyleSheet(cable.present
                ? "QLabel { color: #30d050; font-size: 11px; }"
                : "QLabel { color: #808080; font-size: 11px; }");
            catSourceCombo->setCurrentIndex(protoToSource(cable.source));
            catAutoReportCheck->setChecked(cable.autoReport);
        } else if (t == "bcd" || t == "vbcd" || t == "bcd_vbcd") {
            stack->setCurrentIndex(2);
            QSignalBlocker b1(bcdNameEdit), b2(bcdEnabledCheck), b3(bcdSourceCombo),
                           b4(bcdTypeCombo), b5(bcdPolarityCombo);
            bcdNameEdit->setText(cable.name);
            bcdEnabledCheck->setChecked(cable.enabled);
            bcdStatusLabel->setText(cable.present ? "Plugged In" : "Unplugged");
            bcdStatusLabel->setStyleSheet(cable.present
                ? "QLabel { color: #30d050; font-size: 11px; }"
                : "QLabel { color: #808080; font-size: 11px; }");
            bcdSourceCombo->setCurrentIndex(protoToSource(cable.source));
            if (t == "vbcd") bcdTypeCombo->setCurrentIndex(1);
            else if (t == "bcd_vbcd") bcdTypeCombo->setCurrentIndex(2);
            else bcdTypeCombo->setCurrentIndex(0);
            bcdPolarityCombo->setCurrentIndex(cable.activeHigh ? 0 : 1);
        } else if (t == "bit") {
            stack->setCurrentIndex(3);
            QSignalBlocker b1(bitNameEdit), b2(bitEnabledCheck);
            bitNameEdit->setText(cable.name);
            bitEnabledCheck->setChecked(cable.enabled);
            bitStatusLabel->setText(cable.present ? "Plugged In" : "Unplugged");
            bitStatusLabel->setStyleSheet(cable.present
                ? "QLabel { color: #30d050; font-size: 11px; }"
                : "QLabel { color: #808080; font-size: 11px; }");
            // Update bit grid rows
            auto* bitGroup = bitPage->findChild<QGroupBox*>("Bit Configuration (0-7)");
            // Bit grid cells are updated by index in the grid layout — skip for now,
            // per-bit UI refresh would iterate the grid children
        } else if (t == "passthrough") {
            stack->setCurrentIndex(4);
            QSignalBlocker b1(ptNameEdit), b2(ptEnabledCheck);
            ptNameEdit->setText(cable.name);
            ptEnabledCheck->setChecked(cable.enabled);
            ptStatusLabel->setText(cable.present ? "Plugged In" : "Unplugged");
            ptStatusLabel->setStyleSheet(cable.present
                ? "QLabel { color: #30d050; font-size: 11px; }"
                : "QLabel { color: #808080; font-size: 11px; }");
        } else {
            stack->setCurrentIndex(0);
        }
    };

    connect(cableList, &QListWidget::currentItemChanged, this,
            [showCableProps](QListWidgetItem* current, QListWidgetItem*) {
        if (current)
            showCableProps(current->data(Qt::UserRole).toString());
    });

    // ── Wire model signals ──────────────────────────────────────────────
    connect(cableModel, &UsbCableModel::cableAdded, this, [refreshList](const QString&) {
        refreshList();
    });
    connect(cableModel, &UsbCableModel::cableRemoved, this, [refreshList, stack](const QString&) {
        refreshList();
        stack->setCurrentIndex(0);
    });
    connect(cableModel, &UsbCableModel::cableChanged, this,
            [refreshList, cableList, showCableProps](const QString& sn) {
        refreshList();
        if (cableList->currentItem() &&
            cableList->currentItem()->data(Qt::UserRole).toString() == sn)
            showCableProps(sn);
    });

    // ── Wire property edits → commands ──────────────────────────────────
    // CAT
    auto sendCatProp = [cableModel, cableList](const QString& key, const QString& val) {
        auto* item = cableList->currentItem();
        if (!item) return;
        cableModel->sendSet(item->data(Qt::UserRole).toString(), key, val);
    };
    connect(catNameEdit, &QLineEdit::editingFinished, this, [catNameEdit, sendCatProp]() {
        sendCatProp("name", QString(catNameEdit->text()).replace(' ', QChar(0x7F)));
    });
    connect(catEnabledCheck, &QCheckBox::toggled, this, [sendCatProp](bool on) {
        sendCatProp("enable", on ? "1" : "0");
    });
    connect(catSourceCombo, &QComboBox::currentTextChanged, this,
            [sendCatProp, sourceToProto](const QString& text) {
        sendCatProp("source", sourceToProto(text));
    });
    connect(catAutoReportCheck, &QCheckBox::toggled, this, [sendCatProp](bool on) {
        sendCatProp("auto_report", on ? "1" : "0");
    });

    // BCD
    auto sendBcdProp = [cableModel, cableList](const QString& key, const QString& val) {
        auto* item = cableList->currentItem();
        if (!item) return;
        cableModel->sendSet(item->data(Qt::UserRole).toString(), key, val);
    };
    connect(bcdNameEdit, &QLineEdit::editingFinished, this, [bcdNameEdit, sendBcdProp]() {
        sendBcdProp("name", QString(bcdNameEdit->text()).replace(' ', QChar(0x7F)));
    });
    connect(bcdEnabledCheck, &QCheckBox::toggled, this, [sendBcdProp](bool on) {
        sendBcdProp("enable", on ? "1" : "0");
    });
    connect(bcdTypeCombo, &QComboBox::currentIndexChanged, this,
            [sendBcdProp](int idx) {
        static const char* types[] = {"bcd", "vbcd", "bcd_vbcd"};
        if (idx >= 0 && idx < 3) sendBcdProp("type", types[idx]);
    });
    connect(bcdPolarityCombo, &QComboBox::currentIndexChanged, this,
            [sendBcdProp](int idx) {
        sendBcdProp("polarity", idx == 0 ? "active_high" : "active_low");
    });
    connect(bcdSourceCombo, &QComboBox::currentTextChanged, this,
            [sendBcdProp, sourceToProto](const QString& text) {
        sendBcdProp("source", sourceToProto(text));
    });

    // Bit cable header
    auto sendBitProp = [cableModel, cableList](const QString& key, const QString& val) {
        auto* item = cableList->currentItem();
        if (!item) return;
        cableModel->sendSet(item->data(Qt::UserRole).toString(), key, val);
    };
    connect(bitNameEdit, &QLineEdit::editingFinished, this, [bitNameEdit, sendBitProp]() {
        sendBitProp("name", QString(bitNameEdit->text()).replace(' ', QChar(0x7F)));
    });
    connect(bitEnabledCheck, &QCheckBox::toggled, this, [sendBitProp](bool on) {
        sendBitProp("enable", on ? "1" : "0");
    });

    // Passthrough
    auto sendPtProp = [cableModel, cableList](const QString& key, const QString& val) {
        auto* item = cableList->currentItem();
        if (!item) return;
        cableModel->sendSet(item->data(Qt::UserRole).toString(), key, val);
    };
    connect(ptNameEdit, &QLineEdit::editingFinished, this, [ptNameEdit, sendPtProp]() {
        sendPtProp("name", QString(ptNameEdit->text()).replace(' ', QChar(0x7F)));
    });
    connect(ptEnabledCheck, &QCheckBox::toggled, this, [sendPtProp](bool on) {
        sendPtProp("enable", on ? "1" : "0");
    });

    // Initial populate
    refreshList();

    return page;
}

#ifdef HAVE_SERIALPORT
QWidget* RadioSetupDialog::buildSerialTab()
{
    auto* page = new QWidget;
    auto* vbox = new QVBoxLayout(page);
    vbox->setSpacing(8);
    vbox->setContentsMargins(8, 8, 8, 8);

    const QString kLabelStyle = "QLabel { color: #8898a8; font-size: 11px; }";
    const QString kGroupStyle = "QGroupBox { color: #00b4d8; font-size: 12px; border: 1px solid #203040; "
                                "border-radius: 4px; margin-top: 6px; padding-top: 14px; } "
                                "QGroupBox::title { subcontrol-origin: margin; left: 8px; }";

    auto& settings = AppSettings::instance();

    // ── Port Configuration ───────────────────────────────────────────────
    {
        auto* group = new QGroupBox("Port Configuration");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(4);

        // Port selector
        grid->addWidget(new QLabel("Port:"), 0, 0);
        auto* portCombo = new QComboBox;
        portCombo->setMinimumWidth(200);
        for (const auto& info : QSerialPortInfo::availablePorts())
            portCombo->addItem(QString("%1 — %2").arg(info.portName(), info.description()),
                               info.portName());
        QString savedPort = settings.value("SerialPortName", "").toString();
        for (int i = 0; i < portCombo->count(); ++i) {
            if (portCombo->itemData(i).toString() == savedPort) {
                portCombo->setCurrentIndex(i);
                break;
            }
        }
        grid->addWidget(portCombo, 0, 1, 1, 2);

        auto* refreshBtn = new QPushButton("Refresh");
        refreshBtn->setFixedHeight(24);
        connect(refreshBtn, &QPushButton::clicked, this, [portCombo]() {
            portCombo->clear();
            for (const auto& info : QSerialPortInfo::availablePorts())
                portCombo->addItem(QString("%1 — %2").arg(info.portName(), info.description()),
                                   info.portName());
        });
        grid->addWidget(refreshBtn, 0, 3);

        // Baud rate
        grid->addWidget(new QLabel("Baud:"), 1, 0);
        auto* baudCombo = new QComboBox;
        for (int b : {9600, 19200, 38400, 57600, 115200})
            baudCombo->addItem(QString::number(b), b);
        int savedBaud = settings.value("SerialBaudRate", "9600").toInt();
        baudCombo->setCurrentIndex(baudCombo->findData(savedBaud));
        grid->addWidget(baudCombo, 1, 1);

        // Data bits
        grid->addWidget(new QLabel("Data:"), 1, 2);
        auto* dataCombo = new QComboBox;
        dataCombo->addItem("8", 8);
        dataCombo->addItem("7", 7);
        grid->addWidget(dataCombo, 1, 3);

        // Parity
        grid->addWidget(new QLabel("Parity:"), 2, 0);
        auto* parityCombo = new QComboBox;
        parityCombo->addItem("None", 0);
        parityCombo->addItem("Even", 2);
        parityCombo->addItem("Odd", 3);
        grid->addWidget(parityCombo, 2, 1);

        // Stop bits
        grid->addWidget(new QLabel("Stop:"), 2, 2);
        auto* stopCombo = new QComboBox;
        stopCombo->addItem("1", 1);
        stopCombo->addItem("2", 2);
        grid->addWidget(stopCombo, 2, 3);

        vbox->addWidget(group);

        // Save port settings on any change
        auto savePort = [portCombo, baudCombo, dataCombo, parityCombo, stopCombo]() {
            auto& s = AppSettings::instance();
            s.setValue("SerialPortName", portCombo->currentData().toString());
            s.setValue("SerialBaudRate", baudCombo->currentData().toString());
            s.setValue("SerialDataBits", dataCombo->currentData().toString());
            s.setValue("SerialParity", parityCombo->currentData().toString());
            s.setValue("SerialStopBits", stopCombo->currentData().toString());
            s.save();
        };
        connect(portCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, savePort);
        connect(baudCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, savePort);
    }

    // ── Pin Assignment ───────────────────────────────────────────────────
    {
        auto* group = new QGroupBox("Pin Assignment");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(4);

        auto* headerPin = new QLabel("Pin");
        auto* headerFn  = new QLabel("Function");
        auto* headerPol = new QLabel("Polarity");
        headerPin->setStyleSheet(kLabelStyle);
        headerFn->setStyleSheet(kLabelStyle);
        headerPol->setStyleSheet(kLabelStyle);
        grid->addWidget(headerPin, 0, 0);
        grid->addWidget(headerFn,  0, 1);
        grid->addWidget(headerPol, 0, 2);

        auto makeFnCombo = [this](const QString& savedKey) {
            auto* combo = new QComboBox;
            combo->addItem("None",   "None");
            combo->addItem("PTT",    "PTT");
            combo->addItem("CW Key", "CwKey");
            combo->addItem("CW PTT", "CwPTT");
            QString saved = AppSettings::instance().value(savedKey, "None").toString();
            for (int i = 0; i < combo->count(); ++i)
                if (combo->itemData(i).toString() == saved) { combo->setCurrentIndex(i); break; }
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [savedKey, combo]() {
                auto& s = AppSettings::instance();
                s.setValue(savedKey, combo->currentData().toString());
                s.save();
            });
            return combo;
        };

        auto makePolCombo = [this](const QString& savedKey) {
            auto* combo = new QComboBox;
            combo->addItem("Active High", "ActiveHigh");
            combo->addItem("Active Low",  "ActiveLow");
            QString saved = AppSettings::instance().value(savedKey, "ActiveHigh").toString();
            combo->setCurrentIndex(saved == "ActiveLow" ? 1 : 0);
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [savedKey, combo]() {
                auto& s = AppSettings::instance();
                s.setValue(savedKey, combo->currentData().toString());
                s.save();
            });
            return combo;
        };

        // DTR row
        grid->addWidget(new QLabel("DTR"), 1, 0);
        grid->addWidget(makeFnCombo("SerialDtrFunction"), 1, 1);
        grid->addWidget(makePolCombo("SerialDtrPolarity"), 1, 2);

        // RTS row
        grid->addWidget(new QLabel("RTS"), 2, 0);
        grid->addWidget(makeFnCombo("SerialRtsFunction"), 2, 1);
        grid->addWidget(makePolCombo("SerialRtsPolarity"), 2, 2);

        // Input pin function combo (different options than output)
        auto makeInputFnCombo = [this](const QString& savedKey) {
            auto* combo = new QComboBox;
            combo->addItem("None",         "None");
            combo->addItem("PTT Input",    "PttInput");
            combo->addItem("CW Key Input", "CwKeyInput");
            combo->addItem("CW Dit Input", "CwDitInput");
            combo->addItem("CW Dah Input", "CwDahInput");
            QString saved = AppSettings::instance().value(savedKey, "None").toString();
            for (int i = 0; i < combo->count(); ++i)
                if (combo->itemData(i).toString() == saved) { combo->setCurrentIndex(i); break; }
            connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [savedKey, combo]() {
                auto& s = AppSettings::instance();
                s.setValue(savedKey, combo->currentData().toString());
                s.save();
            });
            return combo;
        };

        // CTS row (input)
        auto* ctsLabel = new QLabel("CTS");
        ctsLabel->setStyleSheet("QLabel { color: #60a0c0; }");
        grid->addWidget(ctsLabel, 3, 0);
        grid->addWidget(makeInputFnCombo("SerialCtsFunction"), 3, 1);
        grid->addWidget(makePolCombo("SerialCtsPolarity"), 3, 2);

        // DSR row (input)
        auto* dsrLabel = new QLabel("DSR");
        dsrLabel->setStyleSheet("QLabel { color: #60a0c0; }");
        grid->addWidget(dsrLabel, 4, 0);
        grid->addWidget(makeInputFnCombo("SerialDsrFunction"), 4, 1);
        grid->addWidget(makePolCombo("SerialDsrPolarity"), 4, 2);

        // Paddle swap
        auto* swapCb = new QCheckBox("Paddle Swap (swap dit/dah)");
        swapCb->setStyleSheet("QCheckBox { color: #c8d8e8; }");
        swapCb->setChecked(AppSettings::instance().value("SerialPaddleSwap", "False").toString() == "True");
        connect(swapCb, &QCheckBox::toggled, this, [](bool on) {
            auto& s = AppSettings::instance();
            s.setValue("SerialPaddleSwap", on ? "True" : "False");
            s.save();
        });
        grid->addWidget(swapCb, 5, 0, 1, 3);

        vbox->addWidget(group);
    }

    // ── Auto-open toggle ─────────────────────────────────────────────────
    {
        auto* autoOpen = new QCheckBox("Auto-open serial port on startup");
        autoOpen->setStyleSheet("QCheckBox { color: #c8d8e8; }");
        autoOpen->setChecked(settings.value("SerialAutoOpen", "False").toString() == "True");
        connect(autoOpen, &QCheckBox::toggled, this, [](bool on) {
            auto& s = AppSettings::instance();
            s.setValue("SerialAutoOpen", on ? "True" : "False");
            s.save();
        });
        vbox->addWidget(autoOpen);
    }

    // ── FlexControl tuning knob ────────────────────────────────────────
    {
        auto* group = new QGroupBox("FlexControl Tuning Knob");
        group->setStyleSheet(kGroupStyle);
        auto* grid = new QGridLayout(group);
        grid->setSpacing(6);

        // Status
        auto* fcStatusLabel = new QLabel("Not detected");
        fcStatusLabel->setStyleSheet("QLabel { color: #808080; font-size: 11px; }");
        grid->addWidget(new QLabel("Status:"), 0, 0);
        grid->addWidget(fcStatusLabel, 0, 1);

        // Detect / Close buttons
        auto* fcDetectBtn = new QPushButton("Detect");
        fcDetectBtn->setFixedWidth(80);
        fcDetectBtn->setStyleSheet(
            "QPushButton { background: #00b4d8; color: #0f0f1a; font-weight: bold; "
            "border: 1px solid #008ba8; padding: 3px; border-radius: 3px; }"
            "QPushButton:hover { background: #00c8f0; }");
        auto* fcCloseBtn = new QPushButton("Close");
        fcCloseBtn->setFixedWidth(80);
        fcCloseBtn->setStyleSheet(fcDetectBtn->styleSheet());
        fcCloseBtn->setEnabled(false);

        auto* btnRow = new QHBoxLayout;
        btnRow->addWidget(fcDetectBtn);
        btnRow->addWidget(fcCloseBtn);
        btnRow->addStretch();
        grid->addLayout(btnRow, 0, 2);

        // Update status display
        auto updateFcStatus = [fcStatusLabel, fcCloseBtn, fcDetectBtn]
                              (bool connected, const QString& port = {}) {
            if (connected) {
                fcStatusLabel->setText(QString("Connected (%1)").arg(port));
                fcStatusLabel->setStyleSheet("QLabel { color: #30d050; font-size: 11px; }");
                fcCloseBtn->setEnabled(true);
                fcDetectBtn->setEnabled(false);
            } else {
                fcStatusLabel->setText("Not detected");
                fcStatusLabel->setStyleSheet("QLabel { color: #808080; font-size: 11px; }");
                fcCloseBtn->setEnabled(false);
                fcDetectBtn->setEnabled(true);
            }
        };

        connect(fcDetectBtn, &QPushButton::clicked, this, [updateFcStatus] {
            QString port = FlexControlManager::detectPort();
            if (port.isEmpty()) {
                updateFcStatus(false);
                return;
            }
            updateFcStatus(true, port);
            // Store port for MainWindow to open
            auto& s = AppSettings::instance();
            s.setValue("FlexControlPort", port);
            s.setValue("FlexControlOpen", "True");
            s.save();
        });
        connect(fcCloseBtn, &QPushButton::clicked, this, [updateFcStatus] {
            updateFcStatus(false);
            auto& s = AppSettings::instance();
            s.setValue("FlexControlOpen", "False");
            s.save();
        });

        // Show current state from settings
        if (settings.value("FlexControlOpen", "False").toString() == "True") {
            QString port = settings.value("FlexControlPort").toString();
            if (!port.isEmpty())
                updateFcStatus(true, port);
        }

        // Button action configuration
        static const QStringList actions = {
            "None", "StepUp", "StepDown", "ToggleMox",
            "ToggleTune", "ToggleMute", "ToggleLock",
            "NextSlice", "PrevSlice"
        };
        static const char* defaultActions[3][2] = {
            {"StepUp", "StepDown"},
            {"ToggleMox", "ToggleTune"},
            {"ToggleMute", "ToggleLock"},
        };
        static const char* btnLabels[3] = {"Button 1:", "Button 2:", "Button 3:"};
        static const char* actLabels[2] = {"Tap", "Double"};

        for (int b = 0; b < 3; ++b) {
            grid->addWidget(new QLabel(btnLabels[b]), b + 1, 0);
            auto* row = new QHBoxLayout;
            for (int a = 0; a < 2; ++a) {
                row->addWidget(new QLabel(actLabels[a]));
                auto* combo = new QComboBox;
                combo->addItems(actions);
                combo->setStyleSheet(QString(kEditStyle).replace("QLineEdit", "QComboBox"));
                QString key = QString("FlexControlBtn%1Action%2").arg(b + 1).arg(a);
                QString current = settings.value(key, defaultActions[b][a]).toString();
                int idx = actions.indexOf(current);
                if (idx >= 0) combo->setCurrentIndex(idx);
                connect(combo, &QComboBox::currentTextChanged, this, [key](const QString& text) {
                    auto& s = AppSettings::instance();
                    s.setValue(key, text);
                    s.save();
                });
                row->addWidget(combo);
            }
            row->addStretch();
            grid->addLayout(row, b + 1, 1, 1, 2);
        }

        // Auto-detect checkbox
        auto* autoDetect = new QCheckBox("Auto-detect on startup");
        autoDetect->setStyleSheet("QCheckBox { color: #c8d8e8; }");
        autoDetect->setChecked(settings.value("FlexControlAutoDetect", "True").toString() == "True");
        connect(autoDetect, &QCheckBox::toggled, this, [](bool on) {
            auto& s = AppSettings::instance();
            s.setValue("FlexControlAutoDetect", on ? "True" : "False");
            s.save();
        });
        grid->addWidget(autoDetect, 4, 0, 1, 3);

        auto* invertDir = new QCheckBox("Invert tuning direction");
        invertDir->setStyleSheet("QCheckBox { color: #c8d8e8; }");
        invertDir->setChecked(settings.value("FlexControlInvertDir", "False").toString() == "True");
        connect(invertDir, &QCheckBox::toggled, this, [](bool on) {
            auto& s = AppSettings::instance();
            s.setValue("FlexControlInvertDir", on ? "True" : "False");
            s.save();
        });
        grid->addWidget(invertDir, 5, 0, 1, 3);

        vbox->addWidget(group);
    }

    vbox->addStretch();
    return page;
}
#endif

void RadioSetupDialog::selectTab(const QString& tabName)
{
    if (!m_tabs) return;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (m_tabs->tabText(i) == tabName) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }
}

} // namespace AetherSDR
