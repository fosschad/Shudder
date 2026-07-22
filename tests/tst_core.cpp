#include "chat/IrcParser.h"
#include "core/UrlUtils.h"
#include "core/XdgPaths.h"
#include "playback/StreamlinkResolver.h"
#include "shudder_config.h"
#include "storage/PreferencesService.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

class ShudderCoreTests : public QObject {
  Q_OBJECT

private slots:
  void appIdentityIsShudder();
  void chatParserHandlesActionRepliesAndModeration();
  void preferencesValidateCompatibleValues();
  void preferencesRejectCorruptionSafely();
  void streamlinkQualityParsingIsDeterministic();
  void mentionDetectionIsPrecise();
  void logRedactionRemovesCredentials();
  void navigationWhitelistRejectsLocalFiles();
};

void ShudderCoreTests::appIdentityIsShudder()
{
  QCOMPARE(QStringLiteral(SHUDDER_PRODUCT_NAME), QStringLiteral("Shudder"));
  QCOMPARE(QStringLiteral(SHUDDER_EXECUTABLE_NAME), QStringLiteral("shudder"));
  QVERIFY(QStringLiteral(SHUDDER_APP_ID).startsWith(QStringLiteral("io.github.")));
  QVERIFY(!QStringLiteral(SHUDDER_APP_ID).contains(QStringLiteral("ACTUAL_FORK_OWNER")));
}

void ShudderCoreTests::chatParserHandlesActionRepliesAndModeration()
{
  const QString actionLine = QStringLiteral("@id=abc;badges=broadcaster/1,subscriber/12;color=#ff00ff;display-name=Tester;emotes=25:0-4;reply-parent-msg-id=parent;reply-parent-display-name=Other;reply-parent-msg-body=hello;tmi-sent-ts=1710000000000 :tester!tester@tester.tmi.twitch.tv PRIVMSG #channel :\u0001ACTION Kappa waves\u0001");
  const auto action = IrcParser::parseLine(actionLine);
  QVERIFY(action.has_value());
  QCOMPARE(action->type, ChatEvent::Message);
  QVERIFY(action->action);
  QCOMPARE(action->body, QStringLiteral("Kappa waves"));
  QCOMPARE(action->badges, QStringList({QStringLiteral("broadcaster/1"), QStringLiteral("subscriber/12")}));
  QCOMPARE(action->emotes.size(), 1);
  QCOMPARE(action->emotes.first().id, QStringLiteral("25"));
  QCOMPARE(action->emotes.first().start, 0);
  QCOMPARE(action->emotes.first().end, 4);
  QCOMPARE(action->replyParentId, QStringLiteral("parent"));
  QCOMPARE(action->replyParentUser, QStringLiteral("Other"));

  const auto clear = IrcParser::parseLine(QStringLiteral("@target-msg-id=abc :tmi.twitch.tv CLEARMSG #channel :deleted text"));
  QVERIFY(clear.has_value());
  QCOMPARE(clear->type, ChatEvent::ClearMessage);
  QCOMPARE(clear->targetMessageId, QStringLiteral("abc"));

  const auto ban = IrcParser::parseLine(QStringLiteral("@ban-duration=600 :tmi.twitch.tv CLEARCHAT #channel :baduser"));
  QVERIFY(ban.has_value());
  QCOMPARE(ban->type, ChatEvent::ClearChat);
  QCOMPARE(ban->targetUserLogin, QStringLiteral("baduser"));
  QCOMPARE(ban->timeoutSeconds, 600);
}

void ShudderCoreTests::preferencesValidateCompatibleValues()
{
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  qputenv("XDG_CONFIG_HOME", temp.filePath(QStringLiteral("config")).toUtf8());
  qputenv("XDG_DATA_HOME", temp.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", temp.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("XDG_STATE_HOME", temp.filePath(QStringLiteral("state")).toUtf8());

  PreferencesService preferences(XdgPaths(QStringLiteral("shudder-test")));
  QVERIFY(preferences.load());
  preferences.set(QStringLiteral("sideChatWidth"), 900);
  QCOMPARE(preferences.get(QStringLiteral("sideChatWidth")).toInt(), 620);
  preferences.set(QStringLiteral("playerMode"), QStringLiteral("native"));
  QCOMPARE(preferences.get(QStringLiteral("playerMode")).toString(), QStringLiteral("native"));
  QCOMPARE(preferences.get(QStringLiteral("nativeQuality")).toString(), QString());
  preferences.set(QStringLiteral("nativeQuality"), QStringLiteral("best"));
  QCOMPARE(preferences.get(QStringLiteral("nativeQuality")).toString(), QString());
  preferences.set(QStringLiteral("nativeQuality"), QStringLiteral("720p60"));
  QCOMPARE(preferences.get(QStringLiteral("nativeQuality")).toString(), QStringLiteral("720p60"));
  preferences.set(QStringLiteral("nativeQuality"), QStringLiteral("source"));
  QCOMPARE(preferences.get(QStringLiteral("nativeQuality")).toString(), QString());
  preferences.set(QStringLiteral("nativeQuality"), QStringLiteral("1440p+"));
  QCOMPARE(preferences.get(QStringLiteral("nativeQuality")).toString(), QStringLiteral("1440p60"));
  preferences.set(QStringLiteral("nativeQuality"), QStringLiteral("2160p60"));
  QCOMPARE(preferences.get(QStringLiteral("nativeQuality")).toString(), QStringLiteral("2160p60"));
  preferences.set(QStringLiteral("nativeQuality"), QStringLiteral("worst"));
  QCOMPARE(preferences.get(QStringLiteral("nativeQuality")).toString(), QStringLiteral("2160p60"));
}

void ShudderCoreTests::preferencesRejectCorruptionSafely()
{
  QTemporaryDir temp;
  QVERIFY(temp.isValid());
  qputenv("XDG_CONFIG_HOME", temp.filePath(QStringLiteral("config")).toUtf8());
  qputenv("XDG_DATA_HOME", temp.filePath(QStringLiteral("data")).toUtf8());
  qputenv("XDG_CACHE_HOME", temp.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("XDG_STATE_HOME", temp.filePath(QStringLiteral("state")).toUtf8());

  const XdgPaths paths(QStringLiteral("shudder-corrupt-test"));
  QVERIFY(paths.ensureAll());
  QFile file(paths.preferencesFile());
  QVERIFY(file.open(QIODevice::WriteOnly));
  file.write("{not valid json");
  file.close();

  PreferencesService preferences(paths);
  QVERIFY(!preferences.load());
  QCOMPARE(preferences.get(QStringLiteral("playerMode")).toString(), QStringLiteral("native"));
  QVERIFY(!preferences.values().contains(QStringLiteral("unknownSecret")));
  QVERIFY(!preferences.values().contains(QStringLiteral("twitchClientId")));
}

void ShudderCoreTests::streamlinkQualityParsingIsDeterministic()
{
  const QByteArray payload = R"JSON({"streams":{"720p60":{},"audio_only":{},"best":{},"worst":{},"1080p60":{}}})JSON";
  const QStringList qualities = StreamlinkResolver::parseQualitiesJson(payload);
  QCOMPARE(qualities.first(), QStringLiteral("best"));
  QVERIFY(qualities.contains(QStringLiteral("1080p60")));
  QVERIFY(qualities.contains(QStringLiteral("audio_only")));
  QVERIFY(!qualities.contains(QStringLiteral("worst")));
  QCOMPARE(StreamlinkResolver::streamlinkQualityArgument(QStringLiteral("source")), QStringLiteral("best"));
  QCOMPARE(StreamlinkResolver::streamlinkQualityArgument(QStringLiteral("1440p+")), QStringLiteral("1440p60,1440p"));
  QCOMPARE(StreamlinkResolver::streamlinkQualityArgument(QStringLiteral("2160p")), QStringLiteral("2160p60,2160p"));
  QCOMPARE(StreamlinkResolver::streamlinkQualityArgument(QStringLiteral("1440p")), QStringLiteral("1440p60,1440p"));
  QCOMPARE(StreamlinkResolver::streamlinkQualityArgument(QStringLiteral("1080p")), QStringLiteral("1080p60,1080p"));
  QCOMPARE(StreamlinkResolver::streamlinkQualityArgument(QStringLiteral("720p")), QStringLiteral("720p60,720p"));
  QCOMPARE(StreamlinkResolver::streamlinkQualityArgument(QStringLiteral("worst")), QStringLiteral("best"));
  QVERIFY(StreamlinkResolver::parseQualitiesJson(QByteArrayLiteral("not json")).isEmpty());
  QVERIFY(StreamlinkResolver::parseQualitiesJson(QByteArrayLiteral("[]")).isEmpty());
}

void ShudderCoreTests::mentionDetectionIsPrecise()
{
  const QString login = QStringLiteral("name");
  const QString display = QStringLiteral("DisplayName");
  QVERIFY(IrcParser::mentionsUser(QStringLiteral("@name hello"), login, display));
  QVERIFY(IrcParser::mentionsUser(QStringLiteral("hello @name"), login, display));
  QVERIFY(IrcParser::mentionsUser(QStringLiteral("hello @name!"), login, display));
  QVERIFY(IrcParser::mentionsUser(QStringLiteral("hello (@name)"), login, display));
  QVERIFY(IrcParser::mentionsUser(QStringLiteral("@NAME"), login, display));
  QVERIFY(IrcParser::mentionsUser(QStringLiteral("@name @name"), login, display));
  QVERIFY(IrcParser::mentionsUser(QStringLiteral("/me waves at @name"), login, display));
  QVERIFY(IrcParser::mentionsUser(QStringLiteral("reply text containing @DisplayName"), login, display));
  QVERIFY(!IrcParser::mentionsUser(QStringLiteral("@name2"), login, display));
  QVERIFY(!IrcParser::mentionsUser(QStringLiteral("email@name"), login, display));
  QVERIFY(!IrcParser::mentionsUser(QStringLiteral("ordinary text"), login, display));
  QVERIFY(!IrcParser::mentionsUser(QStringLiteral("@name from myself"), login, display, login));
  QVERIFY(!IrcParser::mentionsUser(QStringLiteral("@name"), QString(), QString()));
}

void ShudderCoreTests::logRedactionRemovesCredentials()
{
  const QString input = QStringLiteral("Authorization=OAuth secret123 https://usher.ttvnw.net/a.m3u8?sig=abc&token=def Bearer token456");
  const QString redacted = UrlUtils::redactedForLog(input);
  QVERIFY(!redacted.contains(QStringLiteral("secret123")));
  QVERIFY(!redacted.contains(QStringLiteral("token456")));
  QVERIFY(!redacted.contains(QStringLiteral("token=def")));
  QVERIFY(redacted.contains(QStringLiteral("[REDACTED]")));
}

void ShudderCoreTests::navigationWhitelistRejectsLocalFiles()
{
  QVERIFY(UrlUtils::isAllowedTwitchActionUrl(QUrl(QStringLiteral("https://www.twitch.tv/subs/example"))));
  QVERIFY(!UrlUtils::isAllowedTwitchActionUrl(QUrl(QStringLiteral("file:///etc/passwd"))));
  QVERIFY(!UrlUtils::isAllowedTwitchActionUrl(QUrl(QStringLiteral("https://evil.example/"))));
}

QTEST_MAIN(ShudderCoreTests)
#include "tst_core.moc"
