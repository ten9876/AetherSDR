#include "ConnectionPanel.h"
#include "core/AppSettings.h"
#include "core/NetworkPathResolver.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalBlocker>

namespace AetherSDR {

namespace {

constexpr int kSourceModeRole = Qt::UserRole + 10;
constexpr int kSourceInterfaceIdRole = Qt::UserRole + 11;
constexpr int kSourceInterfaceNameRole = Qt::UserRole + 12;
constexpr int kSourceAddressRole = Qt::UserRole + 13;
constexpr int kSourceStaleRole = Qt::UserRole + 14;

QJsonObject loadRoutedProfiles()
{
    const QByteArray json =
        AppSettings::instance().value("RoutedProfilesJson", "{}").toString().toUtf8();
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    return doc.isObject() ? doc.object() : QJsonObject{};
}

void saveRoutedProfiles(const QJsonObject& profiles)
{
    auto& settings = AppSettings::instance();
    settings.setValue("RoutedProfilesJson",
                      QString::fromUtf8(QJsonDocument(profiles).toJson(QJsonDocument::Compact)));
    settings.save();
}

RadioBindSettings bindSettingsFromProfile(const QJsonObject& profile)
{
    const QJsonObject bind = profile.value("bind").toObject();
    RadioBindSettings settings;
    settings.mode = bind.value("mode").toString() == "explicit"
        ? RadioBindMode::Explicit
        : RadioBindMode::Auto;
    settings.interfaceId = bind.value("interface_id").toString();
    settings.interfaceName = bind.value("interface_name").toString();
    settings.bindAddress = QHostAddress(bind.value("last_successful_ipv4").toString());
    return settings;
}

QString staleSelectionText(const RadioBindSettings& settings)
{
    QString iface = settings.interfaceName.trimmed();
    if (iface.isEmpty())
        iface = settings.interfaceId.trimmed();
    if (iface.isEmpty())
        iface = QStringLiteral("Saved source");
    const QString addr = settings.bindAddress.isNull()
        ? QStringLiteral("unknown IPv4")
        : settings.bindAddress.toString();
    return QStringLiteral("%1 (unavailable, last %2)").arg(iface, addr);
}

}

ConnectionPanel::ConnectionPanel(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet("ConnectionPanel { background: #0f0f1a; }");
    const QString editStyle =
        "QLineEdit { border: 1px solid #304050; "
        "border-radius: 3px; padding: 2px 4px; }";

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(8, 8, 8, 8);
    vbox->setSpacing(6);

    // Status label (shows connection state text)
    m_statusLabel = new QLabel("Not connected", this);
    m_statusLabel->setStyleSheet("QLabel { color: #8aa8c0; font-size: 11px; }");
    m_statusLabel->setAlignment(Qt::AlignCenter);
    vbox->addWidget(m_statusLabel);

    // Discovered radios list
    m_radioGroup = new QGroupBox("Discovered Radios", this);
    auto* gbox  = new QVBoxLayout(m_radioGroup);
    m_radioList = new QListWidget(m_radioGroup);
    m_radioList->setSelectionMode(QAbstractItemView::SingleSelection);
    gbox->addWidget(m_radioList);
    vbox->addWidget(m_radioGroup, 1);

    // Low bandwidth checkbox
    m_lowBwCheck = new QCheckBox("Low Bandwidth", this);
    m_lowBwCheck->setChecked(
        AppSettings::instance().value("LowBandwidthConnect", "False").toString() == "True");
    m_lowBwCheck->setToolTip("Reduces FFT/waterfall data from the radio.\n"
                             "Recommended for VPN, LTE, or other metered/limited links.");
    m_lowBwCheck->setStyleSheet("QCheckBox { color: #8aa8c0; font-size: 11px; }");
    vbox->addWidget(m_lowBwCheck);

    // Connect/disconnect button
    m_connectBtn = new QPushButton("Connect", this);
    m_connectBtn->setEnabled(false);
    vbox->addWidget(m_connectBtn);

    // ── SmartLink login section ──────────────────────────────────────────
    m_smartLinkGroup = new QGroupBox("SmartLink", this);
    auto* slBox = new QVBoxLayout(m_smartLinkGroup);
    slBox->setSpacing(4);

    // Login form container (hidden after successful login)
    m_loginForm = new QWidget(m_smartLinkGroup);
    auto* loginLayout = new QVBoxLayout(m_loginForm);
    loginLayout->setContentsMargins(0, 0, 0, 0);
    loginLayout->setSpacing(4);

    auto* emailRow = new QHBoxLayout;
    emailRow->addWidget(new QLabel("Email:", m_loginForm));
    m_emailEdit = new QLineEdit(m_loginForm);
    m_emailEdit->setStyleSheet(editStyle);
    m_emailEdit->setPlaceholderText("flexradio account email");
    QString storedEmail = AppSettings::instance().value("SmartLinkEmail").toString();
    if (!storedEmail.isEmpty())
        m_emailEdit->setText(QString::fromUtf8(QByteArray::fromBase64(storedEmail.toUtf8())));
    emailRow->addWidget(m_emailEdit, 1);
    loginLayout->addLayout(emailRow);

    auto* passRow = new QHBoxLayout;
    passRow->addWidget(new QLabel("Pass:", m_loginForm));
    m_passwordEdit = new QLineEdit(m_loginForm);
    m_passwordEdit->setStyleSheet(editStyle);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("password");
    passRow->addWidget(m_passwordEdit, 1);
    loginLayout->addLayout(passRow);

    m_loginBtn = new QPushButton("Log In", m_loginForm);
    loginLayout->addWidget(m_loginBtn);

    slBox->addWidget(m_loginForm);

    // User info (shown after login)
    m_slUserLabel = new QLabel("", m_smartLinkGroup);
    m_slUserLabel->setStyleSheet("QLabel { color: #00b4d8; font-size: 10px; }");
    m_slUserLabel->setVisible(false);
    slBox->addWidget(m_slUserLabel);

    vbox->addWidget(m_smartLinkGroup);

    // ── Manual (routed) connection ───────────────────────────────────────
    m_manualGroup = new QGroupBox("Manual Connection", this);
    auto* manBox = new QVBoxLayout(m_manualGroup);
    manBox->setContentsMargins(4, 8, 4, 4);
    manBox->setSpacing(4);

    auto* manRow = new QHBoxLayout;
    manRow->setContentsMargins(0, 0, 0, 0);
    m_manualIpEdit = new QLineEdit(m_manualGroup);
    m_manualIpEdit->setStyleSheet(editStyle);
    m_manualIpEdit->setPlaceholderText("IP address");
    m_manualIpEdit->setFixedWidth(150);
    m_manualProbeBtn = new QPushButton("Go", m_manualGroup);
    m_manualProbeBtn->setFixedWidth(70);
    manRow->addWidget(m_manualIpEdit);
    manRow->addWidget(m_manualProbeBtn);
    manBox->addLayout(manRow);

    auto* sourceRow = new QHBoxLayout;
    sourceRow->setContentsMargins(0, 0, 0, 0);
    sourceRow->addWidget(new QLabel("Source:", m_manualGroup));
    m_manualSourceCombo = new QComboBox(m_manualGroup);
    sourceRow->addWidget(m_manualSourceCombo, 1);
    manBox->addLayout(sourceRow);

    m_manualSourceWarningLabel = new QLabel(m_manualGroup);
    m_manualSourceWarningLabel->setVisible(false);
    m_manualSourceWarningLabel->setWordWrap(true);
    m_manualSourceWarningLabel->setStyleSheet("QLabel { color: #ffbb66; font-size: 10px; }");
    manBox->addWidget(m_manualSourceWarningLabel);
    refreshManualSourceOptions();

    connect(m_manualProbeBtn, &QPushButton::clicked, this, [this] {
        const QString ip = m_manualIpEdit->text().trimmed();
        if (!ip.isEmpty()) probeRadio(ip);
    });
    connect(m_manualIpEdit, &QLineEdit::returnPressed, this, [this] {
        const QString ip = m_manualIpEdit->text().trimmed();
        if (!ip.isEmpty()) probeRadio(ip);
    });
    connect(m_manualIpEdit, &QLineEdit::textChanged,
            this, &ConnectionPanel::onManualIpChanged);

    vbox->addWidget(m_manualGroup);

    // Login action (button click or Enter in password field)
    auto doLogin = [this] {
        const QString email = m_emailEdit->text().trimmed();
        const QString pass  = m_passwordEdit->text();
        if (email.isEmpty() || pass.isEmpty()) return;
        m_loginBtn->setEnabled(false);
        m_loginBtn->setText("Logging in...");
        emit smartLinkLoginRequested(email, pass);
    };
    connect(m_loginBtn, &QPushButton::clicked, this, doLogin);
    connect(m_passwordEdit, &QLineEdit::returnPressed, this, doLogin);

    // All widgets now exist — safe to call setConnected for initial state
    setConnected(false);

    connect(m_radioList, &QListWidget::itemSelectionChanged,
            this, &ConnectionPanel::onListSelectionChanged);
    connect(m_connectBtn, &QPushButton::clicked,
            this, &ConnectionPanel::onConnectClicked);
    connect(m_radioList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) {
                if (!m_connected)
                    onConnectClicked();
            });
}

void ConnectionPanel::setConnected(bool connected)
{
    m_connected = connected;
    m_connectBtn->setText(connected ? "Disconnect" : "Connect");
    m_connectBtn->setEnabled(connected || m_radioList->currentItem() != nullptr);
}

void ConnectionPanel::setStatusText(const QString& text)
{
    m_statusLabel->setText(text);
}

// ─── Radio list management ────────────────────────────────────────────────────

void ConnectionPanel::onRadioDiscovered(const RadioInfo& radio)
{
    m_radios.append(radio);
    m_radioList->addItem(radio.displayName());
    // Auto-select first discovered radio so new users don't get stuck
    if (m_radioList->count() == 1)
        m_radioList->setCurrentRow(0);
}

void ConnectionPanel::onRadioUpdated(const RadioInfo& radio)
{
    for (int i = 0; i < m_radios.size(); ++i) {
        if (m_radios[i].serial == radio.serial) {
            m_radios[i] = radio;
            m_radioList->item(i)->setText(radio.displayName());
            return;
        }
    }
}

void ConnectionPanel::onRadioLost(const QString& serial)
{
    for (int i = 0; i < m_radios.size(); ++i) {
        if (m_radios[i].serial == serial) {
            delete m_radioList->takeItem(i);
            m_radios.removeAt(i);
            return;
        }
    }
}

void ConnectionPanel::onListSelectionChanged()
{
    // When connected, Disconnect is always available. When disconnected,
    // Connect requires a radio to be selected. (#561)
    m_connectBtn->setEnabled(m_connected || m_radioList->currentItem() != nullptr);
}

void ConnectionPanel::onConnectClicked()
{
    if (m_connected) {
        emit disconnectRequested();
        return;
    }

    const int row = m_radioList->currentRow();
    if (row < 0) return;

    // Check if this is a WAN entry (stored in item data)
    auto* item = m_radioList->item(row);
    if (!item) return;

    int wanIdx = item->data(Qt::UserRole + 1).toInt();
    if (wanIdx > 0 && wanIdx <= m_wanRadios.size()) {
        emit wanConnectRequested(m_wanRadios[wanIdx - 1]);  // 1-based index
        return;
    }

    // Save low bandwidth preference before connecting
    auto& s = AppSettings::instance();
    s.setValue("LowBandwidthConnect", m_lowBwCheck->isChecked() ? "True" : "False");
    s.save();

    // LAN radio
    if (row < m_radios.size())
        emit connectRequested(m_radios[row]);
}

void ConnectionPanel::setSmartLinkClient(SmartLinkClient* client)
{
    m_smartLink = client;
    if (!client) return;

    connect(client, &SmartLinkClient::authenticated, this, [this] {
        // Clear password from memory immediately
        m_passwordEdit->clear();

        // Hide login form, show logout button
        m_loginForm->setVisible(false);

        // Add logout button below user label
        m_loginBtn = new QPushButton("Log Out", m_smartLinkGroup);
        qobject_cast<QVBoxLayout*>(m_smartLinkGroup->layout())->insertWidget(1, m_loginBtn);
        connect(m_loginBtn, &QPushButton::clicked, this, [this] {
            m_smartLink->logout();
            // Remove logout button, show login form
            m_loginBtn->deleteLater();
            m_loginForm->setVisible(true);
            m_loginBtn = m_loginForm->findChild<QPushButton*>();
            m_slUserLabel->setVisible(false);
            // Remove WAN entries from unified list
            for (int i = m_radioList->count() - 1; i >= 0; --i) {
                if (m_radioList->item(i)->data(Qt::UserRole + 1).toInt() > 0)
                    delete m_radioList->takeItem(i);
            }
            m_wanRadios.clear();
        });

        // User info may not be available yet — show placeholder
        m_slUserLabel->setText("Connected to SmartLink");
        m_slUserLabel->setStyleSheet("QLabel { color: #00b4d8; font-size: 10px; }");
        m_slUserLabel->setVisible(true);
    });

    // Update user label when server sends user_settings (after authenticated)
    connect(client, &SmartLinkClient::serverConnected, this, [this] {
        // User settings arrive shortly after server connect
        QTimer::singleShot(500, this, [this] {
            if (!m_smartLink->firstName().isEmpty()) {
                m_slUserLabel->setText(QString("%1 %2 (%3)")
                    .arg(m_smartLink->firstName(), m_smartLink->lastName(),
                         m_smartLink->callsign()));
            }
        });
    });

    connect(client, &SmartLinkClient::authFailed, this, [this](const QString& err) {
        m_passwordEdit->clear();
        m_loginBtn->setText("Log In");
        m_loginBtn->setEnabled(true);
        m_slUserLabel->setText("Login failed: " + err);
        m_slUserLabel->setStyleSheet("QLabel { color: #ff4444; font-size: 10px; }");
        m_slUserLabel->setVisible(true);
    });

    // Attempt auto-login from stored keychain credentials
    client->tryAutoLogin();

    connect(client, &SmartLinkClient::radioListReceived, this,
            [this](const QList<WanRadioInfo>& radios) {
        // Remove old WAN entries from unified list
        for (int i = m_radioList->count() - 1; i >= 0; --i) {
            if (m_radioList->item(i)->data(Qt::UserRole + 1).toInt() > 0)
                delete m_radioList->takeItem(i);
        }

        m_wanRadios = radios;
        for (int i = 0; i < radios.size(); ++i) {
            const auto& r = radios[i];
            // Skip if already in LAN list (same serial)
            bool isLan = false;
            for (const auto& lan : m_radios) {
                if (lan.serial == r.serial) { isLan = true; break; }
            }
            if (isLan) continue;

            QString display = QString("%1  %2  %3\nAvailable (SmartLink)")
                .arg(r.model, r.nickname, r.callsign);
            auto* item = new QListWidgetItem(display);
            item->setData(Qt::UserRole + 1, i + 1);  // 1-based WAN index
            m_radioList->addItem(item);
        }
    });
}

bool ConnectionPanel::event(QEvent* e)
{
    if (e->type() == QEvent::WindowDeactivate) {
        hide();
        return true;
    }
    return QWidget::event(e);
}

void ConnectionPanel::paintEvent(QPaintEvent*)
{
    // Solid fill — system window frame handles border/decorations (#574)
    QPainter p(this);
    p.fillRect(rect(), QColor(15, 15, 26));
}

void ConnectionPanel::refreshManualSourceOptions(const RadioBindSettings* selected)
{
    const QSignalBlocker blocker(m_manualSourceCombo);
    m_manualSourceCombo->clear();

    m_manualSourceCombo->addItem("Auto");
    m_manualSourceCombo->setItemData(0, static_cast<int>(RadioBindMode::Auto), kSourceModeRole);
    m_manualSourceCombo->setItemData(0, false, kSourceStaleRole);

    int selectedIndex = 0;
    const auto candidates = NetworkPathResolver::enumerateIpv4Candidates();
    for (const auto& candidate : candidates) {
        const int idx = m_manualSourceCombo->count();
        m_manualSourceCombo->addItem(candidate.label());
        m_manualSourceCombo->setItemData(idx, static_cast<int>(RadioBindMode::Explicit), kSourceModeRole);
        m_manualSourceCombo->setItemData(idx, candidate.interfaceId, kSourceInterfaceIdRole);
        m_manualSourceCombo->setItemData(idx, candidate.interfaceName, kSourceInterfaceNameRole);
        m_manualSourceCombo->setItemData(idx, candidate.address.toString(), kSourceAddressRole);
        m_manualSourceCombo->setItemData(idx, false, kSourceStaleRole);

        if (selected &&
            selected->mode == RadioBindMode::Explicit &&
            ((!selected->interfaceId.isEmpty() && selected->interfaceId == candidate.interfaceId) ||
             (!selected->bindAddress.isNull() && selected->bindAddress == candidate.address))) {
            selectedIndex = idx;
        }
    }

    if (selected &&
        selected->mode == RadioBindMode::Explicit &&
        selectedIndex == 0) {
        selectedIndex = m_manualSourceCombo->count();
        m_manualSourceCombo->addItem(staleSelectionText(*selected));
        m_manualSourceCombo->setItemData(selectedIndex, static_cast<int>(RadioBindMode::Explicit), kSourceModeRole);
        m_manualSourceCombo->setItemData(selectedIndex, selected->interfaceId, kSourceInterfaceIdRole);
        m_manualSourceCombo->setItemData(selectedIndex, selected->interfaceName, kSourceInterfaceNameRole);
        m_manualSourceCombo->setItemData(selectedIndex, selected->bindAddress.toString(), kSourceAddressRole);
        m_manualSourceCombo->setItemData(selectedIndex, true, kSourceStaleRole);
    }

    m_manualSourceCombo->setCurrentIndex(selectedIndex);
}

void ConnectionPanel::applySavedSourceSelection(const QString& ip)
{
    const QString trimmedIp = ip.trimmed();
    m_manualProfileIp = trimmedIp;
    m_manualSourceWarningLabel->clear();
    m_manualSourceWarningLabel->setVisible(false);

    if (trimmedIp.isEmpty()) {
        refreshManualSourceOptions();
        return;
    }

    const QJsonObject profiles = loadRoutedProfiles();
    const QJsonObject profile = profiles.value(trimmedIp).toObject();
    if (profile.isEmpty()) {
        refreshManualSourceOptions();
        return;
    }

    RadioBindSettings settings = bindSettingsFromProfile(profile);
    if (settings.mode == RadioBindMode::Explicit) {
        const auto resolved = NetworkPathResolver::resolveExplicitSelection(
            settings.interfaceId, settings.interfaceName, settings.bindAddress);
        if (resolved.isValid()) {
            settings.interfaceId = resolved.interfaceId;
            settings.interfaceName = resolved.interfaceName;
            settings.bindAddress = resolved.address;
        } else {
            m_manualSourceWarningLabel->setText(
                QStringLiteral("Saved source path for %1 is unavailable. Re-select a live IPv4 source before connecting.")
                    .arg(trimmedIp));
            m_manualSourceWarningLabel->setVisible(true);
        }
    }

    refreshManualSourceOptions(&settings);
}

RadioBindSettings ConnectionPanel::currentManualBindSettings(bool* staleSelection) const
{
    RadioBindSettings settings;
    const int index = m_manualSourceCombo->currentIndex();
    settings.mode = static_cast<RadioBindMode>(
        m_manualSourceCombo->itemData(index, kSourceModeRole).toInt());
    settings.interfaceId = m_manualSourceCombo->itemData(index, kSourceInterfaceIdRole).toString();
    settings.interfaceName = m_manualSourceCombo->itemData(index, kSourceInterfaceNameRole).toString();
    settings.bindAddress = QHostAddress(m_manualSourceCombo->itemData(index, kSourceAddressRole).toString());
    if (staleSelection)
        *staleSelection = m_manualSourceCombo->itemData(index, kSourceStaleRole).toBool();
    return settings;
}

void ConnectionPanel::saveManualProfile(const QString& targetIp,
                                        const RadioBindSettings& settings,
                                        const QHostAddress& lastSuccessfulLocalIp)
{
    if (targetIp.trimmed().isEmpty())
        return;

    QJsonObject profiles = loadRoutedProfiles();
    QJsonObject profile;
    profile["schema_version"] = 1;

    QJsonObject identity;
    identity["target_address"] = targetIp;
    profile["identity"] = identity;

    QJsonObject bind;
    bind["mode"] = settings.mode == RadioBindMode::Explicit ? "explicit" : "auto";
    bind["interface_id"] = settings.interfaceId;
    bind["interface_name"] = settings.interfaceName;
    bind["last_successful_ipv4"] = lastSuccessfulLocalIp.toString();
    profile["bind"] = bind;

    profiles[targetIp] = profile;
    saveRoutedProfiles(profiles);
}

void ConnectionPanel::onManualIpChanged(const QString& ip)
{
    const QString trimmed = ip.trimmed();
    if (trimmed == m_manualProfileIp)
        return;
    applySavedSourceSelection(trimmed);
}

void ConnectionPanel::probeRadio(const QString& ip)
{
    if (m_manualIpEdit->text().trimmed() != ip) {
        m_manualIpEdit->setText(ip);
        applySavedSourceSelection(ip);
    } else if (m_manualProfileIp != ip) {
        applySavedSourceSelection(ip);
    }

    bool staleSelection = false;
    const RadioBindSettings bindSettings = currentManualBindSettings(&staleSelection);
    if (bindSettings.mode == RadioBindMode::Explicit && staleSelection) {
        m_manualSourceWarningLabel->setText(
            QStringLiteral("Selected source path is unavailable. Choose a live IPv4 source before probing."));
        m_manualSourceWarningLabel->setVisible(true);
        return;
    }

    m_manualProbeBtn->setEnabled(false);
    m_manualProbeBtn->setText("Probing...");
    m_manualSourceWarningLabel->setVisible(false);

    auto* sock = new QTcpSocket(this);
    if (bindSettings.mode == RadioBindMode::Explicit &&
        !sock->bind(bindSettings.bindAddress, 0)) {
        m_manualSourceWarningLabel->setText(
            QStringLiteral("Failed to bind source %1: %2")
                .arg(bindSettings.bindAddress.toString(), sock->errorString()));
        m_manualSourceWarningLabel->setVisible(true);
        sock->deleteLater();
        m_manualProbeBtn->setEnabled(true);
        m_manualProbeBtn->setText("Go");
        return;
    }
    sock->connectToHost(ip, 4992);

    // 3-second timeout
    QTimer::singleShot(3000, sock, [this, sock] {
        if (sock->state() != QAbstractSocket::ConnectedState) {
            sock->abort();
            sock->deleteLater();
            m_manualProbeBtn->setEnabled(true);
            m_manualProbeBtn->setText("Go");
        }
    });

    connect(sock, &QTcpSocket::connected, this, [this, sock, ip, bindSettings] {
        // Connected — read V/H lines, then disconnect
        auto* buf = new QByteArray;
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, ip, buf, bindSettings] {
            buf->append(sock->readAll());

            // We need V<version>\n and H<handle>\n
            QString version;
            while (buf->contains('\n')) {
                int idx = buf->indexOf('\n');
                QString line = QString::fromUtf8(buf->left(idx)).trimmed();
                buf->remove(0, idx + 1);

                if (line.startsWith('V'))
                    version = line.mid(1);
                else if (line.startsWith('H')) {
                    // Got both V and H — we have enough info
                    const QHostAddress localSource = sock->localAddress();
                    sock->disconnectFromHost();
                    sock->deleteLater();
                    delete buf;

                    // Build a RadioInfo for this routed radio
                    RadioInfo info;
                    info.address = QHostAddress(ip);
                    info.port = 4992;
                    info.version = version;
                    info.status = "Available";
                    info.model = "FLEX";
                    info.name = "FLEX";
                    info.serial = ip;  // use IP as unique ID for routed radios
                    info.isRouted = true;
                    info.bindSettings = bindSettings;
                    info.sessionBindAddress = localSource;

                    saveManualProfile(ip, bindSettings, info.sessionBindAddress);

                    // Check if already in list
                    for (int i = 0; i < m_radios.size(); ++i) {
                        if (m_radios[i].address == info.address) {
                            m_radios[i] = info;
                            m_radioList->item(i)->setText(info.displayName());
                            m_manualProbeBtn->setEnabled(true);
                            m_manualProbeBtn->setText("Go");
                            emit routedRadioFound(info);
                            return;
                        }
                    }

                    m_radios.append(info);
                    m_radioList->addItem(info.displayName());

                    m_manualProbeBtn->setEnabled(true);
                    m_manualProbeBtn->setText("Go");
                    emit routedRadioFound(info);

                    qDebug() << "ConnectionPanel: routed radio found at" << ip
                             << "version:" << version
                             << "localSource:" << info.sessionBindAddress.toString()
                             << "mode:" << info.bindSettings.modeString();
                    return;
                }
            }
        });
    });

    connect(sock, &QTcpSocket::errorOccurred, this,
            [this, sock](QAbstractSocket::SocketError) {
        qWarning() << "ConnectionPanel: probe failed:" << sock->errorString();
        sock->deleteLater();
        m_manualProbeBtn->setEnabled(true);
        m_manualProbeBtn->setText("Go");
    });
}

} // namespace AetherSDR
