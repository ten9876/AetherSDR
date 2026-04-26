#pragma once

#include <QWidget>

class QVBoxLayout;

namespace AetherSDR {

class PanadapterApplet;

// Top-level window hosting a detached panadapter. Created by
// PanadapterStack::floatPanadapter(), destroyed on dock.
class PanFloatingWindow : public QWidget {
    Q_OBJECT

public:
    explicit PanFloatingWindow(QWidget* parent = nullptr);

    void adoptApplet(PanadapterApplet* applet);
    PanadapterApplet* takeApplet();
    PanadapterApplet* applet() const { return m_applet; }
    QString panId() const;

    void saveWindowGeometry();
    void restoreWindowGeometry();
    void setShuttingDown(bool on) { m_shuttingDown = on; }

    // Follow the main-window frameless setting.  Preserves position.
    void setFramelessMode(bool on);

signals:
    void dockRequested(const QString& panId);

protected:
    void closeEvent(QCloseEvent* ev) override;

private:
    PanadapterApplet* m_applet{nullptr};
    QVBoxLayout* m_layout{nullptr};
    bool m_shuttingDown{false};
};

} // namespace AetherSDR
