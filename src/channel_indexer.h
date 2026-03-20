#pragma once
#include <QObject>
#include <QSet>
#include <QString>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>

class LogosAPIClient;

// Pagination cursor for channel history queries.
// Serializable to JSON for persistence across restarts.
struct IndexerCursor {
    quint64 slot = 0;
    QString lastMsgId;  // empty = start of slot

    QJsonObject toJson() const;
    static IndexerCursor fromJson(const QJsonObject& obj);
    bool isNull() const { return slot == 0 && lastMsgId.isEmpty(); }
};

// A single inscription read from the chain.
struct Inscription {
    QString channelId;
    QString inscriptionId;
    QByteArray data;
    quint64 slot;

    QJsonObject toJson() const;
    static Inscription fromJson(const QJsonObject& obj);
};

// Result of a paginated query.
struct IndexerPage {
    QList<Inscription> inscriptions;
    IndexerCursor nextCursor;  // pass to next call to continue
    bool hasMore = false;

    QJsonObject toJson() const;
};

// ChannelIndexer — discovers and reads L1 channel inscriptions.
//
// Provides the "read" side of the Zone SDK channel model:
//   ChannelSync    → write to a specific channel
//   ChannelIndexer → discover channels, read history, follow live
//
// Channels are discovered by app prefix (e.g., "BLOG" finds all λBLOG:* channels).
// Uses kv_module for local caching of discovered channels and cursors.
class ChannelIndexer : public QObject {
    Q_OBJECT
public:
    explicit ChannelIndexer(QObject* parent = nullptr);

    void setBlockchainClient(LogosAPIClient* blockchain);
    void setKvClient(LogosAPIClient* kv);
    bool isAvailable() const { return m_blockchain != nullptr; }

    // === Discovery ===

    // Discover all channels matching an app prefix.
    // Returns JSON array: [{"channelId":"...", "latestCid":"...", "inscriptionCount":N}, ...]
    // Results are cached in kv_module; call refreshDiscovery() to update.
    QString discoverChannels(const QString& appPrefix);

    // Force refresh discovery cache for a prefix.
    void refreshDiscovery(const QString& appPrefix);

    // === Reading ===

    // Get the most recent inscription for a channel.
    // Returns the inscription data (typically a CID), or empty if none.
    QByteArray getLatestInscription(const QString& channelId);

    // Get paginated history for a channel.
    // Pass empty/null cursor to start from beginning.
    // Returns up to `limit` inscriptions and a cursor for the next page.
    IndexerPage getHistory(const QString& channelId,
                           const IndexerCursor& cursor = IndexerCursor(),
                           int limit = 50);

    // Get total inscription count for a channel (from cache or chain).
    int getInscriptionCount(const QString& channelId);

    // === Live Following ===

    // Follow a specific channel for new inscriptions.
    void follow(const QString& channelId);

    // Follow all channels matching an app prefix.
    // New channels discovered after calling this will also be followed.
    void followPrefix(const QString& appPrefix);

    // Stop following a channel.
    void unfollow(const QString& channelId);

    // Stop following a prefix.
    void unfollowPrefix(const QString& appPrefix);

    // Called by SyncModule when blockchain_module fires events.
    void onNewInscription(const QString& channelId,
                          const QString& inscriptionId,
                          const QByteArray& data,
                          quint64 slot);

    // Called by SyncModule when a new block is finalized.
    void onBlockFinalized(quint64 slot);

    // === Cache Management ===

    // Save current cursors and discovery cache to kv_module.
    void saveCacheState();

    // Load cached state from kv_module on startup.
    void loadCacheState();

signals:
    // Emitted when a new inscription is detected on a followed channel.
    void inscriptionDiscovered(const QString& channelId,
                               const QString& inscriptionId,
                               const QByteArray& data);

    // Emitted when a new channel matching a followed prefix is discovered.
    void channelDiscovered(const QString& appPrefix,
                           const QString& channelId);

    void error(const QString& message);

private:
    LogosAPIClient* m_blockchain = nullptr;
    LogosAPIClient* m_kv = nullptr;

    QSet<QString> m_followedChannels;
    QSet<QString> m_followedPrefixes;

    // Discovery cache: prefix → list of channel IDs
    QHash<QString, QStringList> m_discoveryCache;

    // Per-channel cache: channelId → latest inscription
    QHash<QString, Inscription> m_latestCache;

    // Check if a channel matches any followed prefix
    bool matchesFollowedPrefix(const QString& channelId) const;

    // Internal: query blockchain for channels with prefix
    QStringList queryChannelsByPrefix(const QString& appPrefix);
};
