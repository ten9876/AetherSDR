#include "ContainerWidget.h"
#include "ContainerManager.h"

#include <QVBoxLayout>

namespace AetherSDR {

// ── Leaf constructor ────────────────────────────────────────────────────────

ContainerWidget::ContainerWidget(const QString& id, const QString& title,
                                 QWidget* content, ContainerManager* mgr,
                                 QWidget* parent)
    : QWidget(parent)
    , m_id(id)
    , m_title(title)
    , m_leaf(content)
    , m_manager(mgr)
{
    initLayout();
    if (m_leaf) {
        m_layout->addWidget(m_leaf);
        m_leaf->show();
    }
}

// ── Branch constructor ──────────────────────────────────────────────────────

ContainerWidget::ContainerWidget(const QString& id, const QString& title,
                                 ContainerManager* mgr,
                                 QWidget* parent)
    : QWidget(parent)
    , m_id(id)
    , m_title(title)
    , m_manager(mgr)
{
    initLayout();
}

void ContainerWidget::initLayout()
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
}

// ── Child management ────────────────────────────────────────────────────────

ContainerWidget* ContainerWidget::childAt(int index) const
{
    if (index < 0 || index >= m_children.size()) { return nullptr; }
    return m_children[index];
}

void ContainerWidget::addChild(ContainerWidget* child)
{
    if (!child || m_leaf) { return; }  // can't add children to a leaf
    m_children.append(child);
    child->setParent(this);
    m_layout->addWidget(child);
}

void ContainerWidget::insertChild(int index, ContainerWidget* child)
{
    if (!child || m_leaf) { return; }
    index = qBound(0, index, m_children.size());
    m_children.insert(index, child);
    child->setParent(this);
    m_layout->insertWidget(index, child);
}

void ContainerWidget::removeChild(ContainerWidget* child)
{
    if (!child) { return; }
    int idx = m_children.indexOf(child);
    if (idx < 0) { return; }
    m_children.remove(idx);
    m_layout->removeWidget(child);
    child->setParent(nullptr);
}

ContainerWidget* ContainerWidget::findById(const QString& id)
{
    if (m_id == id) { return this; }
    for (ContainerWidget* child : m_children) {
        if (ContainerWidget* found = child->findById(id)) {
            return found;
        }
    }
    return nullptr;
}

// ── Float / Dock / Hide ─────────────────────────────────────────────────────

void ContainerWidget::floatContainer()
{
    if (m_floating || !m_manager) { return; }

    // If any children are currently floating, dock them first so the
    // parent floats as a complete unit (edge case #1 from issue plan).
    for (ContainerWidget* child : m_children) {
        if (child->isFloating()) {
            child->dockContainer();
        }
    }

    m_manager->floatContainer(m_id);
}

void ContainerWidget::dockContainer()
{
    if (!m_floating || !m_manager) { return; }
    m_manager->dockContainer(m_id);
}

void ContainerWidget::setContainerVisible(bool visible)
{
    if (m_floating && m_manager) {
        // When floating, raise/hide the floating window rather than the
        // docked widget — mirrors AppletPanel's toggle-button behaviour.
        m_manager->setFloatingWindowVisible(m_id, visible);
        return;
    }
    setVisible(visible);
}

} // namespace AetherSDR
