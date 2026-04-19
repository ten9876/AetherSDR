#pragma once

#include <QObject>
#include <QMap>
#include <QJsonObject>
#include <functional>

class QWidget;

namespace AetherSDR {

class ContainerWidget;
class FloatingContainerWindow;

// ContentFactory — maps string IDs to widget constructors so the container
// tree can be reconstructed from a persisted JSON description.
//
// Register a factory before loading the tree:
//   mgr->registerContent("TX", [](){ return new TxApplet; });
using ContentFactory = std::function<QWidget*()>;

// ContainerManager — owns the root of a ContainerWidget tree and manages
// all floating windows.  Handles JSON serialisation/deserialisation of the
// full tree (container hierarchy, visibility, floating state, geometry)
// under a single AppSettings key ("ContainerTree").
//
// Phase 1: framework + API.  Phase 2 will wire AppletPanel to construct
// its applets via ContainerManager.
class ContainerManager : public QObject {
    Q_OBJECT

public:
    explicit ContainerManager(QWidget* parentWidget, QObject* parent = nullptr);
    ~ContainerManager() override;

    // ── Content factory ─────────────────────────────────────────────────────
    void registerContent(const QString& id, ContentFactory factory);

    // Create a content widget from the factory (returns nullptr if unregistered).
    QWidget* createContent(const QString& id) const;

    // ── Tree construction ───────────────────────────────────────────────────
    // Set the root container (takes ownership).
    void setRoot(ContainerWidget* root);
    ContainerWidget* root() const { return m_root; }

    // Find any container in the tree by ID.
    ContainerWidget* findById(const QString& id) const;

    // ── Float / Dock (called by ContainerWidget) ────────────────────────────
    void floatContainer(const QString& id);
    void dockContainer(const QString& id);
    void setFloatingWindowVisible(const QString& id, bool visible);
    bool isFloating(const QString& id) const;

    // ── Persistence ─────────────────────────────────────────────────────────
    // Serialise the entire container tree (hierarchy, visibility, float
    // state, floating-window geometry) to a QJsonObject.
    QJsonObject saveTree() const;

    // Write the tree JSON to AppSettings under "ContainerTree".
    void saveToSettings() const;

    // Restore tree state (visibility, float, geometry) from a QJsonObject.
    // The tree structure must already be built; this applies saved state
    // on top of it. Unknown IDs in the JSON are silently ignored.
    void restoreTree(const QJsonObject& obj);

    // Load from AppSettings "ContainerTree" key and apply.
    void restoreFromSettings();

signals:
    void containerFloated(const QString& id);
    void containerDocked(const QString& id);

private:
    QJsonObject saveNode(const ContainerWidget* node) const;
    void restoreNode(ContainerWidget* node, const QJsonObject& obj);

    // Find the parent ContainerWidget of a given child ID.
    ContainerWidget* findParentOf(const QString& id, ContainerWidget* subtree) const;

    ContainerWidget* m_root{nullptr};
    QWidget*         m_parentWidget{nullptr};  // parent for floating windows
    QMap<QString, ContentFactory> m_factories;
    QMap<QString, FloatingContainerWindow*> m_floatingWindows;
};

} // namespace AetherSDR
