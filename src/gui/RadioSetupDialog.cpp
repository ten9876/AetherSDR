#include "RadioSetupDialog.h"
#include "ComboStyle.h"
#include "models/RadioModel.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#ifdef HAVE_SERIALPORT
#include "core/SerialPortController.h"
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
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QTimer>
#include <QMediaDevices>
#include <QAudioDevice>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QProgressBar>

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
    setMinimumSize(660, 520);
    setStyleSheet("QDialog { background: #0f0f1a; }");

    auto* layout = new QVBoxLayout(this);

    auto* tabs = new QTabWidget;
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
            m_model->connection()->sendCommand("radio name " + m_nicknameEdit->text());
        });
        connect(m_callsignEdit, &QLineEdit::editingFinished, this, [this] {
            m_model->connection()->sendCommand("radio callsign " + m_callsignEdit->text());
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
            m_model->connection()->sendCommand(
                QString("radio set enforce_private_ip_connections=%1").arg(on ? 1 : 0));
        });
        grid->addWidget(enforceBtn, 0, 1);

        for (auto* lbl : group->findChildren<QLabel*>())
            if (lbl->styleSheet().isEmpty()) lbl->setStyleSheet(kLabelStyle);

        vbox->addWidget(group);
    }

    // DHCP / Static IP group
    {
        auto* group = new QGroupBox("IP Configuration");
        group->setStyleSheet(kGroupStyle);
        auto* gvbox = new QVBoxLayout(group);
        gvbox->setSpacing(6);

        // DHCP / Static buttons
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

        // Static IP fields
        auto* fieldsGrid = new QGridLayout;
        fieldsGrid->setSpacing(4);

        fieldsGrid->addWidget(new QLabel("IP Address:"), 0, 0);
        // If static is configured, show those values; otherwise show current DHCP values
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

        // Apply button
        auto* applyBtn = new QPushButton("Apply");
        applyBtn->setEnabled(false);
        applyBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
            "padding: 4px 16px; }"
            "QPushButton:hover { background: #203040; }");
        gvbox->addWidget(applyBtn, 0, Qt::AlignLeft);

        // DHCP/Static toggle logic
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

        // Apply sends the command
        connect(applyBtn, &QPushButton::clicked, this,
                [this, dhcpBtn, staticIp, staticMask, staticGw, applyBtn] {
            if (dhcpBtn->isChecked()) {
                m_model->connection()->sendCommand("radio static_net_params reset");
                qDebug() << "RadioSetupDialog: network set to DHCP";
            } else {
                const QString cmd = QString("radio static_net_params ip=%1 gateway=%2 netmask=%3")
                    .arg(staticIp->text(), staticGw->text(), staticMask->text());
                m_model->connection()->sendCommand(cmd);
                qDebug() << "RadioSetupDialog: static IP applied" << cmd;
            }
            applyBtn->setEnabled(false);
        });

        vbox->addWidget(group);
    }

    vbox->addStretch(1);
    return page;
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
    auto* tx = m_model->transmitModel();
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

        addTimingField(0, 0, "ACC TX:",  tx->accTxDelay());
        addTimingField(0, 1, "TX Delay:", tx->txDelay());
        addTimingField(1, 0, "RCA TX1:", tx->tx1Delay());
        addTimingField(1, 1, "Timeout(min):", tx->interlockTimeout());
        addTimingField(2, 0, "RCA TX2:", tx->tx2Delay());

        // TX Profile dropdown (below Timeout, right column)
        auto* profCmb = new QComboBox;
        profCmb->addItems(tx->profileList());
        profCmb->setCurrentText(tx->activeProfile());
        AetherSDR::applyComboStyle(profCmb);
        grid->addWidget(profCmb, 2, 2, 1, 2);
        connect(profCmb, &QComboBox::currentTextChanged, this, [this](const QString& name) {
            m_model->transmitModel()->loadProfile(name);
        });

        addTimingField(3, 0, "RCA TX3:", tx->tx3Delay());

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
                .arg(m_model->transmitModel()->activeProfile()));
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
                    m_model->connection()->sendCommand(
                        QString("transmit bandset %1 rfpower=%2").arg(bandId).arg(rfEdit->text()));
                });
                headerGrid->addWidget(rfEdit, row, col++);

                // Tune Power
                auto* tuneEdit = new QLineEdit(QString::number(b.tunePower));
                tuneEdit->setStyleSheet(kEditStyle);
                tuneEdit->setFixedWidth(50);
                tuneEdit->setAlignment(Qt::AlignCenter);
                connect(tuneEdit, &QLineEdit::editingFinished, dlg, [this, tuneEdit, bandId] {
                    m_model->connection()->sendCommand(
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
                        m_model->connection()->sendCommand(
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
        rcaCmb->setCurrentIndex(tx->rcaTxReqPolarity());
        AetherSDR::applyComboStyle(rcaCmb);
        grid->addWidget(rcaCmb, 0, 1);

        auto* accLbl = new QLabel("Accessory:");
        accLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(accLbl, 0, 2);
        auto* accCmb = new QComboBox;
        accCmb->addItems({"Active Low", "Active High"});
        accCmb->setCurrentIndex(tx->accTxReqPolarity());
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
        auto* mpEdit = new QLineEdit(QString::number(tx->maxPowerLevel()));
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
            m_model->connection()->sendCommand(
                QString("transmit set max_power_level=%1").arg(val));
        });

        auto* tmLbl = new QLabel("Tune Mode:");
        tmLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(tmLbl, 1, 0);
        auto* tmCmb = new QComboBox;
        tmCmb->addItems({"Single Tone", "Two Tone"});
        tmCmb->setCurrentText(tx->tuneMode() == "single_tone" ? "Single Tone" : "Two Tone");
        AetherSDR::applyComboStyle(tmCmb);
        connect(tmCmb, &QComboBox::currentTextChanged, this, [this](const QString& text) {
            QString mode = (text == "Single Tone") ? "single_tone" : "two_tone";
            m_model->connection()->sendCommand("transmit set tune_mode=" + mode);
        });
        grid->addWidget(tmCmb, 1, 1);

        auto* swLbl = new QLabel("Show TX in Waterfall:");
        swLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(swLbl, 2, 0);
        auto* swBtn = new QPushButton(tx->showTxInWaterfall() ? "Enabled" : "Disabled");
        swBtn->setCheckable(true);
        swBtn->setChecked(tx->showTxInWaterfall());
        swBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 11px; font-weight: bold; "
            "padding: 3px 10px; }"
            "QPushButton:checked { background: #1a5030; color: #00e060; "
            "border: 1px solid #20a040; }");
        connect(swBtn, &QPushButton::toggled, this, [this, swBtn](bool on) {
            swBtn->setText(on ? "Enabled" : "Disabled");
            m_model->connection()->sendCommand(
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
    auto* tx = m_model->transmitModel();
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
        auto* biasBtn = mkTogBtn("BIAS", tx->micBias());
        connect(biasBtn, &QPushButton::toggled, this, [this](bool on) {
            m_model->connection()->sendCommand(QString("mic bias %1").arg(on ? 1 : 0));
        });
        row1->addWidget(biasBtn);
        auto* boostBtn = mkTogBtn("+20dB", tx->micBoost());
        connect(boostBtn, &QPushButton::toggled, this, [this](bool on) {
            m_model->connection()->sendCommand(QString("mic boost %1").arg(on ? 1 : 0));
        });
        row1->addWidget(boostBtn);
        row1->addStretch(1);
        gvb->addLayout(row1);

        // Meter in RX
        auto* row2 = new QHBoxLayout;
        row2->setSpacing(4);
        auto* metBtn = mkTogBtn("Enabled", tx->metInRx());
        connect(metBtn, &QPushButton::toggled, this, [this, metBtn](bool on) {
            metBtn->setText(on ? "Enabled" : "Disabled");
            m_model->connection()->sendCommand(QString("transmit set met_in_rx=%1").arg(on ? 1 : 0));
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
        auto* iamBtn = mkTogBtn("Enabled", tx->cwIambic());
        connect(iamBtn, &QPushButton::toggled, this, [this, iamBtn](bool on) {
            iamBtn->setText(on ? "Enabled" : "Disabled");
            m_model->connection()->sendCommand(QString("cw iambic %1").arg(on ? 1 : 0));
        });
        grid->addWidget(iamBtn, 0, 1);
        auto* modeA = mkTogBtn("A", tx->cwIambicMode() == 0);
        auto* modeB = mkTogBtn("B", tx->cwIambicMode() == 1);
        connect(modeA, &QPushButton::clicked, this, [this, modeA, modeB] {
            modeA->setChecked(true); modeB->setChecked(false);
            m_model->connection()->sendCommand("cw mode 0");
        });
        connect(modeB, &QPushButton::clicked, this, [this, modeA, modeB] {
            modeA->setChecked(false); modeB->setChecked(true);
            m_model->connection()->sendCommand("cw mode 1");
        });
        grid->addWidget(modeA, 0, 2);
        grid->addWidget(modeB, 0, 3);

        // Swap: Dot/Dash button
        auto* swapLbl = new QLabel("Swap:");
        swapLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(swapLbl, 0, 4);
        auto* swapBtn = mkTogBtn("Dot/Dash", tx->cwSwapPaddles());
        connect(swapBtn, &QPushButton::toggled, this, [this](bool on) {
            m_model->connection()->sendCommand(QString("cw swap %1").arg(on ? 1 : 0));
        });
        grid->addWidget(swapBtn, 0, 5);

        // Sideband: CWU | CWL
        auto* sbLbl = new QLabel("Sideband:");
        sbLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(sbLbl, 1, 0);
        auto* cwuBtn = mkTogBtn("CWU", !tx->cwlEnabled());
        auto* cwlBtn = mkTogBtn("CWL", tx->cwlEnabled());
        connect(cwuBtn, &QPushButton::clicked, this, [this, cwuBtn, cwlBtn] {
            cwuBtn->setChecked(true); cwlBtn->setChecked(false);
            m_model->connection()->sendCommand("cw cwl_enabled 0");
        });
        connect(cwlBtn, &QPushButton::clicked, this, [this, cwuBtn, cwlBtn] {
            cwuBtn->setChecked(false); cwlBtn->setChecked(true);
            m_model->connection()->sendCommand("cw cwl_enabled 1");
        });
        grid->addWidget(cwuBtn, 1, 1);
        grid->addWidget(cwlBtn, 1, 2);

        // CWX: Sync
        auto* cwxLbl = new QLabel("CWX:");
        cwxLbl->setStyleSheet(kLabelStyle);
        grid->addWidget(cwxLbl, 1, 4);
        auto* syncBtn = mkTogBtn("Sync", tx->syncCwx());
        connect(syncBtn, &QPushButton::toggled, this, [this](bool on) {
            m_model->connection()->sendCommand(QString("cw synccwx %1").arg(on ? 1 : 0));
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
        auto* markEdit = new QLineEdit("2125");
        markEdit->setStyleSheet(kEditStyle);
        markEdit->setFixedWidth(60);
        connect(markEdit, &QLineEdit::editingFinished, this, [this, markEdit] {
            m_model->connection()->sendCommand(
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

            auto* row = new QHBoxLayout;
            row->setSpacing(4);
            auto* ppbLbl = new QLabel("Freq Error (ppb):");
            ppbLbl->setStyleSheet(kLabelStyle);
            row->addWidget(ppbLbl);
            auto* ppbEdit = new QLineEdit(QString::number(m_model->freqErrorPpb()));
            ppbEdit->setStyleSheet(kEditStyle);
            ppbEdit->setFixedWidth(80);
            connect(ppbEdit, &QLineEdit::editingFinished, this, [this, ppbEdit] {
                m_model->connection()->sendCommand(
                    "radio set freq_error_ppb=" + ppbEdit->text());
            });
            row->addWidget(ppbEdit);
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
            m_model->connection()->sendCommand(
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
                m_model->connection()->sendCommand(
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

    // Front Speaker (mute only, no gain control)
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

    // Update from radio status
    connect(m_model, &RadioModel::audioOutputChanged, this,
            [this, lineoutSlider, lineoutValue, lineoutMute,
             hpSlider, hpValue, hpMute, spkMute] {
        QSignalBlocker b1(lineoutSlider), b2(lineoutMute),
                       b3(hpSlider), b4(hpMute), b5(spkMute);
        lineoutSlider->setValue(m_model->lineoutGain());
        lineoutValue->setText(QString::number(m_model->lineoutGain()));
        lineoutMute->setChecked(m_model->lineoutMute());
        hpSlider->setValue(m_model->headphoneGain());
        hpValue->setText(QString::number(m_model->headphoneGain()));
        hpMute->setChecked(m_model->headphoneMute());
        spkMute->setChecked(m_model->frontSpeakerMute());
    });

    vbox->addWidget(outGroup);

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
                m_model->connection()->sendCommand(
                    QString("radio filter_sharpness %1 level=%2").arg(mode).arg(v));
            });
            connect(autoBtn, &QPushButton::toggled, this, [this, slider, mode](bool on) {
                slider->setEnabled(!on);
                m_model->connection()->sendCommand(
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
            m_model->connection()->sendCommand(
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
            m_model->connection()->sendCommand(
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
            m_model->connection()->sendCommand(QString("xvtr remove %1").arg(idx));
            int tabIdx = xvtrTabs->indexOf(pg);
            if (tabIdx >= 0) xvtrTabs->removeTab(tabIdx);
        });
        grid->addWidget(removeBtn, 4, 3);

        // Wire editable fields
        connect(nameEdit, &QLineEdit::editingFinished, this, [this, nameEdit, idx] {
            m_model->connection()->sendCommand(
                QString("xvtr set %1 name=%2").arg(idx).arg(nameEdit->text()));
        });
        auto updateLo = [rfEdit, ifEdit, loEdit] {
            double rf = rfEdit->text().toDouble();
            double ifF = ifEdit->text().toDouble();
            loEdit->setText(QString::number(rf - ifF, 'f', 3));
        };
        connect(rfEdit, &QLineEdit::editingFinished, this, [this, rfEdit, idx, updateLo] {
            m_model->connection()->sendCommand(
                QString("xvtr set %1 rf_freq=%2").arg(idx).arg(rfEdit->text()));
            updateLo();
        });
        connect(ifEdit, &QLineEdit::editingFinished, this, [this, ifEdit, idx, updateLo] {
            m_model->connection()->sendCommand(
                QString("xvtr set %1 if_freq=%2").arg(idx).arg(ifEdit->text()));
            updateLo();
        });
        connect(errEdit, &QLineEdit::editingFinished, this, [this, errEdit, idx] {
            m_model->connection()->sendCommand(
                QString("xvtr set %1 lo_error=%2").arg(idx).arg(errEdit->text()));
        });
        connect(rxGainEdit, &QLineEdit::editingFinished, this, [this, rxGainEdit, idx] {
            m_model->connection()->sendCommand(
                QString("xvtr set %1 rx_gain=%2").arg(idx).arg(rxGainEdit->text()));
        });
        connect(maxPwrEdit, &QLineEdit::editingFinished, this, [this, maxPwrEdit, idx] {
            m_model->connection()->sendCommand(
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
        m_model->connection()->sendCommand("xvtr create",
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

    vbox->addStretch();
    return page;
}
#endif

} // namespace AetherSDR
