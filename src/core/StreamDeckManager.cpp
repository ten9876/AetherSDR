#ifdef HAVE_HIDAPI
#include "StreamDeckManager.h"
#include <QDebug>

namespace AetherSDR {

StreamDeckManager::StreamDeckManager(QObject* parent)
    : QObject(parent)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(POLL_INTERVAL_MS);
    connect(m_pollTimer, &QTimer::timeout, this, &StreamDeckManager::poll);

    m_hotplugTimer = new QTimer(this);
    m_hotplugTimer->setInterval(HOTPLUG_INTERVAL_MS);
    connect(m_hotplugTimer, &QTimer::timeout, this, &StreamDeckManager::hotplugCheck);
}

StreamDeckManager::~StreamDeckManager()
{
    stop();
}

void StreamDeckManager::start()
{
    hotplugCheck();  // scan immediately
    m_hotplugTimer->start();
    if (!m_devices.isEmpty())
        m_pollTimer->start();
}

void StreamDeckManager::stop()
{
    m_pollTimer->stop();
    m_hotplugTimer->stop();
    closeAll();
}

QStringList StreamDeckManager::connectedSerials() const
{
    return m_devices.keys();
}

const StreamDeckDeviceInfo* StreamDeckManager::deviceInfo(const QString& serial) const
{
    auto it = m_devices.constFind(serial);
    return (it != m_devices.constEnd()) ? it.value()->info : nullptr;
}

QString StreamDeckManager::deviceSerial(int index) const
{
    auto keys = m_devices.keys();
    return (index >= 0 && index < keys.size()) ? keys[index] : QString();
}

void StreamDeckManager::setKeyImage(const QString& serial, int key, const QByteArray& imageData)
{
    auto it = m_devices.find(serial);
    if (it == m_devices.end()) return;
    auto& h = *it.value();
    if (key < 0 || key >= h.info->keyCount) return;
    h.protocol->writeKeyImage(h.device, key, imageData, *h.info);
}

void StreamDeckManager::setTouchscreenImage(const QString& serial, const QByteArray& imageData,
                                             int x, int y, int w, int h)
{
    auto it = m_devices.find(serial);
    if (it == m_devices.end()) return;
    auto& handle = *it.value();
    handle.protocol->writeTouchscreenImage(handle.device, imageData, x, y, w, h, *handle.info);
}

void StreamDeckManager::setBrightness(const QString& serial, int percent)
{
    auto it = m_devices.find(serial);
    if (it == m_devices.end()) return;
    it.value()->protocol->setBrightness(it.value()->device, percent);
    it.value()->brightness = percent;
}

void StreamDeckManager::poll()
{
    QStringList deadDevices;

    for (auto it = m_devices.begin(); it != m_devices.end(); ++it) {
        auto& h = *it.value();
        if (!h.device) continue;

        int res = hid_read(h.device, m_buf, h.info->inputReportLen);
        if (res < 0) {
            // Device disconnected
            deadDevices.append(it.key());
            continue;
        }
        if (res == 0) continue;  // no data

        auto events = h.protocol->parseInput(m_buf, res, *h.info);

        // Key events — only emit changes
        for (const auto& ke : events.keys) {
            if (ke.key < 0 || ke.key >= static_cast<int>(h.prevKeyStates.size()))
                continue;
            if (ke.pressed != h.prevKeyStates[ke.key]) {
                h.prevKeyStates[ke.key] = ke.pressed;
                emit keyPressed(it.key(), ke.key, ke.pressed);
            }
        }

        // Dial events
        for (const auto& de : events.dials)
            emit dialTurned(it.key(), de.dial, de.delta);
        for (const auto& dp : events.dialPushes)
            emit dialPressed(it.key(), dp.dial, dp.pressed);

        // Touch events
        for (const auto& te : events.touches)
            emit touchEvent(it.key(), static_cast<int>(te.type),
                           te.x, te.y, te.xOut, te.yOut);
    }

    for (const auto& serial : deadDevices)
        closeDevice(serial);
}

void StreamDeckManager::hotplugCheck()
{
    const auto* table = streamDeckDeviceTable();
    int count = streamDeckDeviceCount();

    for (int i = 0; i < count; ++i) {
        auto* enumList = hid_enumerate(STREAMDECK_VID, table[i].pid);
        for (auto* cur = enumList; cur; cur = cur->next) {
            // Check if already open by path
            bool alreadyOpen = false;
            QString path = QString::fromUtf8(cur->path);
            for (const auto& h : m_devices) {
                if (h->path == path) { alreadyOpen = true; break; }
            }
            if (!alreadyOpen)
                openDevice(&table[i], cur->path);
        }
        hid_free_enumeration(enumList);
    }
}

void StreamDeckManager::openDevice(const StreamDeckDeviceInfo* info, const char* path)
{
    hid_device* dev = hid_open_path(path);
    if (!dev) return;

    hid_set_nonblocking(dev, 1);

    auto* handle = new StreamDeckHandle();
    handle->device = dev;
    handle->info = info;
    handle->protocol = createProtocol(info->family);
    handle->path = QString::fromUtf8(path);
    handle->prevKeyStates.resize(info->keyCount, false);

    // Read serial and firmware
    handle->serial = handle->protocol->serialNumber(dev);
    handle->firmware = handle->protocol->firmwareVersion(dev);

    if (handle->serial.isEmpty()) {
        // Fallback: use path as identifier
        handle->serial = handle->path;
    }

    // Set default brightness
    handle->protocol->setBrightness(dev, handle->brightness);

    QString serial = handle->serial;
    QString model = info->name;

    qDebug() << "StreamDeckManager: opened" << model
             << "serial:" << serial << "fw:" << handle->firmware;

    m_devices[serial] = handle;

    if (!m_pollTimer->isActive())
        m_pollTimer->start();

    emit deviceConnected(serial, model, info->keyCount, info->keyCols,
                         info->keyRows, info->dialCount);
}

void StreamDeckManager::closeDevice(const QString& serial)
{
    auto it = m_devices.find(serial);
    if (it == m_devices.end()) return;

    auto* h = it.value();
    if (h->device) {
        hid_close(h->device);
        h->device = nullptr;
    }
    delete h;

    qDebug() << "StreamDeckManager: closed" << serial;
    m_devices.erase(it);
    emit deviceDisconnected(serial);

    if (m_devices.isEmpty())
        m_pollTimer->stop();
}

void StreamDeckManager::closeAll()
{
    auto keys = m_devices.keys();
    for (const auto& serial : keys)
        closeDevice(serial);
}

} // namespace AetherSDR
#endif
