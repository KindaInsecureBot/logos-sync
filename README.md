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

### Example 1: Blog — Publish a post

A blog author writes a post. The content gets stored (CID), the CID is inscribed
on the author's L1 channel, and peers are notified in real-time.

```cpp
void BlogPlugin::publishPost(const QByteArray& postJson)
{
    LogosAPIClient* sync = api->getClient("sync_module");

    // 1. Store the post content → get back a CID
    QString cid = sync->invokeRemoteMethod("sync_module", "store",
                      postJson).toString();
    // cid = "bafy2bzace..." (content-addressed, immutable)

    // 2. Inscribe the CID on the author's L1 blog channel
    //    Channel ID = SHA-256("λBLOG:" + authorPubkey)
    QString channelId = sync->invokeRemoteMethod("sync_module",
                            "deriveChannelId", QString("BLOG"), m_ownPubkey).toString();
    sync->invokeRemoteMethod("sync_module", "inscribe", channelId, cid.toUtf8());
    // Now anyone can find this post by reading the author's channel

    // 3. Broadcast to online subscribers via Chat SDK
    sync->invokeRemoteMethod("sync_module", "broadcast",
        QJsonDocument(QJsonObject{{"type","new_post"},{"cid",cid}}).toJson());
}
```

**What happens on-chain:** The author's channel `λBLOG:<pubkey>` now has
an inscription pointing to the post's CID. This is permanent and discoverable
by anyone running a Basecamp node.

---

### Example 2: Feed Reader — Discover and follow all blogs

A reader plugin wants to show a feed of all blogs on the network.
No server, no API — just query the L1 channels.

```cpp
void FeedReader::initLogos(LogosAPI* api)
{
    LogosAPIClient* sync = api->getClient("sync_module");

    // 1. Discover all blog channels on the network
    //    Queries the blockchain node for all channels with λBLOG: prefix
    QString json = sync->invokeRemoteMethod("sync_module", "discoverChannels",
                       QString("BLOG")).toString();
    // json = [
    //   {"channelId":"a1b2c3...","latestCid":"bafy...","inscriptionCount":12},
    //   {"channelId":"d4e5f6...","latestCid":"bafy...","inscriptionCount":3},
    //   ...
    // ]

    // 2. For each blog, get the latest post
    QJsonArray channels = QJsonDocument::fromJson(json.toUtf8()).array();
    for (const auto& ch : channels) {
        QString channelId = ch.toObject()["channelId"].toString();
        QByteArray latestCid = sync->invokeRemoteMethod("sync_module",
                                   "getLatestInscription", channelId).toByteArray();
        // Fetch the actual content from Storage
        QByteArray post = sync->invokeRemoteMethod("sync_module", "fetch",
                              QString::fromUtf8(latestCid)).toByteArray();
        displayPost(post);
    }

    // 3. Follow ALL blog channels — get notified of new posts from anyone
    sync->invokeRemoteMethod("sync_module", "followPrefix", QString("BLOG"));
    // Now indexInscriptionDiscovered fires whenever any blog publishes
    // indexChannelDiscovered fires when a brand new blog appears
}
```

**The key insight:** No indexer service needed. The Basecamp node is already
running — the ChannelIndexer just queries it. Cold start = one prefix scan.

---

### Example 3: Blog History — Paginated reading

Load an author's full post history with cursor-based pagination.

```cpp
void FeedReader::loadAuthorHistory(const QString& authorPubkey)
{
    LogosAPIClient* sync = api->getClient("sync_module");
    QString channelId = sync->invokeRemoteMethod("sync_module",
                            "deriveChannelId", QString("BLOG"), authorPubkey).toString();

    // First page (20 posts, oldest first)
    QString pageJson = sync->invokeRemoteMethod("sync_module", "getHistory",
                           channelId, QString("{}"), 20).toString();
    QJsonObject page = QJsonDocument::fromJson(pageJson.toUtf8()).object();

    QJsonArray inscriptions = page["inscriptions"].toArray();
    for (const auto& insc : inscriptions) {
        QString cid = insc.toObject()["data"].toString();
        QByteArray post = sync->invokeRemoteMethod("sync_module", "fetch", cid).toByteArray();
        displayPost(post);
    }

    // More pages?
    if (page["hasMore"].toBool()) {
        QJsonObject cursor = page["nextCursor"].toObject();
        // Pass cursor to next getHistory() call to continue
        // Cursor is serializable — save to disk for resume across restarts
    }
}
```

---

### Example 4: Encrypted Notes — Private backup with L1 anchoring

Notes are encrypted locally, stored content-addressed, and anchored on L1
so the author can always recover their history from any device with the same key.

```cpp
void NotesPlugin::backupNote(const QByteArray& encryptedNote, const QString& noteId)
{
    LogosAPIClient* sync = api->getClient("sync_module");

    // 1. Store encrypted blob (opaque bytes — Storage module doesn't decrypt)
    QString cid = sync->invokeRemoteMethod("sync_module", "store",
                      encryptedNote).toString();

    // 2. Inscribe on author's notes channel (separate namespace from blogs)
    QString channelId = sync->invokeRemoteMethod("sync_module",
                            "deriveChannelId", QString("NOTES"), m_ownPubkey).toString();
    QJsonObject envelope{{"noteId", noteId}, {"cid", cid}, {"ts", QDateTime::currentSecsSinceEpoch()}};
    sync->invokeRemoteMethod("sync_module", "inscribe", channelId,
        QJsonDocument(envelope).toJson(QJsonDocument::Compact));

    // 3. On another device: scan the NOTES channel, fetch CIDs, decrypt locally
    //    No server knows the content. The L1 channel is just pointers.
}
```

---

### Example 5: Wiki — Collaborative with version history

A wiki page is owned by a channel. Each edit inscribes a new CID.
The full edit history is the channel's inscription log.

```cpp
void WikiPlugin::savePage(const QString& pageId, const QByteArray& content)
{
    LogosAPIClient* sync = api->getClient("sync_module");

    // Store new version
    QString cid = sync->invokeRemoteMethod("sync_module", "store", content).toString();

    // Inscribe on the page's channel
    QString channelId = sync->invokeRemoteMethod("sync_module",
                            "deriveChannelId", QString("WIKI"), pageId).toString();
    sync->invokeRemoteMethod("sync_module", "inscribe", channelId, cid.toUtf8());

    // Notify collaborators
    sync->invokeRemoteMethod("sync_module", "broadcast",
        QJsonDocument(QJsonObject{{"type","edit"},{"page",pageId},{"cid",cid}}).toJson());
}

void WikiPlugin::showPageHistory(const QString& pageId)
{
    LogosAPIClient* sync = api->getClient("sync_module");
    QString channelId = sync->invokeRemoteMethod("sync_module",
                            "deriveChannelId", QString("WIKI"), pageId).toString();

    // Every inscription = one edit. Full version history on L1.
    QString history = sync->invokeRemoteMethod("sync_module", "getHistory",
                          channelId, QString("{}"), 100).toString();
    // Display as version timeline — each entry has slot (block height) for timestamp
}

void WikiPlugin::discoverAllPages()
{
    LogosAPIClient* sync = api->getClient("sync_module");

    // Find every wiki page on the network
    QString pages = sync->invokeRemoteMethod("sync_module", "discoverChannels",
                        QString("WIKI")).toString();
    // Each channel = one page. Build a page index from this.
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

---

## ⚡ Vibecoded — with receipts

This library was vibecoded by an AI agent ([Kibby](https://github.com/KindaInsecureBot)) in conversation with a human. That means the architecture, API design, and implementation were generated through iterative prompting — not hand-typed line by line.

**What we actually verified:**

| Check | Status |
|-------|--------|
| Unit tests (60 across 5 suites) | ✅ All passing |
| Compilation against real Logos SDK (`logos-cpp-sdk` + `logos-liblogos`) | ✅ Clean build, no warnings |
| Qt MOC generation (signals, slots, Q_INVOKABLE) | ✅ All headers parse correctly |
| SDK type compatibility (`LogosAPIClient`, `PluginInterface`, `onEvent`) | ✅ Verified against actual SDK headers |
| Build environment | Qt 6.10.2, GCC 15.2.0, Railway Nix box |
| Output artifact | `sync_module_plugin.so` (429KB shared library) |
| QPluginLoader (Basecamp runtime) | ✅ SyncModule instantiates, all 37 methods + 9 signals exposed, clean unload |

**What we did NOT test:**

- Actual calls to `storage_module`, `blockchain_module`, or `chat_module`
- Real L1 channel inscriptions or Storage uploads
- Multi-plugin scenarios (blog + notes using sync_module simultaneously)
- Performance under load

The unit tests use stub `LogosAPIClient` instances that return empty `QVariant` — they verify null-safety, state management, signal routing, ID derivation, and API surface correctness. Integration testing against live Logos modules is the next step.

**In short:** the plumbing compiles and loads in Basecamp, but it hasn't carried water through live Logos modules yet. PRs and integration tests welcome.
