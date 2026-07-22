#pragma once

#include <QDateTime>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>

struct ChatEmoteRange {
  QString id;
  int start = 0;
  int end = 0;
};

struct ChatEvent {
  enum Type { Message, Notice, ClearMessage, ClearChat, Ping, Reconnect, Unknown };

  Type type = Unknown;
  QString id;
  QString channel;
  QString authorLogin;
  QString displayName;
  QString color;
  QString body;
  QString targetMessageId;
  QString targetUserLogin;
  QString replyParentId;
  QString replyParentBody;
  QString replyParentUser;
  QStringList badges;
  QVector<ChatEmoteRange> emotes;
  bool action = false;
  bool deleted = false;
  int timeoutSeconds = 0;
  QDateTime timestamp;
};

class IrcParser {
public:
  [[nodiscard]] static std::optional<ChatEvent> parseLine(const QString &line);
  [[nodiscard]] static QString stripIrcEscapes(QString value);
  [[nodiscard]] static bool mentionsUser(const QString &text, const QString &login, const QString &displayName,
                                         const QString &authorLogin = {});

private:
  [[nodiscard]] static QMap<QString, QString> parseTags(const QString &tags);
  [[nodiscard]] static QVector<ChatEmoteRange> parseEmotes(const QString &raw);
  [[nodiscard]] static QString unescapeTagValue(QString value);
};
