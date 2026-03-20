#include <QtTest>
#include <QJsonDocument>
#include <QJsonArray>
#include "channel_indexer.h"
#include "logos_api_client.h"

// Mock blockchain client: configure prefix → channel-list responses.
class MockBlockchainClient : public LogosAPIClient {
public:
    // appPrefix (e.g. "BLOG") → list of channelIds to return
    QHash<QString, QStringList> prefixChannels;

    QVariant invokeRemoteMethod(const QString& /*obj*/,
                                const QString& method,
                                const QVariant& arg1 = {},
                                const QVariant& /*arg2*/ = {},
                                const QVariant& /*arg3*/ = {}) override
    {
        if (method == QLatin1String("queryChannelsByPrefix")) {
            const QString prefix = arg1.toString(); // e.g. "λBLOG:"
            for (auto it = prefixChannels.cbegin(); it != prefixChannels.cend(); ++it) {
                if (prefix.contains(it.key())) {
                    QJsonArray arr;
                    for (const QString& ch : it.value()) arr.append(ch);
                    return QString::fromUtf8(
                        QJsonDocument(arr).toJson(QJsonDocument::Compact));
                }
            }
        }
        return {};
    }
};

class TestChannelIndexer : public QObject {
    Q_OBJECT
private slots:

    // 1. discoverChannels with no client returns empty JSON array
    void discoverChannels_noClient_returnsEmpty()
    {
        ChannelIndexer ci;
        const QString result = ci.discoverChannels("BLOG");
        QCOMPARE(result, QStringLiteral("[]"));
    }

    // 2a. isAvailable with no client returns false
    void isAvailable_noClient_false()
    {
        ChannelIndexer ci;
        QVERIFY(!ci.isAvailable());
    }

    // 2b. isAvailable with client returns true
    void isAvailable_withClient_true()
    {
        ChannelIndexer ci;
        LogosAPIClient proxy;
        ci.setBlockchainClient(&proxy);
        QVERIFY(ci.isAvailable());
    }

    // 3. follow then onNewInscription emits inscriptionDiscovered
    void follow_addsToSet()
    {
        ChannelIndexer ci;
        const QString channelId = QStringLiteral("channel-abc");
        ci.follow(channelId);

        QSignalSpy spy(&ci, &ChannelIndexer::inscriptionDiscovered);
        ci.onNewInscription(channelId, "insc-1", QByteArray("payload"), 10);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toString(), channelId);
    }

    // 4. unfollow stops events
    void unfollow_stopsEvents()
    {
        ChannelIndexer ci;
        const QString channelId = QStringLiteral("channel-xyz");
        ci.follow(channelId);
        ci.unfollow(channelId);

        QSignalSpy spy(&ci, &ChannelIndexer::inscriptionDiscovered);
        ci.onNewInscription(channelId, "insc-2", QByteArray("payload"), 11);
        QCOMPARE(spy.count(), 0);
    }

    // 5. followPrefix matches channels via discovery cache
    void followPrefix_matchesChannels()
    {
        ChannelIndexer ci;
        MockBlockchainClient mock;
        const QString channelId = QStringLiteral("ch-blog-1");
        mock.prefixChannels["BLOG"] = {channelId};
        ci.setBlockchainClient(&mock);

        // Pre-populate discovery cache
        ci.discoverChannels("BLOG");
        ci.followPrefix("BLOG");

        QSignalSpy spy(&ci, &ChannelIndexer::inscriptionDiscovered);
        ci.onNewInscription(channelId, "insc-3", QByteArray("data"), 20);
        QCOMPARE(spy.count(), 1);
    }

    // 6. unfollowPrefix stops matching
    void unfollowPrefix_stopsMatching()
    {
        ChannelIndexer ci;
        MockBlockchainClient mock;
        const QString channelId = QStringLiteral("ch-blog-2");
        mock.prefixChannels["BLOG"] = {channelId};
        ci.setBlockchainClient(&mock);

        ci.discoverChannels("BLOG");
        ci.followPrefix("BLOG");
        ci.unfollowPrefix("BLOG");

        QSignalSpy spy(&ci, &ChannelIndexer::inscriptionDiscovered);
        ci.onNewInscription(channelId, "insc-4", QByteArray("data"), 21);
        QCOMPARE(spy.count(), 0);
    }

    // 7. onNewInscription for completely unfollowed channel does not emit
    void onNewInscription_unfollowedChannel_noEmit()
    {
        ChannelIndexer ci;
        QSignalSpy spy(&ci, &ChannelIndexer::inscriptionDiscovered);
        ci.onNewInscription("unknown-ch", "insc-x", QByteArray("data"), 5);
        QCOMPARE(spy.count(), 0);
    }

    // 8. getLatestInscription with no client returns empty
    void getLatestInscription_noClient_returnsEmpty()
    {
        ChannelIndexer ci;
        QVERIFY(ci.getLatestInscription("channel-1").isEmpty());
    }

    // 9. getHistory with no client returns empty page
    void getHistory_noClient_returnsEmpty()
    {
        ChannelIndexer ci;
        const IndexerPage page = ci.getHistory("channel-1");
        QVERIFY(page.inscriptions.isEmpty());
        QVERIFY(!page.hasMore);
        QVERIFY(page.nextCursor.isNull());
    }

    // 10. IndexerCursor serializes/deserializes correctly
    void indexerCursor_jsonRoundtrip()
    {
        IndexerCursor cursor;
        cursor.slot       = 42;
        cursor.lastMsgId  = QStringLiteral("msg-123");

        const IndexerCursor restored = IndexerCursor::fromJson(cursor.toJson());
        QCOMPARE(restored.slot,       cursor.slot);
        QCOMPARE(restored.lastMsgId,  cursor.lastMsgId);
        QVERIFY(!restored.isNull());
    }

    // 11. Inscription serializes/deserializes correctly
    void inscription_jsonRoundtrip()
    {
        Inscription insc;
        insc.channelId     = QStringLiteral("ch-abc");
        insc.inscriptionId = QStringLiteral("insc-xyz");
        insc.data          = QByteArray("hello world");
        insc.slot          = 100;

        const Inscription restored = Inscription::fromJson(insc.toJson());
        QCOMPARE(restored.channelId,     insc.channelId);
        QCOMPARE(restored.inscriptionId, insc.inscriptionId);
        QCOMPARE(restored.data,          insc.data);
        QCOMPARE(restored.slot,          insc.slot);
    }

    // 12. followPrefix + onNewInscription with brand-new channel emits channelDiscovered
    void followPrefix_newChannelDiscovered()
    {
        ChannelIndexer ci;
        MockBlockchainClient mock;
        const QString newChannelId = QStringLiteral("ch-blog-new");
        // Mock returns the new channel when queried (simulates chain discovery)
        mock.prefixChannels["BLOG"] = {newChannelId};
        ci.setBlockchainClient(&mock);

        // followPrefix but discovery cache is empty — channel NOT yet in cache
        ci.followPrefix("BLOG");

        QSignalSpy discovered(&ci, &ChannelIndexer::channelDiscovered);
        QSignalSpy inscribed(&ci,  &ChannelIndexer::inscriptionDiscovered);

        ci.onNewInscription(newChannelId, "insc-new", QByteArray("data"), 30);

        QCOMPARE(discovered.count(), 1);
        QCOMPARE(discovered.at(0).at(0).toString(), QStringLiteral("BLOG"));
        QCOMPARE(discovered.at(0).at(1).toString(), newChannelId);
        QCOMPARE(inscribed.count(), 1);
    }

    // 13. matchesFollowedPrefix: channel in cache matches; unknown channel does not
    void matchesFollowedPrefix_correctMatching()
    {
        ChannelIndexer ci;
        MockBlockchainClient mock;
        const QString knownChannel   = QStringLiteral("ch-known");
        const QString unknownChannel = QStringLiteral("ch-unknown");
        mock.prefixChannels["NOTES"] = {knownChannel};
        ci.setBlockchainClient(&mock);

        // Populate cache and start following prefix
        ci.discoverChannels("NOTES");
        ci.followPrefix("NOTES");

        // Known channel → inscriptionDiscovered fires
        QSignalSpy spyKnown(&ci, &ChannelIndexer::inscriptionDiscovered);
        ci.onNewInscription(knownChannel, "i1", QByteArray("d"), 1);
        QCOMPARE(spyKnown.count(), 1);

        // Unknown channel (not in cache, not returned by mock) → no emission
        // Remove from mock so queryChannelsByPrefix returns nothing for it
        mock.prefixChannels["NOTES"] = {knownChannel}; // doesn't include unknownChannel
        QSignalSpy spyUnknown(&ci, &ChannelIndexer::inscriptionDiscovered);
        ci.onNewInscription(unknownChannel, "i2", QByteArray("d"), 2);
        QCOMPARE(spyUnknown.count(), 0);
    }
};

QTEST_MAIN(TestChannelIndexer)
#include "test_channel_indexer.moc"
