#include "ConnectionPanel.h"
#include "core/AppSettings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QDebug>

namespace AetherSDR {

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

    connect(m_manualProbeBtn, &QPushButton::clicked, this, [this] {
        const QString ip = m_manualIpEdit->text().trimmed();
        if (!ip.isEmpty()) probeRadio(ip);
    });
    connect(m_manualIpEdit, &QLineEdit::returnPressed, this, [this] {
        const QString ip = m_manualIpEdit->text().trimmed();
        if (!ip.isEmpty()) probeRadio(ip);
    });

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

void ConnectionPanel::probeRadio(const QString& ip)
{
    m_manualProbeBtn->setEnabled(false);
    m_manualProbeBtn->setText("Probing...");

    auto* sock = new QTcpSocket(this);
    sock->connectToHost(ip, 4992);

    // 3-second timeout
    QTimer::singleShot(3000, sock, [this, sock] {
        if (sock->state() != QAbstractSocket::ConnectedState) {
            sock->abort();
            sock->deleteLater();
            m_manualProbeBtn->setEnabled(true);
            m_manualProbeBtn->setText("Connect");
        }
    });

    connect(sock, &QTcpSocket::connected, this, [this, sock, ip] {
        // Connected — read V/H lines, then disconnect
        auto* buf = new QByteArray;
        connect(sock, &QTcpSocket::readyRead, this, [this, sock, ip, buf] {
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
                             << "version:" << version;
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
        m_manualProbeBtn->setText("Connect");
    });
}

} // namespace AetherSDR
