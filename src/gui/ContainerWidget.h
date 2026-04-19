#pragma once

#include <QWidget>
#include <QVector>
#include <QString>

class QVBoxLayout;

namespace AetherSDR {

class ContainerManager;
class FloatingContainerWindow;

// ContainerWidget — a node in the container tree.  Each node is either a
// **leaf** (wraps a single content QWidget) or a **branch** (holds an ordered
// list of child ContainerWidgets in a vertical layout).
//
// Nesting is arbitrary: a branch can contain leaves and other branches.
// Float/dock/hide operations work at any level — you can float the entire
// sidebar, a sub-group (e.g. TX DSP: CHAIN+CEQ+CMP), or a single applet.
//
// ContainerManager owns the root and handles persistence; ContainerWidget
// is a pure layout/visibility node that delegates floating-window lifecycle
// to its manager.
class ContainerWidget : public QWidget {
    Q_OBJECT

public:
    // Construct a leaf container wrapping an existing content widget.
    ContainerWidget(const QString& id, const QString& title,
                    QWidget* content, ContainerManager* mgr,
                    QWidget* parent = nullptr);

    // Construct a branch container (no content widget; children added later).
    ContainerWidget(const QString& id, const QString& title,
                    ContainerManager* mgr,
                    QWidget* parent = nullptr);

    const QString& containerId() const { return m_id; }
    const QString& title()       const { return m_title; }
    bool isLeaf()                const { return m_leaf != nullptr; }
    bool isFloating()            const { return m_floating; }

    // The wrapped content widget (nullptr for branches).
    QWidget* contentWidget() const { return m_leaf; }

    // ── Child management (branch nodes only) ────────────────────────────────
    int childCount() const { return m_children.size(); }
    ContainerWidget* childAt(int index) const;
    void addChild(ContainerWidget* child);
    void insertChild(int index, ContainerWidget* child);
    void removeChild(ContainerWidget* child);
    const QVector<ContainerWidget*>& children() const { return m_children; }

    // Find a descendant container by ID (depth-first).
    ContainerWidget* findById(const QString& id);

    // ── Float / Dock / Hide ─────────────────────────────────────────────────
    // Detach this container into a FloatingContainerWindow.
    void floatContainer();

    // Return this container from its floating window back to its parent.
    void dockContainer();

    // Show/hide without changing float state (toggle-button behaviour).
    void setContainerVisible(bool visible);

signals:
    void floated(const QString& id);
    void docked(const QString& id);

private:
    friend class ContainerManager;

    void initLayout();

    QString              m_id;
    QString              m_title;
    QWidget*             m_leaf{nullptr};       // non-null for leaf nodes
    QVector<ContainerWidget*> m_children;       // non-empty for branch nodes
    QVBoxLayout*         m_layout{nullptr};
    ContainerManager*    m_manager{nullptr};
    bool                 m_floating{false};
};

} // namespace AetherSDR
