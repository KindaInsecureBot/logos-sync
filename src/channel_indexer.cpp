#include "channel_indexer.h"
#include "logos_api_client.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

// ── IndexerCursor ─────────────────────────────────────────────────────────────

QJsonObject IndexerCursor::toJson() const
{
    return QJsonObject{
        {QStringLiteral("slot"),      QJsonValue(static_cast<qint64>(slot))},
        {QStringLiteral("lastMsgId"), lastMsgId}
    };
}

IndexerCursor IndexerCursor::fromJson(const QJsonObject& obj)
{
    IndexerCursor c;
    c.slot       = static_cast<quint64>(obj.value(QStringLiteral("slot")).toInteger(0));
    c.lastMsgId  = obj.value(QStringLiteral("lastMsgId")).toString();
    return c;
}

// ── Inscription ───────────────────────────────────────────────────────────────

QJsonObject Inscription::toJson() const
{
    return QJsonObject{
        {QStringLiteral("channelId"),     channelId},
        {QStringLiteral("inscriptionId"), inscriptionId},
        {QStringLiteral("data"),          QString::fromLatin1(data.toHex())},
        {QStringLiteral("slot"),          QJsonValue(static_cast<qint64>(slot))}
    };
}

Inscription Inscription::fromJson(const QJsonObject& obj)
{
    Inscription i;
    i.channelId     = obj.value(QStringLiteral("channelId")).toString();
    i.inscriptionId = obj.value(QStringLiteral("inscriptionId")).toString();
    i.data          = QByteArray::fromHex(
        obj.value(QStringLiteral("data")).toString().toLatin1());
    i.slot          = static_cast<quint64>(
        obj.value(QStringLiteral("slot")).toInteger(0));
    return i;
}

// ── IndexerPage ───────────────────────────────────────────────────────────────

QJsonObject IndexerPage::toJson() const
{
    QJsonArray arr;
    for (const Inscription& insc : inscriptions)
        arr.append(insc.toJson());
    return QJsonObject{
        {QStringLiteral("inscriptions"), arr},
        {QStringLiteral("nextCursor"),   nextCursor.toJson()},
        {QStringLiteral("hasMore"),      hasMore}
    };
}

// ── ChannelIndexer ────────────────────────────────────────────────────────────

ChannelIndexer::ChannelIndexer(QObject* parent)
    : QObject(parent)
{}

void ChannelIndexer::setBlockchainClient(LogosAPIClient* blockchain)
{
    m_blockchain = blockchain;
}

void ChannelIndexer::setKvClient(LogosAPIClient* kv)
{
    m_kv = kv;
}

// ── Discovery ─────────────────────────────────────────────────────────────────

QString ChannelIndexer::discoverChannels(const QString& appPrefix)
{
    if (!m_blockchain || appPrefix.isEmpty())
        return QStringLiteral("[]");

    if (!m_discoveryCache.contains(appPrefix))
        refreshDiscovery(appPrefix);

    QJsonArray result;
    for (const QString& ch : m_discoveryCache.value(appPrefix)) {
        QJsonObject obj;
        obj[QStringLiteral("channelId")]      = ch;
        obj[QStringLiteral("inscriptionCount")] = 0;
        if (m_latestCache.contains(ch))
            obj[QStringLiteral("latestCid")] =
                QString::fromLatin1(m_latestCache.value(ch).data.toHex());
        result.append(obj);
    }
    return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void ChannelIndexer::refreshDiscovery(const QString& appPrefix)
{
    if (!m_blockchain || appPrefix.isEmpty()) return;

    const QStringList channels = queryChannelsByPrefix(appPrefix);
    m_discoveryCache[appPrefix] = channels;

    if (m_kv) {
        QJsonArray arr;
        for (const QString& ch : channels) arr.append(ch);
        m_kv->invokeRemoteMethod("kv_module", "set",
            QStringLiteral("pipe:indexer:discovery:") + appPrefix,
            QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    }
}

QStringList ChannelIndexer::queryChannelsByPrefix(const QString& appPrefix)
{
    if (!m_blockchain) return {};

    const QString prefixStr = QStringLiteral("λ") + appPrefix + QStringLiteral(":");
    const QVariant result = m_blockchain->invokeRemoteMethod(
        "blockchain_module", "queryChannelsByPrefix", prefixStr);

    const QJsonDocument doc = QJsonDocument::fromJson(result.toString().toUtf8());
    if (!doc.isArray()) return {};

    QStringList channels;
    for (const QJsonValue& v : doc.array()) {
        if (v.isString()) {
            channels.append(v.toString());
        } else if (v.isObject()) {
            const QString cid = v.toObject().value(QStringLiteral("channelId")).toString();
            if (!cid.isEmpty()) channels.append(cid);
        }
    }
    return channels;
}

// ── Reading ───────────────────────────────────────────────────────────────────

QByteArray ChannelIndexer::getLatestInscription(const QString& channelId)
{
    if (channelId.isEmpty()) return {};

    if (m_latestCache.contains(channelId))
        return m_latestCache.value(channelId).data;

    if (!m_blockchain) return {};

    const QVariant result = m_blockchain->invokeRemoteMethod(
        "blockchain_module", "getLatestInscription", channelId);

    const QString hex = result.toString().trimmed();
    if (hex.isEmpty()) return {};

    const QByteArray data = QByteArray::fromHex(hex.toLatin1());

    Inscription insc;
    insc.channelId = channelId;
    insc.data      = data;
    m_latestCache[channelId] = insc;

    return data;
}

IndexerPage ChannelIndexer::getHistory(const QString& channelId,
                                        const IndexerCursor& cursor,
                                        int limit)
{
    IndexerPage page;
    if (!m_blockchain || channelId.isEmpty()) return page;

    const QVariant result = m_blockchain->invokeRemoteMethod(
        "blockchain_module", "getChannelInscriptions",
        channelId,
        QVariant(static_cast<quint64>(cursor.slot)),
        QVariant(limit));

    const QJsonDocument doc = QJsonDocument::fromJson(result.toString().toUtf8());
    if (!doc.isArray()) return page;

    const QJsonArray arr = doc.array();
    for (const QJsonValue& v : arr) {
        if (!v.isObject()) continue;
        Inscription insc = Inscription::fromJson(v.toObject());
        if (insc.channelId.isEmpty())
            insc.channelId = channelId;
        page.inscriptions.append(insc);
    }

    page.hasMore = arr.size() >= limit;

    if (!page.inscriptions.isEmpty()) {
        const Inscription& last = page.inscriptions.last();
        page.nextCursor.slot       = last.slot;
        page.nextCursor.lastMsgId  = last.inscriptionId;
    }

    return page;
}

int ChannelIndexer::getInscriptionCount(const QString& channelId)
{
    if (channelId.isEmpty() || !m_blockchain) return 0;

    const QVariant result = m_blockchain->invokeRemoteMethod(
        "blockchain_module", "getInscriptionCount", channelId);
    return result.toInt();
}

// ── Live Following ────────────────────────────────────────────────────────────

void ChannelIndexer::follow(const QString& channelId)
{
    if (channelId.isEmpty()) return;
    m_followedChannels.insert(channelId);
}

void ChannelIndexer::followPrefix(const QString& appPrefix)
{
    if (appPrefix.isEmpty() || m_followedPrefixes.contains(appPrefix)) return;
    m_followedPrefixes.insert(appPrefix);

    // Auto-follow all currently discovered channels for this prefix
    for (const QString& channelId : m_discoveryCache.value(appPrefix))
        m_followedChannels.insert(channelId);
}

void ChannelIndexer::unfollow(const QString& channelId)
{
    if (channelId.isEmpty()) return;
    m_followedChannels.remove(channelId);
}

void ChannelIndexer::unfollowPrefix(const QString& appPrefix)
{
    if (appPrefix.isEmpty()) return;
    m_followedPrefixes.remove(appPrefix);

    // Remove individually followed channels that came from this prefix
    for (const QString& channelId : m_discoveryCache.value(appPrefix))
        m_followedChannels.remove(channelId);
}

bool ChannelIndexer::matchesFollowedPrefix(const QString& channelId) const
{
    for (const QString& prefix : m_followedPrefixes) {
        if (m_discoveryCache.value(prefix).contains(channelId))
            return true;
    }
    return false;
}

void ChannelIndexer::onNewInscription(const QString& channelId,
                                       const QString& inscriptionId,
                                       const QByteArray& data,
                                       quint64 slot)
{
    if (channelId.isEmpty() || inscriptionId.isEmpty()) return;

    bool shouldForward = m_followedChannels.contains(channelId)
                         || matchesFollowedPrefix(channelId);

    if (!shouldForward) {
        // Check if this is a newly-inscribed channel for a followed prefix
        for (const QString& prefix : m_followedPrefixes) {
            const QStringList fresh = queryChannelsByPrefix(prefix);
            if (fresh.contains(channelId)) {
                if (!m_discoveryCache[prefix].contains(channelId))
                    m_discoveryCache[prefix].append(channelId);
                emit channelDiscovered(prefix, channelId);
                shouldForward = true;
                break;
            }
        }
    }

    if (!shouldForward) return;

    Inscription insc{channelId, inscriptionId, data, slot};
    m_latestCache[channelId] = insc;

    emit inscriptionDiscovered(channelId, inscriptionId, data);
}

void ChannelIndexer::onBlockFinalized(quint64 /*slot*/)
{
    // Refresh discovery for all followed prefixes on each finalized block
    for (const QString& prefix : m_followedPrefixes)
        refreshDiscovery(prefix);
}

// ── Cache Management ──────────────────────────────────────────────────────────

void ChannelIndexer::saveCacheState()
{
    if (!m_kv) return;

    // Save manifest of known prefixes so loadCacheState() knows what to load
    QJsonArray prefixArr;
    for (const QString& prefix : m_discoveryCache.keys())
        prefixArr.append(prefix);
    m_kv->invokeRemoteMethod("kv_module", "set",
        QStringLiteral("pipe:indexer:manifest"),
        QString::fromUtf8(QJsonDocument(prefixArr).toJson(QJsonDocument::Compact)));

    // Save per-prefix channel lists
    for (auto it = m_discoveryCache.cbegin(); it != m_discoveryCache.cend(); ++it) {
        QJsonArray arr;
        for (const QString& ch : it.value()) arr.append(ch);
        m_kv->invokeRemoteMethod("kv_module", "set",
            QStringLiteral("pipe:indexer:discovery:") + it.key(),
            QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    }

    // Save latest inscriptions
    for (auto it = m_latestCache.cbegin(); it != m_latestCache.cend(); ++it) {
        m_kv->invokeRemoteMethod("kv_module", "set",
            QStringLiteral("pipe:indexer:latest:") + it.key(),
            QString::fromUtf8(
                QJsonDocument(it.value().toJson()).toJson(QJsonDocument::Compact)));
    }
}

void ChannelIndexer::loadCacheState()
{
    if (!m_kv) return;

    // Read manifest to learn which prefixes were previously cached
    const QVariant manifestRaw = m_kv->invokeRemoteMethod(
        "kv_module", "get", QStringLiteral("pipe:indexer:manifest"));
    const QJsonDocument manifestDoc =
        QJsonDocument::fromJson(manifestRaw.toString().toUtf8());
    if (!manifestDoc.isArray()) return;

    for (const QJsonValue& pv : manifestDoc.array()) {
        const QString prefix = pv.toString();
        if (prefix.isEmpty()) continue;

        // Load channel list
        const QVariant chRaw = m_kv->invokeRemoteMethod(
            "kv_module", "get",
            QStringLiteral("pipe:indexer:discovery:") + prefix);
        const QJsonDocument chDoc =
            QJsonDocument::fromJson(chRaw.toString().toUtf8());
        if (!chDoc.isArray()) continue;

        QStringList channels;
        for (const QJsonValue& cv : chDoc.array())
            channels.append(cv.toString());
        m_discoveryCache[prefix] = channels;

        // Load latest inscription for each channel
        for (const QString& channelId : channels) {
            const QVariant latestRaw = m_kv->invokeRemoteMethod(
                "kv_module", "get",
                QStringLiteral("pipe:indexer:latest:") + channelId);
            const QJsonDocument latestDoc =
                QJsonDocument::fromJson(latestRaw.toString().toUtf8());
            if (latestDoc.isObject())
                m_latestCache[channelId] = Inscription::fromJson(latestDoc.object());
        }
    }
}
