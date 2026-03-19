#pragma once
#include <QString>
#include <QCryptographicHash>

namespace LogosSync {

constexpr const char* CHANNEL_PREFIX = "λ";

inline QString deriveChannelId(const QString& appPrefix, const QString& uniqueId) {
    const QByteArray input = QStringLiteral("λ%1:%2").arg(appPrefix, uniqueId).toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

inline QString deriveConvoId(const QString& appPrefix, const QString& pubkeyHex) {
    const QByteArray input = QStringLiteral("%1-channel:%2").arg(appPrefix, pubkeyHex).toUtf8();
    return QString::fromLatin1(
        QCryptographicHash::hash(input, QCryptographicHash::Sha256).toHex());
}

namespace Apps {
    constexpr const char* BLOG  = "BLOG";
    constexpr const char* NOTES = "NOTES";
    constexpr const char* WIKI  = "WIKI";
    constexpr const char* DOCS  = "DOCS";
}

} // namespace LogosSync
