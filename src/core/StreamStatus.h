#pragma once

// Shared helpers for parsing SmartSDR stream-status fields.
// Deduplicated from MainWindow.cpp, TciServer.cpp, and RadioModel.cpp (#2142).

#include <QString>
#include <QMap>

namespace AetherSDR {

// Parse a hex-or-decimal handle/token string (e.g. "0x12345678" or "305419896")
// into a quint32.  Returns 0 on failure.
inline quint32 parseStatusHandle(QString text)
{
    text = text.trimmed();
    bool ok = false;
    quint32 value = text.toUInt(&ok, 0);
    if (ok)
        return value;

    if (text.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive))
        text = text.mid(2);
    value = text.toUInt(&ok, 16);
    return ok ? value : 0;
}

// Return true if a stream-status key-value map either omits client_handle
// (legacy firmware) or matches our own handle.
inline bool streamStatusBelongsToUs(const QMap<QString, QString>& kvs, quint32 ourHandle)
{
    if (!kvs.contains(QStringLiteral("client_handle")))
        return true; // Preserve compatibility with status lines that omit ownership.
    const quint32 owner = parseStatusHandle(kvs.value(QStringLiteral("client_handle")));
    return owner == 0 || owner == ourHandle;
}

} // namespace AetherSDR
