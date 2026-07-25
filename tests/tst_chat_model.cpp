#include "chat/ChatModel.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

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

QTEST_MAIN(ChatModelTests)
#include "tst_chat_model.moc"
