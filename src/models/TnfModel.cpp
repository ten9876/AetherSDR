#include "TnfModel.h"
#include "core/LogManager.h"
#include <QDebug>

namespace AetherSDR {

TnfModel::TnfModel(QObject* parent)
    : QObject(parent)
{}

const TnfEntry* TnfModel::tnf(int id) const
{
    auto it = m_tnfs.constFind(id);
    return it != m_tnfs.constEnd() ? &(*it) : nullptr;
}

// ── Status parsing ──────────────────────────────────────────────────────────

void TnfModel::applyTnfStatus(int id, const QMap<QString, QString>& kvs)
{
    auto& t = m_tnfs[id];
    t.id = id;

    if (kvs.contains("freq"))
        t.freqMhz = kvs["freq"].toDouble();
    if (kvs.contains("width")) {
        // Width comes from radio in MHz (e.g. 0.000150 = 150 Hz)
        double widthMhz = kvs["width"].toDouble();
        t.widthHz = static_cast<int>(widthMhz * 1.0e6 + 0.5);
        if (t.widthHz < 10) t.widthHz = 100;  // sane default
    }
    if (kvs.contains("depth"))
        t.depthDb = kvs["depth"].toInt();
    if (kvs.contains("permanent"))
        t.permanent = kvs["permanent"] == "1";

    qCDebug(lcProtocol) << "TnfModel: TNF" << id << "freq=" << t.freqMhz
             << "width=" << t.widthHz << "depth=" << t.depthDb;
    emit tnfChanged(id);
}

void TnfModel::removeTnf(int id)
{
    if (m_tnfs.remove(id)) {
        qCDebug(lcProtocol) << "TnfModel: removed TNF" << id;
        emit tnfRemoved(id);
    }
}

void TnfModel::applyGlobalEnabled(bool on)
{
    if (m_globalEnabled == on) return;
    m_globalEnabled = on;
    emit globalEnabledChanged(on);
}

// ── Commands ────────────────────────────────────────────────────────────────

void TnfModel::createTnf(double freqMhz)
{
    emit commandReady(QString("tnf create freq=%1").arg(freqMhz, 0, 'f', 6));
}

void TnfModel::setTnfFreq(int id, double freqMhz)
{
    emit commandReady(QString("tnf set %1 freq=%2").arg(id).arg(freqMhz, 0, 'f', 6));
}

void TnfModel::setTnfWidth(int id, int widthHz)
{
    // Radio expects width in MHz (e.g. 150 Hz → 0.000150)
    emit commandReady(QString("tnf set %1 width=%2").arg(id).arg(widthHz / 1.0e6, 0, 'f', 6));
}

void TnfModel::setTnfDepth(int id, int depthDb)
{
    emit commandReady(QString("tnf set %1 depth=%2").arg(id).arg(depthDb));
}

void TnfModel::setTnfPermanent(int id, bool on)
{
    emit commandReady(QString("tnf set %1 permanent=%2").arg(id).arg(on ? 1 : 0));
    // Radio doesn't send status update — update locally
    auto it = m_tnfs.find(id);
    if (it != m_tnfs.end()) {
        it->permanent = on;
        emit tnfChanged(id);
    }
}

void TnfModel::requestRemoveTnf(int id)
{
    emit commandReady(QString("tnf remove %1").arg(id));
    // Radio does not send a removal status — remove optimistically
    removeTnf(id);
}

void TnfModel::requestGlobalTnfEnabled(bool on)
{
    emit commandReady(QString("radio set tnf_enabled=%1").arg(on ? 1 : 0));
    // Optimistic update — radio echoes tnf_enabled in status, but update
    // immediately so the UI reflects the change without waiting for the echo.
    if (m_globalEnabled != on) {
        m_globalEnabled = on;
        emit globalEnabledChanged(on);
    }
}

void TnfModel::clear()
{
    m_tnfs.clear();
}

} // namespace AetherSDR
