#pragma once
#include <QObject>
#include <QSet>
#include <QString>
#include <QByteArray>

class ModuleProxy;

namespace LogosSync {

// PeerSync — Chat SDK real-time P2P messaging wrapper.
//
// Each peer broadcasts messages to their deterministic "app channel"
// conversation.  The conversation ID is derived per-app as:
//   SHA-256("<appPrefix>-channel:<pubkeyHex>")  → hex string
//
// Messages are transmitted as hex-encoded UTF-8 payloads over the Chat SDK
// (org.logos.ChatSDKModuleInterface).  Callers receive decoded QByteArray
// payloads via the messageReceived signal.
//
// SyncModule wires chat_module's messageReceived signal to onMessage().
class PeerSync : public QObject {
    Q_OBJECT
public:
    explicit PeerSync(QObject* parent = nullptr);

    void setChatClient(ModuleProxy* chat);
    void setAppPrefix(const QString& appPrefix);
    void setOwnPubkey(const QString& pubkeyHex);

    bool isAvailable() const { return m_chat != nullptr; }

    // Initialise and start the Chat SDK node.  Emits started() on success.
    // If no chat client is available, emits started() immediately (offline mode).
    void start();

    // Send a message (raw bytes) to own channel.  The bytes are hex-encoded
    // for wire transport.
    void broadcast(const QByteArray& message);

    // Subscribe to a peer's channel (fetches history + watches live messages).
    void subscribe(const QString& pubkeyHex);

    // Stop listening to a peer's channel.
    void unsubscribe(const QString& pubkeyHex);

    // Called by SyncModule when chat_module fires its messageReceived signal.
    // Filters to watched conversations and decodes hex payloads.
    void onMessage(const QString& convoId,
                   const QString& senderPubkey,
                   const QString& contentHex);

    // Derive the conversation ID for a peer given the current appPrefix.
    QString convoIdForPubkey(const QString& pubkeyHex) const;

signals:
    // Decoded message payload from a subscribed peer (or own pubkey).
    void messageReceived(const QString& senderPubkey, const QByteArray& message);
    void started();
    void error(const QString& message);

private:
    ModuleProxy*  m_chat = nullptr;
    QString       m_appPrefix;
    QString       m_ownPubkey;
    QString       m_ownConvoId;
    QSet<QString> m_watchedConvos; // convo IDs we are actively watching
};

} // namespace LogosSync
