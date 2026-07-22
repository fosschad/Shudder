#include "chat/IrcParser.h"

#include <QRegularExpression>
#include <QTimeZone>

#include <algorithm>

namespace {
QString prefixLogin(const QString &prefix)
{
  const int bang = prefix.indexOf(QLatin1Char('!'));
  if (bang > 0) return prefix.left(bang);
  return prefix;
}

QString displayNameFromTags(const QMap<QString, QString> &tags, const QString &fallback)
{
  const QString display = tags.value(QStringLiteral("display-name"));
  return display.isEmpty() ? fallback : display;
}

QDateTime timestampFromTags(const QMap<QString, QString> &tags)
{
  bool ok = false;
  const qint64 tmiSentTs = tags.value(QStringLiteral("tmi-sent-ts")).toLongLong(&ok);
  if (ok && tmiSentTs > 0) return QDateTime::fromMSecsSinceEpoch(tmiSentTs, QTimeZone::UTC);
  return QDateTime::currentDateTimeUtc();
}
}

std::optional<ChatEvent> IrcParser::parseLine(const QString &rawLine)
{
  QString line = rawLine.trimmed();
  if (line.isEmpty()) return std::nullopt;

  ChatEvent event;
  QMap<QString, QString> tags;
  if (line.startsWith(QLatin1Char('@'))) {
    const int space = line.indexOf(QLatin1Char(' '));
    if (space < 0) return std::nullopt;
    tags = parseTags(line.mid(1, space - 1));
    line = line.mid(space + 1);
  }

  QString prefix;
  if (line.startsWith(QLatin1Char(':'))) {
    const int space = line.indexOf(QLatin1Char(' '));
    if (space < 0) return std::nullopt;
    prefix = line.mid(1, space - 1);
    line = line.mid(space + 1);
  }

  QString trailing;
  const int trailingIndex = line.indexOf(QStringLiteral(" :"));
  if (trailingIndex >= 0) {
    trailing = line.mid(trailingIndex + 2);
    line = line.left(trailingIndex);
  }
  const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
  if (parts.isEmpty()) return std::nullopt;

  const QString command = parts.at(0).toUpper();
  event.channel = parts.size() > 1 ? parts.at(1).mid(parts.at(1).startsWith(QLatin1Char('#')) ? 1 : 0) : QString();
  event.id = tags.value(QStringLiteral("id"));
  event.authorLogin = tags.value(QStringLiteral("login"), prefixLogin(prefix)).toLower();
  event.displayName = displayNameFromTags(tags, event.authorLogin);
  event.color = tags.value(QStringLiteral("color"));
  event.timestamp = timestampFromTags(tags);
  event.replyParentId = tags.value(QStringLiteral("reply-parent-msg-id"));
  event.replyParentBody = tags.value(QStringLiteral("reply-parent-msg-body"));
  event.replyParentUser = tags.value(QStringLiteral("reply-parent-display-name"));

  const QString badges = tags.value(QStringLiteral("badges"));
  for (const QString &badge : badges.split(QLatin1Char(','), Qt::SkipEmptyParts)) event.badges.push_back(badge);
  event.emotes = parseEmotes(tags.value(QStringLiteral("emotes")));

  if (command == QLatin1String("PING")) {
    event.type = ChatEvent::Ping;
    event.body = trailing;
    return event;
  }
  if (command == QLatin1String("RECONNECT")) {
    event.type = ChatEvent::Reconnect;
    return event;
  }
  if (command == QLatin1String("PRIVMSG")) {
    event.type = ChatEvent::Message;
    event.body = stripIrcEscapes(trailing);
    if (event.body.startsWith(QStringLiteral("ACTION "))) {
      event.action = true;
      event.body = event.body.mid(7);
    }
    return event;
  }
  if (command == QLatin1String("USERNOTICE")) {
    event.type = ChatEvent::Notice;
    event.body = trailing.isEmpty() ? tags.value(QStringLiteral("system-msg")) : stripIrcEscapes(trailing);
    return event;
  }
  if (command == QLatin1String("CLEARMSG")) {
    event.type = ChatEvent::ClearMessage;
    event.targetMessageId = tags.value(QStringLiteral("target-msg-id"));
    event.deleted = true;
    return event;
  }
  if (command == QLatin1String("CLEARCHAT")) {
    event.type = ChatEvent::ClearChat;
    event.targetUserLogin = trailing.toLower();
    bool ok = false;
    event.timeoutSeconds = tags.value(QStringLiteral("ban-duration")).toInt(&ok);
    if (!ok) event.timeoutSeconds = 0;
    return event;
  }
  return std::nullopt;
}

bool IrcParser::mentionsUser(const QString &text, const QString &login, const QString &displayName, const QString &authorLogin)
{
  const QString normalizedLogin = login.trimmed().toLower();
  const QString normalizedDisplayName = displayName.trimmed().toLower();
  if (normalizedLogin.isEmpty() && normalizedDisplayName.isEmpty()) return false;
  if (!normalizedLogin.isEmpty() && authorLogin.trimmed().toLower() == normalizedLogin) return false;

  QStringList names;
  if (!normalizedLogin.isEmpty()) names.push_back(QRegularExpression::escape(normalizedLogin));
  if (!normalizedDisplayName.isEmpty() && normalizedDisplayName != normalizedLogin) names.push_back(QRegularExpression::escape(normalizedDisplayName));
  if (names.isEmpty()) return false;

  const QRegularExpression expression(QStringLiteral("(^|[^A-Za-z0-9_@])@(%1)(?=$|[^A-Za-z0-9_])").arg(names.join(QLatin1Char('|'))),
                                      QRegularExpression::CaseInsensitiveOption | QRegularExpression::UseUnicodePropertiesOption);
  return expression.match(text).hasMatch();
}

QString IrcParser::stripIrcEscapes(QString value)
{
  if (value.startsWith(QChar(0x0001))) value.remove(0, 1);
  if (value.endsWith(QChar(0x0001))) value.chop(1);
  return value;
}

QMap<QString, QString> IrcParser::parseTags(const QString &tags)
{
  QMap<QString, QString> result;
  for (const QString &tag : tags.split(QLatin1Char(';'))) {
    const int equals = tag.indexOf(QLatin1Char('='));
    if (equals < 0) result.insert(tag, QString());
    else result.insert(tag.left(equals), unescapeTagValue(tag.mid(equals + 1)));
  }
  return result;
}

QVector<ChatEmoteRange> IrcParser::parseEmotes(const QString &raw)
{
  QVector<ChatEmoteRange> ranges;
  for (const QString &group : raw.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
    const int separator = group.indexOf(QLatin1Char(':'));
    if (separator <= 0 || separator == group.size() - 1) continue;
    const QString id = group.left(separator);
    for (const QString &position : group.mid(separator + 1).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
      const int dash = position.indexOf(QLatin1Char('-'));
      if (dash <= 0 || dash == position.size() - 1) continue;
      bool startOk = false;
      bool endOk = false;
      const int start = position.left(dash).toInt(&startOk);
      const int end = position.mid(dash + 1).toInt(&endOk);
      if (!startOk || !endOk || start < 0 || end < start) continue;
      ranges.push_back(ChatEmoteRange{.id = id, .start = start, .end = end});
    }
  }
  std::sort(ranges.begin(), ranges.end(), [](const ChatEmoteRange &left, const ChatEmoteRange &right) {
    if (left.start == right.start) return left.end < right.end;
    return left.start < right.start;
  });
  return ranges;
}

QString IrcParser::unescapeTagValue(QString value)
{
  value.replace(QStringLiteral("\\s"), QStringLiteral(" "));
  value.replace(QStringLiteral("\\:"), QStringLiteral(";"));
  value.replace(QStringLiteral("\\r"), QStringLiteral("\r"));
  value.replace(QStringLiteral("\\n"), QStringLiteral("\n"));
  value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
  return value;
}
