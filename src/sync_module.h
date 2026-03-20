#pragma once
#include "content_store.h"
#include "channel_sync.h"
#include "channel_indexer.h"
#include "peer_sync.h"
#include "core/interface.h"
#include <QtPlugin>

class LogosAPIClient;

// SyncModule — main PluginInterface entry point for logos-sync.
//
// Wires ContentStore, ChannelSync, ChannelIndexer, and PeerSync to the Logos
// SDK module system.  Other plugins obtain a client via:
//   LogosAPIClient* sync = api->getClient("sync_module");
// then call Q_INVOKABLE methods through invokeRemoteMethod().
//
// All four sub-APIs are also accessible directly when SyncModule is used
// as an in-process library (tests, future mono-build scenarios).
class SyncModule : public QObject, public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "com.example.PluginInterface" FILE "metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit SyncModule(QObject* parent = nullptr);

    Q_INVOKABLE QString name() const override { return QStringLiteral("sync_module"); }
    Q_INVOKABLE QString version() const override { return QStringLiteral("0.1.0"); }
    Q_INVOKABLE void    initLogos(LogosAPI* api);

    // ── ContentStore API ──────────────────────────────────────────────────────
    Q_INVOKABLE QString    store(const QByteArray& content, int chunkSize = 256 * 1024);
    Q_INVOKABLE QByteArray fetch(const QString& cid);
    Q_INVOKABLE bool       contentExists(const QString& cid);
    Q_INVOKABLE bool       contentRemove(const QString& cid);

    // ── ChannelSync API ───────────────────────────────────────────────────────
    Q_INVOKABLE static QString deriveChannelId(const QString& appPrefix,
                                               const QString& uniqueId);
    Q_INVOKABLE QString inscribe(const QString& channelId, const QByteArray& data);
    Q_INVOKABLE QString queryChannel(const QString& channelId);
    Q_INVOKABLE void    follow(const QString& channelId);
    Q_INVOKABLE void    unfollow(const QString& channelId);

    // ── ChannelIndexer API ────────────────────────────────────────────────────
    Q_INVOKABLE QString    discoverChannels(const QString& appPrefix);
    Q_INVOKABLE void       refreshDiscovery(const QString& appPrefix);
    Q_INVOKABLE QByteArray getLatestInscription(const QString& channelId);
    Q_INVOKABLE QString    getHistory(const QString& channelId,
                                      const QString& cursorJson = QString(),
                                      int limit = 50);
    Q_INVOKABLE int        getInscriptionCount(const QString& channelId);
    Q_INVOKABLE void       followIndex(const QString& channelId);
    Q_INVOKABLE void       unfollowIndex(const QString& channelId);
    Q_INVOKABLE void       followPrefix(const QString& appPrefix);
    Q_INVOKABLE void       unfollowPrefix(const QString& appPrefix);

    // ── PeerSync API ──────────────────────────────────────────────────────────
    Q_INVOKABLE void setAppPrefix(const QString& appPrefix);
    Q_INVOKABLE void setOwnPubkey(const QString& pubkeyHex);
    Q_INVOKABLE void broadcast(const QByteArray& message);
    Q_INVOKABLE void peerSubscribe(const QString& pubkeyHex);
    Q_INVOKABLE void peerUnsubscribe(const QString& pubkeyHex);

    // Direct sub-object access for in-process callers.
    ContentStore*   contentStore()    const { return m_contentStore; }
    ChannelSync*    channelSync()     const { return m_channelSync; }
    ChannelIndexer* channelIndexer()  const { return m_channelIndexer; }
    PeerSync*       peerSync()        const { return m_peerSync; }

signals:
    // ContentStore
    void stored(const QString& cid);
    void fetched(const QString& cid, const QByteArray& content);

    // ChannelSync
    void inscribed(const QString& channelId, const QString& inscriptionId);
    void inscriptionReceived(const QString& channelId,
                             const QString& inscriptionId,
                             const QByteArray& data);

    // ChannelIndexer
    void indexInscriptionDiscovered(const QString& channelId,
                                    const QString& inscriptionId,
                                    const QByteArray& data);
    void indexChannelDiscovered(const QString& appPrefix, const QString& channelId);

    // PeerSync
    void messageReceived(const QString& senderPubkey, const QByteArray& message);
    void peerSyncStarted();

    // Unified error signal: source is "content", "channel", "index", or "peer".
    void syncError(const QString& source, const QString& message);

private:
    ContentStore*   m_contentStore   = nullptr;
    ChannelSync*    m_channelSync    = nullptr;
    ChannelIndexer* m_channelIndexer = nullptr;
    PeerSync*       m_peerSync       = nullptr;

    void connectSignals();
    void connectBlockchainModule(LogosAPI* api);
    void connectChatModule(LogosAPI* api);
};
