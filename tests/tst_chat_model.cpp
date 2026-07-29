#include "chat/ChatModel.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QImageReader>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

namespace {
class TestHttpServer final : public QObject {
public:
  struct Response {
    int status = 200;
    QByteArray body;
    QByteArray location;
    int delayMs = 0;
  };

  explicit TestHttpServer(QObject *parent = nullptr) : QObject(parent)
  {
    connect(&m_server, &QTcpServer::newConnection, this, [this]() {
      while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
          QByteArray request = socket->property("requestBuffer").toByteArray();
          request += socket->readAll();
          socket->setProperty("requestBuffer", request);
          if (socket->property("handled").toBool() || !request.contains("\r\n\r\n")) return;
          socket->setProperty("handled", true);
          const QList<QByteArray> firstLine = request.left(request.indexOf("\r\n")).split(' ');
          const QByteArray path = firstLine.size() > 1 ? firstLine.at(1) : QByteArray();
          m_requests.push_back(QString::fromUtf8(path));
          const Response response = m_responses.value(QString::fromUtf8(path), Response{.status = 404});
          QTimer::singleShot(response.delayMs, this, [this, socket = QPointer<QTcpSocket>(socket), path, response]() {
            m_responseAttempts.insert(QString::fromUtf8(path), m_responseAttempts.value(QString::fromUtf8(path)) + 1);
            if (!socket) return;
            const QByteArray reason = response.status == 200 ? QByteArrayLiteral("OK") :
                (response.status == 302 ? QByteArrayLiteral("Found") : QByteArrayLiteral("Error"));
            QByteArray headers = QByteArrayLiteral("HTTP/1.1 ") + QByteArray::number(response.status) + ' ' + reason +
                QByteArrayLiteral("\r\nContent-Type: application/json\r\nContent-Length: ") + QByteArray::number(response.body.size()) +
                QByteArrayLiteral("\r\nConnection: close\r\n");
            if (!response.location.isEmpty()) headers += QByteArrayLiteral("Location: ") + response.location + QByteArrayLiteral("\r\n");
            socket->write(headers + QByteArrayLiteral("\r\n") + response.body);
            socket->disconnectFromHost();
          });
        });
      }
    });
    if (!m_server.listen(QHostAddress::LocalHost)) qFatal("Test HTTP server could not listen");
  }

  void respond(const QString &path, Response response) { m_responses.insert(path, std::move(response)); }
  [[nodiscard]] QUrl baseUrl() const { return QUrl(QStringLiteral("http://127.0.0.1:%1/v3/").arg(m_server.serverPort())); }
  [[nodiscard]] int requestCount(const QString &path) const { return m_requests.count(path); }
  [[nodiscard]] int responseAttemptCount(const QString &path) const { return m_responseAttempts.value(path); }

private:
  QTcpServer m_server;
  QHash<QString, Response> m_responses;
  QHash<QString, int> m_responseAttempts;
  QStringList m_requests;
};
}

class ChatModelTests final : public QObject {
  Q_OBJECT

private slots:
  void badgeScopesHaveDeterministicPriority();
  void staleBadgeResponsesCannotOverwriteCurrentState();
  void badgeVersionReplacementIsAtomic();
  void localMessagesUseCanonicalColorAndReconcileInPlace();
  void authoritativeMessagesWinRegardlessOfArrivalOrder();
  void emptyColorsUseStableCaseInsensitiveFallback();
  void learnedColorsNotifyExistingRows();
  void colorKnowledgeSurvivesResetAndActions();
  void moderationUpdatesDependentRolesAndClearsWholeChannel();
  void explicitDisconnectDisablesReconnect();
  void sevenTvParsingAndUrlSelection();
  void sevenTvRuntimeSupportsSelectedImageFormat();
  void sevenTvLoadsWithoutTwitchCredentialsAndCoalescesRequests();
  void failedSevenTvResponsesPreserveUsableState();
  void staleSevenTvResponsesCannotCrossChannelIdentity();
  void delayedSevenTvResponseCannotCrossChannelSwitch();
  void channelChangesKeepGlobalAndClearChannelEmotes();
  void emoteMatchingAndCacheKeysRemainProviderSeparated();

private:
  static QByteArray badgePayload(const QString &set, const QString &version, const QString &url)
  {
    const QJsonObject asset{{QStringLiteral("id"), version},
                            {QStringLiteral("title"), QStringLiteral("Badge %1").arg(version)},
                            {QStringLiteral("image_url_2x"), url}};
    const QJsonObject badgeSet{{QStringLiteral("set_id"), set},
                               {QStringLiteral("versions"), QJsonArray{asset}}};
    return QJsonDocument(QJsonObject{{QStringLiteral("data"), QJsonArray{badgeSet}}}).toJson(QJsonDocument::Compact);
  }

  static QString firstBadgeUrl(const ChatModel &model, const QString &key)
  {
    ChatEvent event;
    event.badges = {key};
    const QVariantList assets = model.badgeAssetsFor(event);
    return assets.isEmpty() ? QString() : assets.first().toMap().value(QStringLiteral("imageUrl")).toString();
  }

  static QByteArray sevenTvPayload(const QString &setId, const QString &emoteId, const QString &name, bool channelScoped)
  {
    const QJsonArray files{
        QJsonObject{{QStringLiteral("name"), QStringLiteral("2x.webp")}, {QStringLiteral("static_name"), QStringLiteral("2x_static.webp")}},
        QJsonObject{{QStringLiteral("name"), QStringLiteral("1x.png")}}};
    const QJsonObject data{{QStringLiteral("host"), QJsonObject{{QStringLiteral("url"), QStringLiteral("//cdn.7tv.app/emote/%1").arg(emoteId)},
                                                                 {QStringLiteral("files"), files}}}};
    const QJsonObject emote{{QStringLiteral("id"), emoteId}, {QStringLiteral("name"), name}, {QStringLiteral("data"), data}};
    const QJsonObject set{{QStringLiteral("id"), setId}, {QStringLiteral("emotes"), QJsonArray{emote}}};
    return QJsonDocument(channelScoped ? QJsonObject{{QStringLiteral("emote_set"), set}} : set).toJson(QJsonDocument::Compact);
  }
};

void ChatModelTests::badgeScopesHaveDeterministicPriority()
{
  ChatModel model;
  model.m_channel = QStringLiteral("alpha");
  model.m_requestGeneration = 8;
  model.m_badgeRequestGeneration = 3;

  QVERIFY(model.applyBadgePayload(badgePayload(QStringLiteral("subscriber"), QStringLiteral("1"), QStringLiteral("https://global/1.png")),
                                  false, 8, 3, QStringLiteral("alpha")));
  QCOMPARE(firstBadgeUrl(model, QStringLiteral("subscriber/1")), QStringLiteral("https://global/1.png"));

  QVERIFY(model.applyBadgePayload(badgePayload(QStringLiteral("subscriber"), QStringLiteral("1"), QStringLiteral("https://channel/1.png")),
                                  true, 8, 3, QStringLiteral("alpha")));
  QCOMPARE(firstBadgeUrl(model, QStringLiteral("subscriber/1")), QStringLiteral("https://channel/1.png"));

  model.m_channelBadgeAssets.clear();
  QCOMPARE(firstBadgeUrl(model, QStringLiteral("subscriber/1")), QStringLiteral("https://global/1.png"));
  QVERIFY(!model.applyBadgePayload(QByteArrayLiteral("not-json"), true, 8, 3, QStringLiteral("alpha")));
  QCOMPARE(firstBadgeUrl(model, QStringLiteral("subscriber/1")), QStringLiteral("https://global/1.png"));
}

void ChatModelTests::staleBadgeResponsesCannotOverwriteCurrentState()
{
  ChatModel model;
  model.m_channel = QStringLiteral("alpha");
  model.m_requestGeneration = 12;
  model.m_badgeRequestGeneration = 6;
  QVERIFY(model.applyBadgePayload(badgePayload(QStringLiteral("moderator"), QStringLiteral("1"), QStringLiteral("https://new/current.png")),
                                  true, 12, 6, QStringLiteral("alpha")));
  QVERIFY(!model.applyBadgePayload(badgePayload(QStringLiteral("moderator"), QStringLiteral("1"), QStringLiteral("https://old/stale.png")),
                                   true, 11, 6, QStringLiteral("alpha")));
  QVERIFY(!model.applyBadgePayload(badgePayload(QStringLiteral("moderator"), QStringLiteral("1"), QStringLiteral("https://old/same-context.png")),
                                   true, 12, 5, QStringLiteral("alpha")));
  QVERIFY(!model.applyBadgePayload(badgePayload(QStringLiteral("moderator"), QStringLiteral("1"), QStringLiteral("https://other/channel.png")),
                                   true, 12, 6, QStringLiteral("beta")));
  QCOMPARE(firstBadgeUrl(model, QStringLiteral("moderator/1")), QStringLiteral("https://new/current.png"));
}

void ChatModelTests::badgeVersionReplacementIsAtomic()
{
  ChatModel model;
  model.m_channel = QStringLiteral("alpha");
  model.m_requestGeneration = 2;
  model.m_badgeRequestGeneration = 4;
  QVERIFY(model.applyBadgePayload(badgePayload(QStringLiteral("subscriber"), QStringLiteral("1"), QStringLiteral("https://channel/v1.png")),
                                  true, 2, 4, QStringLiteral("alpha")));
  QVERIFY(model.applyBadgePayload(badgePayload(QStringLiteral("subscriber"), QStringLiteral("2"), QStringLiteral("https://channel/v2.png")),
                                  true, 2, 4, QStringLiteral("alpha")));
  QVERIFY(firstBadgeUrl(model, QStringLiteral("subscriber/1")).isEmpty());
  QCOMPARE(firstBadgeUrl(model, QStringLiteral("subscriber/2")), QStringLiteral("https://channel/v2.png"));
}

void ChatModelTests::localMessagesUseCanonicalColorAndReconcileInPlace()
{
  ChatModel model;
  model.m_channel = QStringLiteral("alpha");
  model.m_senderLogin = QStringLiteral("tester");
  model.m_senderDisplayName = QStringLiteral("Tester");
  model.m_senderColor = QStringLiteral("#9146ff");

  model.insertSentMessage(QStringLiteral("alpha"), QStringLiteral("message-1"), QStringLiteral("hello"),
                          QStringLiteral("TESTER"), QStringLiteral("Tester"));
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.data(model.index(0), ChatModel::ColorRole).toString(), QStringLiteral("#9146ff"));
  QVERIFY(model.m_events.first().provisional);

  ChatEvent authoritative;
  authoritative.type = ChatEvent::Message;
  authoritative.id = QStringLiteral("message-1");
  authoritative.channel = QStringLiteral("alpha");
  authoritative.authorLogin = QStringLiteral("tester");
  authoritative.displayName = QStringLiteral("Tester");
  authoritative.body = QStringLiteral("hello");
  authoritative.color = QStringLiteral("#00ff00");
  authoritative.badges = {QStringLiteral("subscriber/2")};
  authoritative.action = true;
  authoritative.timestamp = QDateTime::currentDateTimeUtc();
  model.insertEvent(authoritative);

  QCOMPARE(model.rowCount(), 1);
  QVERIFY(!model.m_events.first().provisional);
  QCOMPARE(model.data(model.index(0), ChatModel::ColorRole).toString(), QStringLiteral("#00ff00"));
  QCOMPARE(model.data(model.index(0), ChatModel::ActionRole).toBool(), true);
  QCOMPARE(model.data(model.index(0), ChatModel::BadgesRole).toStringList(), QStringList{QStringLiteral("subscriber/2")});
}

void ChatModelTests::authoritativeMessagesWinRegardlessOfArrivalOrder()
{
  ChatModel model;
  ChatEvent authoritative;
  authoritative.type = ChatEvent::Message;
  authoritative.id = QStringLiteral("server-first");
  authoritative.authorLogin = QStringLiteral("tester");
  authoritative.displayName = QStringLiteral("Tester");
  authoritative.body = QStringLiteral("hello");
  authoritative.color = QStringLiteral("#336699");
  authoritative.timestamp = QDateTime::currentDateTimeUtc();
  model.insertEvent(authoritative);
  model.insertSentMessage(QStringLiteral("alpha"), QStringLiteral("server-first"), QStringLiteral("hello"),
                          QStringLiteral("tester"), QStringLiteral("Tester"));
  QCOMPARE(model.rowCount(), 1);
  QVERIFY(!model.m_events.first().provisional);
  QCOMPARE(model.data(model.index(0), ChatModel::ColorRole).toString(), QStringLiteral("#336699"));

  model.clear();
  model.insertSentMessage(QStringLiteral("alpha"), QString(), QStringLiteral("missing id"),
                          QStringLiteral("tester"), QStringLiteral("Tester"));
  QCOMPARE(model.rowCount(), 0);
}

void ChatModelTests::emptyColorsUseStableCaseInsensitiveFallback()
{
  ChatModel model;
  const QString lower = model.resolvedUserColor(QStringLiteral("exampleuser"));
  const QString mixed = model.resolvedUserColor(QStringLiteral("ExampleUser"));
  QCOMPARE(lower, mixed);
  QVERIFY(QColor(lower).isValid());
  QVERIFY(lower != QStringLiteral("#ffffff"));
  QVERIFY(lower != QStringLiteral("#f5f5f5"));
}

void ChatModelTests::learnedColorsNotifyExistingRows()
{
  ChatModel model;
  ChatEvent first;
  first.type = ChatEvent::Message;
  first.id = QStringLiteral("first");
  first.authorLogin = QStringLiteral("tester");
  first.body = QStringLiteral("before color metadata");
  first.timestamp = QDateTime::currentDateTimeUtc();
  model.insertEvent(first);
  const QString fallback = model.data(model.index(0), ChatModel::ColorRole).toString();

  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  ChatEvent second = first;
  second.id = QStringLiteral("second");
  second.color = QStringLiteral("#abcdef");
  model.insertEvent(second);
  QVERIFY(model.data(model.index(0), ChatModel::ColorRole).toString() != fallback);
  QCOMPARE(model.data(model.index(0), ChatModel::ColorRole).toString(), QStringLiteral("#abcdef"));
  QVERIFY(!changed.isEmpty());
  const QList<int> roles = changed.first().at(2).value<QList<int>>();
  QVERIFY(roles.contains(ChatModel::ColorRole));
}

void ChatModelTests::colorKnowledgeSurvivesResetAndActions()
{
  ChatModel model;
  ChatEvent event;
  event.type = ChatEvent::Message;
  event.id = QStringLiteral("known-1");
  event.authorLogin = QStringLiteral("tester");
  event.displayName = QStringLiteral("Tester");
  event.color = QStringLiteral("#123abc");
  event.action = true;
  event.timestamp = QDateTime::currentDateTimeUtc();
  model.insertEvent(event);
  QCOMPARE(model.data(model.index(0), ChatModel::ColorRole).toString(), QStringLiteral("#123abc"));

  model.clear();
  QCOMPARE(model.resolvedUserColor(QStringLiteral("TESTER")), QStringLiteral("#123abc"));
  event.id = QStringLiteral("known-2");
  event.color.clear();
  model.insertEvent(event);
  QCOMPARE(model.data(model.index(0), ChatModel::ColorRole).toString(), QStringLiteral("#123abc"));
}

void ChatModelTests::moderationUpdatesDependentRolesAndClearsWholeChannel()
{
  ChatModel model;
  for (int i = 0; i < 2; ++i) {
    ChatEvent event;
    event.type = ChatEvent::Message;
    event.id = QStringLiteral("message-%1").arg(i);
    event.authorLogin = i == 0 ? QStringLiteral("one") : QStringLiteral("two");
    event.body = QStringLiteral("body");
    if (i == 0) event.emotes = {ChatEmoteRange{.id = QStringLiteral("25"), .start = 0, .end = 3}};
    event.timestamp = QDateTime::currentDateTimeUtc();
    model.insertEvent(event);
  }
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  ChatEvent clear;
  clear.type = ChatEvent::ClearChat;
  model.applyModeration(clear);
  QVERIFY(model.m_events.at(0).deleted);
  QVERIFY(model.m_events.at(1).deleted);
  QVERIFY(changed.count() >= 2);
  const QList<int> roles = changed.first().at(2).value<QList<int>>();
  QVERIFY(roles.contains(ChatModel::DeletedRole));
  QVERIFY(roles.contains(ChatModel::MessagePartsRole));
  QVERIFY(roles.contains(ChatModel::PlainTextRole));
  const QVariantList parts = model.data(model.index(0), ChatModel::MessagePartsRole).toList();
  QCOMPARE(parts.size(), 1);
  QCOMPARE(parts.first().toMap().value(QStringLiteral("type")).toString(), QStringLiteral("text"));
  QCOMPARE(parts.first().toMap().value(QStringLiteral("text")).toString(), QStringLiteral("<message deleted>"));
}

void ChatModelTests::explicitDisconnectDisablesReconnect()
{
  ChatModel model;
  model.m_channel = QStringLiteral("alpha");
  model.m_shouldReconnect = true;
  model.m_reconnectTimer.start(1000);
  model.disconnectChat();
  QVERIFY(!model.m_shouldReconnect);
  QVERIFY(!model.m_reconnectTimer.isActive());
}

void ChatModelTests::sevenTvParsingAndUrlSelection()
{
  QString error;
  const auto parsed = ChatModel::parseSevenTvPayload(
      sevenTvPayload(QStringLiteral("set-global"), QStringLiteral("emote-one"), QStringLiteral("Wave"), false), false, &error);
  QVERIFY2(parsed.has_value(), qPrintable(error));
  QCOMPARE(parsed->id, QStringLiteral("set-global"));
  QCOMPARE(parsed->emotes.size(), 1);
  QCOMPARE(parsed->emotes.first().id, QStringLiteral("emote-one"));
  QCOMPARE(parsed->emotes.first().imageUrl, QStringLiteral("https://cdn.7tv.app/emote/emote-one/2x_static.webp"));

  QJsonObject animatedOnly{{QStringLiteral("host"), QJsonObject{
      {QStringLiteral("url"), QStringLiteral("https://cdn.7tv.app/emote/animated/")},
      {QStringLiteral("files"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("2x.webp")}}}}}}};
  QCOMPARE(ChatModel::sevenTvImageUrl(animatedOnly), QStringLiteral("https://cdn.7tv.app/emote/animated/2x.webp"));
  QVERIFY(!ChatModel::parseSevenTvPayload(QByteArrayLiteral("not-json"), false, &error));
  QVERIFY(!error.isEmpty());
  QVERIFY(!ChatModel::parseSevenTvPayload(QByteArrayLiteral("{\"id\":\"broken\"}"), false, &error));
  QVERIFY(!ChatModel::parseSevenTvPayload(QByteArrayLiteral("{\"emotes\":[]}"), false, &error));
  QJsonObject insecure{{QStringLiteral("host"), QJsonObject{
      {QStringLiteral("url"), QStringLiteral("http://cdn.7tv.app/emote/insecure")},
      {QStringLiteral("files"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("2x.webp")}}}}}}};
  QVERIFY(ChatModel::sevenTvImageUrl(insecure).isEmpty());
}

void ChatModelTests::sevenTvRuntimeSupportsSelectedImageFormat()
{
  QVERIFY2(QImageReader::supportedImageFormats().contains(QByteArrayLiteral("webp")),
           "Qt's WebP image-format plugin is required for selected 7TV assets");
}

void ChatModelTests::sevenTvLoadsWithoutTwitchCredentialsAndCoalescesRequests()
{
  TestHttpServer server;
  server.respond(QStringLiteral("/v3/emote-sets/global"), {.status = 302, .location = QByteArrayLiteral("/v3/global-data")});
  server.respond(QStringLiteral("/v3/global-data"), {.body = sevenTvPayload(QStringLiteral("global-set"), QStringLiteral("global-id"), QStringLiteral("GlobalWave"), false)});
  server.respond(QStringLiteral("/v3/users/twitch/100"), {.body = sevenTvPayload(QStringLiteral("channel-set"), QStringLiteral("channel-id"), QStringLiteral("ChannelWave"), true), .delayMs = 30});

  ChatModel model;
  model.m_sevenTvApiBaseUrl = server.baseUrl();
  QVERIFY(model.clientId().isEmpty());
  QVERIFY(model.accessToken().isEmpty());
  model.join(QStringLiteral("alpha"), QStringLiteral("100"));
  model.requestSevenTvGlobalEmotes();
  model.requestSevenTvChannelEmotes(QStringLiteral("100"), QStringLiteral("alpha"));

  QTRY_VERIFY_WITH_TIMEOUT(model.m_sevenTvGlobalLoaded, 2000);
  QTRY_VERIFY_WITH_TIMEOUT(model.m_sevenTvChannelLoaded, 2000);
  QCOMPARE(model.m_sevenTvGlobalSetId, QStringLiteral("global-set"));
  QCOMPARE(model.m_sevenTvChannelSetId, QStringLiteral("channel-set"));
  QCOMPARE(server.requestCount(QStringLiteral("/v3/emote-sets/global")), 1);
  QCOMPARE(server.requestCount(QStringLiteral("/v3/global-data")), 1);
  QCOMPARE(server.requestCount(QStringLiteral("/v3/users/twitch/100")), 1);
  QCOMPARE(model.emotePickerEmotes().size(), 2);
  model.disconnectChat();
}

void ChatModelTests::failedSevenTvResponsesPreserveUsableState()
{
  TestHttpServer server;
  server.respond(QStringLiteral("/v3/emote-sets/global"), {.status = 500, .body = QByteArrayLiteral("{\"error\":\"temporary\"}")});
  ChatModel model;
  model.m_sevenTvApiBaseUrl = server.baseUrl();
  model.m_sevenTvGlobalEmotes.push_back({QStringLiteral("cached"), QStringLiteral("Cached"), QStringLiteral("https://cache/cached.webp"), QStringLiteral("7TV"), QStringLiteral("Global")});
  model.requestSevenTvGlobalEmotes(true);
  QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(QStringLiteral("/v3/emote-sets/global")), 1, 2000);
  QTRY_VERIFY_WITH_TIMEOUT(!model.m_sevenTvGlobalReply, 2000);
  QCOMPARE(model.m_sevenTvGlobalEmotes.size(), 1);
  QCOMPARE(model.m_sevenTvGlobalEmotes.first().id, QStringLiteral("cached"));

  ++model.m_sevenTvGlobalRequestId;
  QVERIFY(!model.applySevenTvPayload(QByteArrayLiteral("invalid"), false, {}, {}, model.m_sevenTvGlobalRequestId));
  QCOMPARE(model.m_sevenTvGlobalEmotes.first().id, QStringLiteral("cached"));

  model.m_channel = QStringLiteral("alpha");
  model.m_broadcasterId = QStringLiteral("100");
  model.requestSevenTvChannelEmotes(QStringLiteral("100"), QStringLiteral("alpha"));
  QTRY_VERIFY_WITH_TIMEOUT(model.m_sevenTvChannelLoaded, 2000);
  QCOMPARE(server.requestCount(QStringLiteral("/v3/users/twitch/100")), 1);
  model.requestSevenTvChannelEmotes(QStringLiteral("100"), QStringLiteral("alpha"));
  QCoreApplication::processEvents();
  QCOMPARE(server.requestCount(QStringLiteral("/v3/users/twitch/100")), 1);
}

void ChatModelTests::staleSevenTvResponsesCannotCrossChannelIdentity()
{
  ChatModel model;
  model.m_channel = QStringLiteral("beta");
  model.m_broadcasterId = QStringLiteral("200");
  model.m_sevenTvChannelRequestId = 5;
  const QByteArray current = sevenTvPayload(QStringLiteral("current-set"), QStringLiteral("current"), QStringLiteral("Current"), true);
  const QByteArray stale = sevenTvPayload(QStringLiteral("stale-set"), QStringLiteral("stale"), QStringLiteral("Stale"), true);
  QVERIFY(model.applySevenTvPayload(current, true, QStringLiteral("beta"), QStringLiteral("200"), 5));
  QVERIFY(!model.applySevenTvPayload(stale, true, QStringLiteral("alpha"), QStringLiteral("100"), 4));
  QVERIFY(!model.applySevenTvPayload(stale, true, QStringLiteral("beta"), QStringLiteral("100"), 5));
  QCOMPARE(model.m_sevenTvChannelSetId, QStringLiteral("current-set"));
  QCOMPARE(model.m_sevenTvChannelEmotes.first().id, QStringLiteral("current"));
}

void ChatModelTests::delayedSevenTvResponseCannotCrossChannelSwitch()
{
  TestHttpServer server;
  server.respond(QStringLiteral("/v3/users/twitch/100"), {.body = sevenTvPayload(QStringLiteral("alpha-set"), QStringLiteral("alpha-id"), QStringLiteral("Alpha"), true), .delayMs = 200});
  server.respond(QStringLiteral("/v3/users/twitch/200"), {.body = sevenTvPayload(QStringLiteral("beta-set"), QStringLiteral("beta-id"), QStringLiteral("Beta"), true)});

  ChatModel model;
  model.m_sevenTvApiBaseUrl = server.baseUrl();
  model.m_sevenTvGlobalLoaded = true;
  model.join(QStringLiteral("alpha"), QStringLiteral("100"));
  QTRY_COMPARE_WITH_TIMEOUT(server.requestCount(QStringLiteral("/v3/users/twitch/100")), 1, 1000);
  model.join(QStringLiteral("beta"), QStringLiteral("200"));
  QTRY_VERIFY_WITH_TIMEOUT(model.m_sevenTvChannelLoaded, 2000);
  QCOMPARE(model.m_sevenTvChannelSetId, QStringLiteral("beta-set"));
  QCOMPARE(model.m_sevenTvChannelEmotes.first().id, QStringLiteral("beta-id"));
  QTRY_COMPARE_WITH_TIMEOUT(server.responseAttemptCount(QStringLiteral("/v3/users/twitch/100")), 1, 1000);
  QCOMPARE(model.m_sevenTvChannelSetId, QStringLiteral("beta-set"));
  model.disconnectChat();
}

void ChatModelTests::channelChangesKeepGlobalAndClearChannelEmotes()
{
  ChatModel model;
  model.m_sevenTvGlobalLoaded = true;
  model.m_sevenTvGlobalEmotes.push_back({QStringLiteral("global"), QStringLiteral("Global"), QStringLiteral("https://cache/global.webp"), QStringLiteral("7TV"), QStringLiteral("Global")});
  model.m_channel = QStringLiteral("alpha");
  model.m_broadcasterId = QStringLiteral("100");
  model.m_sevenTvChannelLoaded = true;
  model.m_sevenTvChannelBroadcasterId = QStringLiteral("100");
  model.m_sevenTvChannelEmotes.push_back({QStringLiteral("channel"), QStringLiteral("Channel"), QStringLiteral("https://cache/channel.webp"), QStringLiteral("7TV"), QStringLiteral("Channel")});
  model.join(QStringLiteral("beta"), QStringLiteral("200"));
  QCOMPARE(model.m_sevenTvGlobalEmotes.size(), 1);
  QVERIFY(model.m_sevenTvChannelEmotes.isEmpty());
  QVERIFY(!model.m_sevenTvChannelLoaded);
  QCOMPARE(model.m_broadcasterId, QStringLiteral("200"));
  model.m_sevenTvChannelLoaded = true;
  model.m_sevenTvChannelBroadcasterId = QStringLiteral("200");
  model.m_sevenTvChannelEmotes.push_back({QStringLiteral("old"), QStringLiteral("Old"), QStringLiteral("https://cache/old.webp"), QStringLiteral("7TV"), QStringLiteral("Channel")});
  model.updateChannelIdentity(QStringLiteral("beta"), QStringLiteral("300"));
  QCOMPARE(model.m_broadcasterId, QStringLiteral("300"));
  QVERIFY(model.m_sevenTvChannelEmotes.isEmpty());
  model.disconnectChat();
}

void ChatModelTests::emoteMatchingAndCacheKeysRemainProviderSeparated()
{
  ChatModel model;
  model.m_globalEmotes.push_back({QStringLiteral("twitch-id"), QStringLiteral("Wave"), QStringLiteral("https://twitch/Wave.png"), QStringLiteral("Twitch"), QStringLiteral("Global")});
  model.m_sevenTvGlobalEmotes.push_back({QStringLiteral("seven-id"), QStringLiteral("Wave"), QStringLiteral("https://7tv/Wave.webp"), QStringLiteral("7TV"), QStringLiteral("Global")});
  model.m_sevenTvChannelEmotes.push_back({QStringLiteral("seven-only"), QStringLiteral("SevenOnly"), QStringLiteral("https://7tv/SevenOnly.webp"), QStringLiteral("7TV"), QStringLiteral("Channel")});
  QCOMPARE(model.emotePickerEmotes().size(), 3);
  QCOMPARE(model.preloadEmoteImageUrls().size(), 3);

  ChatEvent event;
  event.body = QStringLiteral("Wave SevenOnly sevenonly Missing (SevenOnly)");
  const QVariantList parts = model.messagePartsFor(event);
  int emoteCount = 0;
  QStringList providers;
  QString combinedText;
  for (const QVariant &partValue : parts) {
    const QVariantMap part = partValue.toMap();
    if (part.value(QStringLiteral("type")) == QStringLiteral("emote")) {
      ++emoteCount;
      providers.push_back(part.value(QStringLiteral("provider")).toString());
    } else combinedText += part.value(QStringLiteral("text")).toString();
  }
  QCOMPARE(emoteCount, 3);
  QCOMPARE(providers, QStringList({QStringLiteral("Twitch"), QStringLiteral("7TV"), QStringLiteral("7TV")}));
  QVERIFY(combinedText.contains(QStringLiteral("sevenonly")));
  QVERIFY(combinedText.contains(QStringLiteral("Missing")));

  model.m_sevenTvChannelEmotes.clear();
  for (int index = 0; index < 700; ++index) {
    model.m_sevenTvChannelEmotes.push_back({QString::number(index), QStringLiteral("Emote%1").arg(index),
                                            QStringLiteral("https://7tv/%1.webp").arg(index), QStringLiteral("7TV"), QStringLiteral("Channel")});
  }
  QCOMPARE(model.preloadEmoteImageUrls().size(), 600);
}

QTEST_MAIN(ChatModelTests)
#include "tst_chat_model.moc"
