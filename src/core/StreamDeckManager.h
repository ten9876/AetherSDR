#pragma once
#ifdef HAVE_HIDAPI

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QByteArray>
#include <memory>
#include <vector>
#include <hidapi/hidapi.h>
#include "StreamDeckDevice.h"

namespace AetherSDR {

// Represents one connected StreamDeck device
struct StreamDeckHandle {
    hid_device*                          device{nullptr};
    const StreamDeckDeviceInfo*          info{nullptr};
    std::unique_ptr<StreamDeckProtocol>  protocol;
    QString                              serial;
    QString                              firmware;
    QString                              path;
    int                                  brightness{70};
    std::vector<bool>                    prevKeyStates;
};

// Manages all connected StreamDeck devices on the ExtControllers thread.
class StreamDeckManager : public QObject {
    Q_OBJECT

public:
    explicit StreamDeckManager(QObject* parent = nullptr);
    ~StreamDeckManager() override;

    QStringList connectedSerials() const;
    const StreamDeckDeviceInfo* deviceInfo(const QString& serial) const;
    QString deviceSerial(int index) const;
    int deviceCount() const { return m_devices.size(); }

public slots:
    void start();
    void stop();
    void setKeyImage(const QString& serial, int key, const QByteArray& imageData);
    void setTouchscreenImage(const QString& serial, const QByteArray& imageData,
                              int x, int y, int w, int h);
    void setBrightness(const QString& serial, int percent);

signals:
    void deviceConnected(const QString& serial, const QString& modelName,
                         int keyCount, int keyCols, int keyRows, int dialCount);
    void deviceDisconnected(const QString& serial);
    void keyPressed(const QString& serial, int key, bool pressed);
    void dialTurned(const QString& serial, int dial, int delta);
    void dialPressed(const QString& serial, int dial, bool pressed);
    void touchEvent(const QString& serial, int type, int x, int y, int xOut, int yOut);

private slots:
    void poll();
    void hotplugCheck();

private:
    void openDevice(const StreamDeckDeviceInfo* info, const char* path);
    void closeDevice(const QString& serial);
    void closeAll();

    QHash<QString, StreamDeckHandle*> m_devices;  // owned, deleted in closeDevice/closeAll
    QTimer* m_pollTimer{nullptr};
    QTimer* m_hotplugTimer{nullptr};
    uint8_t m_buf[1024]{};

    static constexpr int POLL_INTERVAL_MS = 10;
    static constexpr int HOTPLUG_INTERVAL_MS = 3000;
};

} // namespace AetherSDR
#endif
