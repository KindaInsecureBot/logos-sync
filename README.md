# logos-sync

Shared Storage/Sync library for **Logos Basecamp** plugins — content-addressed storage, Zone SDK L1 channel inscriptions, and Chat SDK peer messaging, in one reusable module.

---

## What / Why

Every Basecamp plugin that needs to store blobs, anchor data on-chain, or do live P2P messaging ends up re-implementing the same three wrappers.  `logos-sync` extracts those wrappers from `logos-blog-lez` into a standalone `sync_module` that any plugin can depend on.

```
   blog_plugin ──┐
   notes_plugin ──┤──► sync_module ──► storage_module  (CID blobs)
   wiki_plugin ──┘                ──► blockchain_module (L1 channel inscriptions)
                                  ──► chat_module      (P2P messaging)
```

> **Build status:** Unit tests pass (47/47). Module compiles against
> logos-cpp-sdk + logos-liblogos (Qt 6.10.2, GCC 15.2.0).

---

## Architecture

```
logos-sync/
├── src/
│   ├── sync_types.h        # deriveChannelId / deriveConvoId helpers + App constants
│   ├── content_store.h/cpp # ContentStore — wraps storage_module
│   ├── channel_sync.h/cpp  # ChannelSync  — wraps blockchain_module (L1 inscriptions)
│   ├── peer_sync.h/cpp     # PeerSync     — wraps chat_module (P2P messaging)
│   └── sync_module.h/cpp   # SyncModule   — PluginInterface entry point
├── tests/
│   ├── logos_api_client.h  # no-op stub (tests compile without Logos SDK)
│   ├── test_sync_types.cpp
│   ├── test_content_store.cpp
│   ├── test_channel_sync.cpp
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
