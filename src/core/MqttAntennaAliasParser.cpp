#include "MqttAntennaAliasParser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace AetherSDR {
namespace MqttAntennaAliasParser {

const QString kNamesTopic  = QStringLiteral("aethersdr/antenna/names");
const QString kNamePrefix  = QStringLiteral("aethersdr/antenna/name/");

QVector<AliasUpdate> parse(const QString& topic, const QByteArray& payload)
{
    QVector<AliasUpdate> updates;

    if (topic == kNamesTopic) {
        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(payload, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject())
            return updates;

        const QJsonObject obj = doc.object();
        updates.reserve(obj.size());
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            const QString token = it.key().trimmed();
            if (token.isEmpty())
                continue;
            // JSON null and "" both clear the local name.
            const QString alias = it.value().isNull()
                ? QString()
                : it.value().toString().trimmed();
            updates.push_back({token, alias});
        }
        return updates;
    }

    if (topic.startsWith(kNamePrefix)) {
        const QString token = topic.mid(kNamePrefix.size()).trimmed();
        if (token.isEmpty())
            return updates;
        // Empty per-port payload clears that local name.
        const QString alias = QString::fromUtf8(payload).trimmed();
        updates.push_back({token, alias});
    }

    return updates;
}

} // namespace MqttAntennaAliasParser
} // namespace AetherSDR
