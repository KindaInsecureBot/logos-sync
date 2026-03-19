#include "peer_sync.h"
#include "sync_types.h"
#include "module_proxy.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace LogosSync {

PeerSync::PeerSync(QObject* parent)
    : QObject(parent)
{}

void PeerSync::setChatClient(ModuleProxy* chat)
{
    m_chat = chat;
}

void PeerSync::setAppPrefix(const QString& appPrefix)
{
    m_appPrefix = appPrefix;
    // Recalculate own convo ID if pubkey is already set.
    if (!m_ownPubkey.isEmpty())
        m_ownConvoId = convoIdForPubkey(m_ownPubkey);
}

void PeerSync::setOwnPubkey(const QString& pubkeyHex)
{
    m_ownPubkey  = pubkeyHex;
    m_ownConvoId = convoIdForPubkey(pubkeyHex);
}

QString PeerSync::convoIdForPubkey(const QString& pubkeyHex) const
{
    const QString prefix = m_appPrefix.isEmpty()
        ? QStringLiteral("logos")
        : m_appPrefix;
    return LogosSync::deriveConvoId(prefix, pubkeyHex);
}

void PeerSync::start()
{
    if (!m_chat) {
        emit started(); // no chat module available; proceed in offline mode
        return;
    }

    // Initialise the Chat SDK node with a minimal config.
    const QString config = QStringLiteral(
        R"({"logLevel":"WARN","preset":"logos.dev"})");
    m_chat->invokeRemoteMethod("chat_module", "initChat", config);
    m_chat->invokeRemoteMethod("chat_module", "startChat");

    // Watch own channel so self-published messages round-trip correctly.
    if (!m_ownConvoId.isEmpty()) {
        m_watchedConvos.insert(m_ownConvoId);
        m_chat->invokeRemoteMethod("chat_module", "getConversation", m_ownConvoId);
    }

    emit started();
}

void PeerSync::broadcast(const QByteArray& message)
{
    if (!m_chat || m_ownConvoId.isEmpty()) return;
    // Hex-encode for wire transport (Chat SDK uses hex content).
    const QString contentHex = QString::fromLatin1(message.toHex());
    m_chat->invokeRemoteMethod("chat_module", "sendMessage",
                               m_ownConvoId, contentHex);
}

void PeerSync::subscribe(const QString& pubkeyHex)
{
    if (pubkeyHex.isEmpty()) return;
    const QString convoId = convoIdForPubkey(pubkeyHex);
    if (m_watchedConvos.contains(convoId)) return;
    m_watchedConvos.insert(convoId);

    if (!m_chat) return;

    // Request conversation history; module returns a JSON array of messages:
    // [{"sender": "<pubkey>", "content": "<hex>", "timestamp": "..."}, ...]
    const QVariant result = m_chat->invokeRemoteMethod(
        "chat_module", "getConversation", convoId);
    const QString historyJson = result.toString();
    if (historyJson.isEmpty()) return;

    const QJsonArray messages = QJsonDocument::fromJson(historyJson.toUtf8()).array();
    for (const auto& msg : messages) {
        const QJsonObject m = msg.toObject();
        const QString sender     = m["sender"].toString();
        const QString contentHex = m["content"].toString();
        if (sender.isEmpty() || contentHex.isEmpty()) continue;
        onMessage(convoId, sender, contentHex);
    }
}

void PeerSync::unsubscribe(const QString& pubkeyHex)
{
    if (pubkeyHex.isEmpty()) return;
    m_watchedConvos.remove(convoIdForPubkey(pubkeyHex));
}

void PeerSync::onMessage(const QString& convoId,
                         const QString& senderPubkey,
                         const QString& contentHex)
{
    if (!m_watchedConvos.contains(convoId)) return;
    if (senderPubkey.isEmpty() || contentHex.isEmpty()) return;

    // Decode hex-encoded payload.
    const QByteArray raw = QByteArray::fromHex(contentHex.toLatin1());
    if (raw.isEmpty()) return;

    emit messageReceived(senderPubkey, raw);
}

} // namespace LogosSync
