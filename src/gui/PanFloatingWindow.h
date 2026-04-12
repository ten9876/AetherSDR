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
    PanFloatingWindow(PanadapterApplet* applet, QWidget* parent = nullptr);

    PanadapterApplet* takeApplet();
    PanadapterApplet* applet() const { return m_applet; }
    QString panId() const;

    void saveWindowGeometry();
    void restoreWindowGeometry();
    void setShuttingDown(bool on) { m_shuttingDown = on; }

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
