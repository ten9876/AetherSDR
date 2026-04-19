#include "ContainerManager.h"
#include "ContainerWidget.h"
#include "FloatingContainerWindow.h"
#include "core/AppSettings.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

namespace AetherSDR {

ContainerManager::ContainerManager(QWidget* parentWidget, QObject* parent)
    : QObject(parent)
    , m_parentWidget(parentWidget)
{
}

ContainerManager::~ContainerManager()
{
    // Clean up floating windows — they are not parented to m_root.
    for (FloatingContainerWindow* win : m_floatingWindows) {
        win->hideAndCapture();
        delete win;
    }
    m_floatingWindows.clear();
}

// ── Content factory ─────────────────────────────────────────────────────────

void ContainerManager::registerContent(const QString& id, ContentFactory factory)
{
    m_factories[id] = std::move(factory);
}

QWidget* ContainerManager::createContent(const QString& id) const
{
    auto it = m_factories.find(id);
    if (it == m_factories.end()) { return nullptr; }
    return it.value()();
}

// ── Tree construction ───────────────────────────────────────────────────────

void ContainerManager::setRoot(ContainerWidget* root)
{
    m_root = root;
}

ContainerWidget* ContainerManager::findById(const QString& id) const
{
    if (!m_root) { return nullptr; }
    return m_root->findById(id);
}

// ── Float / Dock ────────────────────────────────────────────────────────────

void ContainerManager::floatContainer(const QString& id)
{
    if (m_floatingWindows.contains(id)) { return; }

    ContainerWidget* container = findById(id);
    if (!container) { return; }

    // Find parent so we can remove/re-add the container on dock.
    ContainerWidget* parentContainer = findParentOf(id, m_root);

    // Reparent the container widget out of the tree into a floating window.
    if (parentContainer) {
        parentContainer->removeChild(container);
    }

    auto* win = new FloatingContainerWindow(id, container->title(),
                                            container, m_parentWidget);
    m_floatingWindows[id] = win;
    container->m_floating = true;

    connect(win, &FloatingContainerWindow::dockRequested,
            this, &ContainerManager::dockContainer);

    // If we have previously saved geometry, restore it.
    if (container->m_floating) {
        win->showAndRestore(win->savedGeometry());
    }

    emit containerFloated(id);
}

void ContainerManager::dockContainer(const QString& id)
{
    if (!m_floatingWindows.contains(id)) { return; }

    ContainerWidget* container = findById(id);
    FloatingContainerWindow* win = m_floatingWindows.value(id);

    if (container) {
        container->m_floating = false;

        // Re-insert into parent.  findParentOf won't find it (it was removed),
        // so we walk the tree to find where it should go.  For now, re-add to
        // root if no better parent is recorded.  Phase 2 will track the
        // original parent index for precise re-insertion.
        // Reparent: the container is currently a child of the floating window's
        // content layout.  Take it back.
        container->setParent(nullptr);

        if (m_root && m_root != container) {
            m_root->addChild(container);
        }
        container->show();
    }

    if (win) {
        win->hideAndCapture();
        win->deleteLater();
    }
    m_floatingWindows.remove(id);

    emit containerDocked(id);
}

void ContainerManager::setFloatingWindowVisible(const QString& id, bool visible)
{
    FloatingContainerWindow* win = m_floatingWindows.value(id);
    if (!win) { return; }
    if (visible) {
        win->showAndRestore(win->savedGeometry());
    } else {
        win->hideAndCapture();
    }
}

bool ContainerManager::isFloating(const QString& id) const
{
    return m_floatingWindows.contains(id);
}

// ── Persistence ─────────────────────────────────────────────────────────────

QJsonObject ContainerManager::saveTree() const
{
    if (!m_root) { return {}; }
    return saveNode(m_root);
}

QJsonObject ContainerManager::saveNode(const ContainerWidget* node) const
{
    QJsonObject obj;
    obj["id"]       = node->containerId();
    obj["title"]    = node->title();
    obj["visible"]  = node->isVisible() || node->isFloating();
    obj["floating"] = node->isFloating();

    // Save floating window geometry if applicable.
    if (node->isFloating()) {
        FloatingContainerWindow* win = m_floatingWindows.value(node->containerId());
        if (win) {
            QRect geo = win->savedGeometry();
            if (geo.isValid()) {
                QJsonObject geoObj;
                geoObj["x"] = geo.x();
                geoObj["y"] = geo.y();
                geoObj["w"] = geo.width();
                geoObj["h"] = geo.height();
                obj["geometry"] = geoObj;
            }
        }
    }

    if (!node->children().isEmpty()) {
        QJsonArray children;
        for (const ContainerWidget* child : node->children()) {
            children.append(saveNode(child));
        }
        obj["children"] = children;
    }

    return obj;
}

void ContainerManager::saveToSettings() const
{
    QJsonObject tree = saveTree();
    QJsonDocument doc(tree);
    AppSettings::instance().setValue("ContainerTree", doc.toJson(QJsonDocument::Compact));
    AppSettings::instance().save();
}

void ContainerManager::restoreTree(const QJsonObject& obj)
{
    if (!m_root || obj.isEmpty()) { return; }
    restoreNode(m_root, obj);
}

void ContainerManager::restoreNode(ContainerWidget* node, const QJsonObject& obj)
{
    if (!node) { return; }

    // The JSON id must match the node we're restoring into.
    if (obj["id"].toString() != node->containerId()) { return; }

    bool visible  = obj["visible"].toBool(true);
    bool floating = obj["floating"].toBool(false);

    if (floating) {
        // Restore geometry into the floating window.
        QRect geo;
        if (obj.contains("geometry")) {
            QJsonObject g = obj["geometry"].toObject();
            geo = QRect(g["x"].toInt(), g["y"].toInt(),
                        g["w"].toInt(), g["h"].toInt());
        }

        // Defer floating to after the event loop is running so the
        // window system is ready.
        QTimer::singleShot(0, this, [this, id = node->containerId(), geo]() {
            floatContainer(id);
            if (geo.isValid()) {
                if (FloatingContainerWindow* win = m_floatingWindows.value(id)) {
                    win->applySavedGeometry(geo);
                }
            }
        });
    } else {
        node->setVisible(visible);
    }

    // Recurse into children by matching IDs.
    if (obj.contains("children")) {
        QJsonArray childArr = obj["children"].toArray();
        for (const QJsonValue& childVal : childArr) {
            QJsonObject childObj = childVal.toObject();
            QString childId = childObj["id"].toString();
            ContainerWidget* childNode = node->findById(childId);
            if (childNode && childNode != node) {
                restoreNode(childNode, childObj);
            }
        }
    }
}

void ContainerManager::restoreFromSettings()
{
    QString json = AppSettings::instance().value("ContainerTree").toString();
    if (json.isEmpty()) { return; }
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (doc.isObject()) {
        restoreTree(doc.object());
    }
}

// ── Private helpers ─────────────────────────────────────────────────────────

ContainerWidget* ContainerManager::findParentOf(const QString& id, ContainerWidget* subtree) const
{
    if (!subtree) { return nullptr; }
    for (ContainerWidget* child : subtree->children()) {
        if (child->containerId() == id) {
            return subtree;
        }
        if (ContainerWidget* found = findParentOf(id, child)) {
            return found;
        }
    }
    return nullptr;
}

} // namespace AetherSDR
