# logos-pipe

Shared Storage/Sync library for **Logos Basecamp** plugins — content-addressed storage, Zone SDK L1 channel inscriptions, and Chat SDK peer messaging, in one reusable module.

---

## What / Why

Every Basecamp plugin that needs to store blobs, anchor data on-chain, or do live P2P messaging ends up re-implementing the same three wrappers.  `logos-pipe` extracts those wrappers from `logos-blog-lez` into a standalone `sync_module` that any plugin can depend on.

```
   blog_plugin ──┐
   notes_plugin ──┤──► sync_module ──► storage_module  (CID blobs)
   wiki_plugin ──┘                ──► blockchain_module (L1 channel inscriptions)
                                  ──► chat_module      (P2P messaging)
```

> **Build status:** Unit tests pass. Module compiles against
> logos-cpp-sdk + logos-liblogos (Qt 6.10.2, GCC 15.2.0).

---

## Architecture

```
logos-pipe/
├── src/
│   ├── sync_types.h           # deriveChannelId / deriveConvoId helpers + App constants
│   ├── content_store.h/cpp    # ContentStore      — wraps storage_module
│   ├── channel_sync.h/cpp     # ChannelSync       — wraps blockchain_module (L1 write)
│   ├── channel_indexer.h/cpp  # ChannelIndexer    — wraps blockchain_module (L1 read/discover)
│   ├── peer_sync.h/cpp        # PeerSync          — wraps chat_module (P2P messaging)
│   └── sync_module.h/cpp      # SyncModule        — PluginInterface entry point
├── tests/
│   ├── logos_api_client.h     # no-op stub (tests compile without Logos SDK)
│   ├── test_sync_types.cpp
│   ├── test_content_store.cpp
│   ├── test_channel_sync.cpp
│   ├── test_channel_indexer.cpp
│   └── test_peer_sync.cpp
├── modules/sync/manifest.json
├── CMakeLists.txt
├── Makefile
└── flake.nix
```

---

## API Reference

### sync_types.h

Shared constants and deterministic ID derivation helpers.

```cpp
#include "sync_types.h"

// Derive a channel ID: SHA-256("λAPP:uniqueId")
QString channelId = LogosSync::deriveChannelId("BLOG", authorPubkey);

// Derive a conversation ID for PeerSync: SHA-256("APP-channel:pubkey")
QString convoId = LogosSync::deriveConvoId("BLOG", authorPubkey);

// Predefined app prefixes
LogosSync::Apps::BLOG   // "BLOG"
LogosSync::Apps::NOTES  // "NOTES"
LogosSync::Apps::WIKI   // "WIKI"
LogosSync::Apps::DOCS   // "DOCS"
```

### ContentStore

Content-addressed blob storage over `storage_module` (org.logos.StorageModuleInterface).

```cpp
ContentStore* cs = syncModule->contentStore();
cs->setStorageClient(api->getClient("storage_module"));

// Store
QString cid = cs->store(jsonBytes);              // default 256 KB chunks
QString cid = cs->store(jsonBytes, 64 * 1024);   // custom chunk size

// Fetch
QByteArray data = cs->fetch(cid);                // downloadToUrl → chunk fallback

// Check / remove
bool ok = cs->exists(cid);
bool ok = cs->remove(cid);

// Signals
connect(cs, &ContentStore::stored,  [](const QString& cid) { ... });
connect(cs, &ContentStore::fetched, [](const QString& cid, const QByteArray& data) { ... });
connect(cs, &ContentStore::error,   [](const QString& msg) { ... });
```

### ChannelSync

Zone SDK L1 channel inscriptions over `blockchain_module` (org.logos.BlockchainModuleInterface).

#### Channel ID convention

```
SHA-256("λBLOG:"  + authorPubkey) → author's blog channel
SHA-256("λNOTES:" + authorPubkey) → author's notes channel
SHA-256("λ<APP>:" + uniqueId)     → any app-specific channel
```

```cpp
ChannelSync* ch = syncModule->channelSync();
ch->setBlockchainClient(api->getClient("blockchain_module"));
ch->setSigningKey(privkeyHex);  // optional: signing key for inscriptions

// Derive channel ID
QString channelId = ChannelSync::deriveChannelId("BLOG", authorPubkey);

// Inscribe
QString inscId = ch->inscribe(channelId, dataBytes);

// Query history → JSON array of inscriptions: [{"id":"...","data":"<hex>"},...]
QString json = ch->queryChannel(channelId);

// Live events
ch->follow(channelId);
ch->unfollow(channelId);

// Signals
connect(ch, &ChannelSync::inscribed,           [](const QString& ch, const QString& id) { ... });
connect(ch, &ChannelSync::inscriptionReceived, [](const QString& ch, const QString& id, const QByteArray& data) { ... });
connect(ch, &ChannelSync::error,               [](const QString& msg) { ... });
```

### ChannelIndexer

Read/discovery counterpart to ChannelSync. Discovers channels by app prefix, reads history with cursor-based pagination, and follows live inscriptions.

```cpp
ChannelIndexer* ci = syncModule->channelIndexer();
ci->setBlockchainClient(api->getClient("blockchain_module"));
ci->setKvClient(api->getClient("kv_module"));  // optional: enables cache persistence

// Discover all channels for an app prefix
// → JSON: [{"channelId":"...", "latestCid":"...", "inscriptionCount":N}, ...]
QString json = ci->discoverChannels("BLOG");

// Force-refresh discovery cache
ci->refreshDiscovery("BLOG");

// Read latest inscription for a channel (CID or raw data)
QByteArray cid = ci->getLatestInscription(channelId);

// Paginated history — pass empty cursor to start from beginning
IndexerPage page = ci->getHistory(channelId);
while (page.hasMore) {
    page = ci->getHistory(channelId, page.nextCursor);
    for (const Inscription& insc : page.inscriptions) { ... }
}

// Follow a specific channel for live inscriptions
ci->follow(channelId);
ci->unfollow(channelId);

// Follow all channels for a prefix (existing + future)
ci->followPrefix("BLOG");
ci->unfollowPrefix("BLOG");

// Persist / restore cache across restarts
ci->saveCacheState();
ci->loadCacheState();  // call on startup before discoverChannels()

// Signals
connect(ci, &ChannelIndexer::inscriptionDiscovered,
        [](const QString& ch, const QString& id, const QByteArray& data) { ... });
connect(ci, &ChannelIndexer::channelDiscovered,
        [](const QString& prefix, const QString& channelId) { ... });
connect(ci, &ChannelIndexer::error, [](const QString& msg) { ... });
```

#### IndexerCursor

```cpp
// Cursors are serializable — persist between sessions
QJsonObject json  = cursor.toJson();
IndexerCursor c2  = IndexerCursor::fromJson(json);
bool atStart      = cursor.isNull();  // slot==0 && lastMsgId empty
```

### PeerSync

Chat SDK real-time P2P messaging over `chat_module` (org.logos.ChatSDKModuleInterface).

#### Conversation ID derivation

```
SHA-256("<appPrefix>-channel:<pubkeyHex>") → peer's conversation channel
```

```cpp
PeerSync* ps = syncModule->peerSync();
ps->setChatClient(api->getClient("chat_module"));
ps->setAppPrefix("BLOG");          // sets the namespace for convo ID derivation
ps->setOwnPubkey(myPubkeyHex);     // starts watching own channel on start()

ps->start();                       // emits started(); safe offline (no client)

// Broadcast on own channel
ps->broadcast(QByteArray(signedEnvelopeJson.toUtf8()));

// Subscribe / unsubscribe to a peer's channel (fetches history on subscribe)
ps->subscribe(authorPubkeyHex);
ps->unsubscribe(authorPubkeyHex);

// Convo ID helper
QString convoId = ps->convoIdForPubkey(authorPubkeyHex);

// Signals
connect(ps, &PeerSync::messageReceived,
        [](const QString& sender, const QByteArray& msg) { ... });
connect(ps, &PeerSync::started, []() { ... });
connect(ps, &PeerSync::error,   [](const QString& msg) { ... });
```

### SyncModule (PluginInterface)

The main entry point.  Other plugins call:

```cpp
LogosAPIClient* sync = api->getClient("sync_module");

// ContentStore
sync->invokeRemoteMethod("sync_module", "store",   content, chunkSize);  // → QString CID
sync->invokeRemoteMethod("sync_module", "fetch",   cid);                 // → QByteArray
sync->invokeRemoteMethod("sync_module", "contentExists", cid);           // → bool
sync->invokeRemoteMethod("sync_module", "contentRemove", cid);           // → bool

// ChannelSync
sync->invokeRemoteMethod("sync_module", "deriveChannelId", appPrefix, uniqueId); // → QString
sync->invokeRemoteMethod("sync_module", "inscribe",    channelId, data);         // → QString inscID
sync->invokeRemoteMethod("sync_module", "queryChannel",channelId);               // → QString JSON
sync->invokeRemoteMethod("sync_module", "follow",      channelId);
sync->invokeRemoteMethod("sync_module", "unfollow",    channelId);

// ChannelIndexer
sync->invokeRemoteMethod("sync_module", "discoverChannels",    appPrefix);       // → QString JSON
sync->invokeRemoteMethod("sync_module", "refreshDiscovery",    appPrefix);
sync->invokeRemoteMethod("sync_module", "getLatestInscription",channelId);       // → QByteArray
sync->invokeRemoteMethod("sync_module", "getHistory",          channelId, cursorJson, limit); // → QString JSON
sync->invokeRemoteMethod("sync_module", "getInscriptionCount", channelId);       // → int
sync->invokeRemoteMethod("sync_module", "followIndex",         channelId);
sync->invokeRemoteMethod("sync_module", "unfollowIndex",       channelId);
sync->invokeRemoteMethod("sync_module", "followPrefix",        appPrefix);
sync->invokeRemoteMethod("sync_module", "unfollowPrefix",      appPrefix);

// PeerSync
sync->invokeRemoteMethod("sync_module", "setAppPrefix",     appPrefix);
sync->invokeRemoteMethod("sync_module", "setOwnPubkey",     pubkeyHex);
sync->invokeRemoteMethod("sync_module", "broadcast",        message);
sync->invokeRemoteMethod("sync_module", "peerSubscribe",    pubkeyHex);
sync->invokeRemoteMethod("sync_module", "peerUnsubscribe",  pubkeyHex);
```

---

## Usage Examples

### Blog plugin

```cpp
void BlogPlugin::initLogos(LogosAPI* api)
{
    LogosAPIClient* sync = api->getClient("sync_module");

    // Set up PeerSync for blog namespace
    sync->invokeRemoteMethod("sync_module", "setAppPrefix", QString("BLOG"));
    sync->invokeRemoteMethod("sync_module", "setOwnPubkey", m_ownPubkey);

    // Publish a post: store body → CID, broadcast notification
    QString cid = sync->invokeRemoteMethod("sync_module", "store",
                      postJson.toUtf8()).toString();
    QString channelId = sync->invokeRemoteMethod("sync_module",
                            "deriveChannelId", QString("BLOG"), m_ownPubkey).toString();
    sync->invokeRemoteMethod("sync_module", "inscribe", channelId, cid.toUtf8());

    // Subscribe to an author
    sync->invokeRemoteMethod("sync_module", "peerSubscribe", authorPubkey);
}
```

### Notes plugin

```cpp
// Notes uses its own channel namespace — no collision with BLOG channels
QString notesChannelId = ChannelSync::deriveChannelId(LogosSync::Apps::NOTES, noteId);
syncModule->channelSync()->inscribe(notesChannelId, noteData);
```

### Reader plugin (using ChannelIndexer)

```cpp
void ReaderPlugin::initLogos(LogosAPI* api)
{
    LogosAPIClient* sync = api->getClient("sync_module");

    // Discover all blog channels
    QString json = sync->invokeRemoteMethod("sync_module", "discoverChannels",
                       QString("BLOG")).toString();
    // json = [{"channelId":"...","inscriptionCount":N}, ...]

    // Follow prefix — receive live events for all current and future BLOG channels
    sync->invokeRemoteMethod("sync_module", "followPrefix", QString("BLOG"));

    // Connect to indexInscriptionDiscovered to receive live updates
    // (via SyncModule signal forwarded from ChannelIndexer)
}

// Read full history for an author's channel
void ReaderPlugin::loadAuthorHistory(const QString& channelId)
{
    ChannelIndexer* ci = syncModule->channelIndexer();
    IndexerPage page   = ci->getHistory(channelId, IndexerCursor(), 20);
    for (const Inscription& insc : page.inscriptions) {
        // insc.data is typically a CID — fetch full content via ContentStore
        QByteArray content = syncModule->contentStore()->fetch(
            QString::fromUtf8(insc.data));
    }
    if (page.hasMore) {
        // Continue with page.nextCursor on next call
    }
}
```

---

## Build

### Tests (no Logos SDK required)

```bash
make test
# or manually:
mkdir build-tests && cd build-tests
cmake .. -DBUILD_TESTS=ON
cmake --build . && ctest --output-on-failure
```

### Module (requires Logos SDK in Nix store)

```bash
make build-module
make install-module   # installs to ~/.local/share/Logos/.../modules/sync_module/
```

### Nix

```bash
nix build .#sync-module
```

---

## Module manifest

```json
{
    "name": "sync_module",
    "version": "0.1.0",
    "type": "core",
    "interface": "com.logos.SyncModuleInterface",
    "dependencies": ["kv_module"]
}
```
