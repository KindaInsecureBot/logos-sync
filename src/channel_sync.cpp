#include "channel_sync.h"
#include "sync_types.h"
#include "module_proxy.h"

namespace LogosSync {

ChannelSync::ChannelSync(QObject* parent)
    : QObject(parent)
{}

void ChannelSync::setZoneClient(ModuleProxy* zone)
{
    m_zone = zone;
}

// static
QString ChannelSync::deriveChannelId(const QString& appPrefix, const QString& uniqueId)
{
    return LogosSync::deriveChannelId(appPrefix, uniqueId);
}

QString ChannelSync::inscribe(const QString& channelId, const QByteArray& data)
{
    if (!m_zone || channelId.isEmpty() || data.isEmpty()) return {};

    // Encode data as hex for wire transport.
    const QString dataHex = QString::fromLatin1(data.toHex());
    const QVariant result = m_zone->invokeRemoteMethod(
        "zone_module", "inscribe", channelId, dataHex);

    const QString inscriptionId = result.toString().trimmed();
    if (inscriptionId.isEmpty()) {
        emit error("ChannelSync: zone module returned empty inscription ID for channel: " + channelId);
        return {};
    }

    emit inscribed(channelId, inscriptionId);
    return inscriptionId;
}

QString ChannelSync::queryChannel(const QString& channelId)
{
    if (!m_zone || channelId.isEmpty()) return QStringLiteral("[]");

    const QVariant result = m_zone->invokeRemoteMethod(
        "zone_module", "queryChannel", channelId);
    const QString json = result.toString();
    return json.isEmpty() ? QStringLiteral("[]") : json;
}

void ChannelSync::follow(const QString& channelId)
{
    if (channelId.isEmpty() || m_followedChannels.contains(channelId)) return;
    m_followedChannels.insert(channelId);

    if (!m_zone) return;
    m_zone->invokeRemoteMethod("zone_module", "follow", channelId);
}

void ChannelSync::unfollow(const QString& channelId)
{
    if (channelId.isEmpty()) return;
    m_followedChannels.remove(channelId);

    if (!m_zone) return;
    m_zone->invokeRemoteMethod("zone_module", "unfollow", channelId);
}

void ChannelSync::onZoneInscription(const QString& channelId,
                                    const QString& inscriptionId,
                                    const QByteArray& data)
{
    if (!m_followedChannels.contains(channelId)) return;
    if (channelId.isEmpty() || inscriptionId.isEmpty()) return;

    emit inscriptionReceived(channelId, inscriptionId, data);
}

} // namespace LogosSync
