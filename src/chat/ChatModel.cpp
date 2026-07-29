#include "chat/ChatModel.h"

#include "shudder_config.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QThread>

#include <algorithm>

namespace {
Q_LOGGING_CATEGORY(lcSevenTv, "shudder.chat.7tv", QtWarningMsg)

QString normalizedSevenTvHostUrl(QString baseUrl)
{
  baseUrl = baseUrl.trimmed();
  if (baseUrl.isEmpty()) return {};
  if (baseUrl.startsWith(QStringLiteral("//"))) baseUrl.prepend(QStringLiteral("https:"));
  else if (!baseUrl.contains(QStringLiteral("://"))) baseUrl.prepend(QStringLiteral("https://"));
  QUrl url(baseUrl);
  if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host().isEmpty()) return {};
  url.setQuery({});
  url.setFragment({});
  baseUrl = url.toString(QUrl::FullyEncoded);
  while (baseUrl.endsWith(QLatin1Char('/'))) baseUrl.chop(1);
  return baseUrl;
}

QString diagnosticUrl(QUrl url)
{
  url.setQuery({});
  url.setFragment({});
  return url.toString(QUrl::FullyEncoded);
}

bool isEmoteEdgePunctuation(QChar value)
{
  if (value == QLatin1Char('_')) return false;
  return value.isPunct();
}
}

ChatModel::ChatModel(QObject *parent) : QAbstractListModel(parent)
{
  m_reconnectTimer.setSingleShot(true);
  connect(&m_reconnectTimer, &QTimer::timeout, this, [this]() {
    if (!m_channel.isEmpty()) join(m_channel);
  });
  connectSocketSignals();
}

int ChatModel::rowCount(const QModelIndex &parent) const
{
  if (parent.isValid()) return 0;
  return m_events.size();
}

QVariant ChatModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= m_events.size()) return {};
  const ChatEvent &event = m_events.at(index.row());
  switch (role) {
  case IdRole: return event.id;
  case AuthorRole: return event.authorLogin;
  case DisplayNameRole: return event.displayName;
  case BodyRole: return event.body;
  case ColorRole: return resolvedUserColor(event.authorLogin, event.color);
  case TimestampRole: return event.timestamp.toLocalTime().toString(QStringLiteral("HH:mm"));
  case ActionRole: return event.action;
  case NoticeRole: return event.type == ChatEvent::Notice;
  case DeletedRole: return event.deleted;
  case ReplyParentIdRole: return event.replyParentId;
  case ReplyParentBodyRole: return event.replyParentBody;
  case ReplyParentUserRole: return event.replyParentUser;
  case BadgesRole: return event.badges;
  case BadgeAssetsRole: return badgeAssetsFor(event);
  case MessagePartsRole: return messagePartsFor(event);
  case MentionedRole: return IrcParser::mentionsUser(event.body, m_senderLogin, m_senderDisplayName, event.authorLogin) ||
      IrcParser::mentionsUser(event.replyParentBody, m_senderLogin, m_senderDisplayName, event.authorLogin);
  case PlainTextRole: {
    const QString body = event.deleted ? tr("<message deleted>") : event.body;
    if (event.type == ChatEvent::Notice) return QStringLiteral("%1 %2").arg(event.timestamp.toLocalTime().toString(QStringLiteral("HH:mm")), body);
    const QString name = event.displayName.isEmpty() ? event.authorLogin : event.displayName;
    return event.action ? QStringLiteral("%1 * %2 %3").arg(event.timestamp.toLocalTime().toString(QStringLiteral("HH:mm")), name, body)
                        : QStringLiteral("%1 %2: %3").arg(event.timestamp.toLocalTime().toString(QStringLiteral("HH:mm")), name, body);
  }
  default: return {};
  }
}

QHash<int, QByteArray> ChatModel::roleNames() const
{
  return {{IdRole, "messageId"},
          {AuthorRole, "author"},
          {DisplayNameRole, "displayName"},
          {BodyRole, "body"},
          {ColorRole, "color"},
          {TimestampRole, "timestamp"},
          {ActionRole, "action"},
          {NoticeRole, "notice"},
          {DeletedRole, "deleted"},
          {ReplyParentIdRole, "replyParentId"},
          {ReplyParentBodyRole, "replyParentBody"},
          {ReplyParentUserRole, "replyParentUser"},
          {BadgesRole, "badges"},
          {BadgeAssetsRole, "badgeAssets"},
          {MessagePartsRole, "messageParts"},
          {PlainTextRole, "plainText"},
          {MentionedRole, "mentioned"}};
}

QString ChatModel::channel() const { return m_channel; }

bool ChatModel::connected() const { return m_socket.state() == QAbstractSocket::ConnectedState; }

QString ChatModel::status() const { return m_status; }

int ChatModel::historyLimit() const { return m_historyLimit; }

void ChatModel::setHistoryLimit(int limit)
{
  const int bounded = qBound(20, limit, 1500);
  if (m_historyLimit == bounded) return;
  m_historyLimit = bounded;
  trimHistory();
  emit historyLimitChanged();
}

QString ChatModel::clientId() const { return m_clientId; }

void ChatModel::setClientId(const QString &clientId)
{
  const QString trimmed = clientId.trimmed();
  if (m_clientId == trimmed) return;
  m_clientId = trimmed;
  ++m_requestGeneration;
  emit clientIdChanged();
  requestBadgeAssets();
  requestSenderColor();
}

QString ChatModel::accessToken() const { return m_accessToken; }

void ChatModel::setAccessToken(const QString &accessToken)
{
  const QString trimmed = accessToken.trimmed();
  if (m_accessToken == trimmed) return;
  m_accessToken = trimmed;
  ++m_requestGeneration;
  emit accessTokenChanged();
  requestBadgeAssets();
  requestSenderColor();
}

QVariantList ChatModel::knownUsers() const
{
  QVariantList users;
  QStringList logins = m_knownUsers.keys();
  std::sort(logins.begin(), logins.end(), [this](const QString &left, const QString &right) {
    return QString::localeAwareCompare(m_knownUsers.value(left), m_knownUsers.value(right)) < 0;
  });
  for (const QString &login : logins) {
    QVariantMap user;
    user.insert(QStringLiteral("login"), login);
    user.insert(QStringLiteral("displayName"), m_knownUsers.value(login));
    users.push_back(user);
  }
  return users;
}

QVariantList ChatModel::emotePickerEmotes() const
{
  QVariantList emotes;
  QSet<QString> seen;
  const auto append = [&emotes, &seen](const QVector<EmoteAsset> &source) {
    for (const EmoteAsset &emote : source) {
      const QString key = QStringLiteral("%1:%2").arg(emote.provider, emote.name.toLower());
      if (key.isEmpty() || seen.contains(key)) continue;
      seen.insert(key);
      QVariantMap map;
      map.insert(QStringLiteral("id"), emote.id);
      map.insert(QStringLiteral("name"), emote.name);
      map.insert(QStringLiteral("imageUrl"), emote.imageUrl);
      map.insert(QStringLiteral("provider"), emote.provider);
      map.insert(QStringLiteral("owner"), emote.owner);
      emotes.push_back(map);
    }
  };
  append(m_channelEmotes);
  append(m_globalEmotes);
  append(m_sevenTvChannelEmotes);
  append(m_sevenTvGlobalEmotes);
  append(m_ffzChannelEmotes);
  append(m_ffzGlobalEmotes);
  append(m_bttvChannelEmotes);
  append(m_bttvGlobalEmotes);
  return emotes;
}

QStringList ChatModel::preloadEmoteImageUrls() const
{
  constexpr int preloadLimit = 600;
  QStringList urls;
  QSet<QString> seen;
  const auto append = [&urls, &seen](const QVector<EmoteAsset> &source) {
    for (const EmoteAsset &emote : source) {
      if (urls.size() >= preloadLimit) return;
      if (emote.imageUrl.isEmpty() || seen.contains(emote.imageUrl)) continue;
      seen.insert(emote.imageUrl);
      urls.push_back(emote.imageUrl);
    }
  };
  append(m_sevenTvChannelEmotes);
  append(m_bttvChannelEmotes);
  append(m_ffzChannelEmotes);
  append(m_channelEmotes);
  append(m_sevenTvGlobalEmotes);
  append(m_bttvGlobalEmotes);
  append(m_ffzGlobalEmotes);
  append(m_globalEmotes);
  return urls;
}

void ChatModel::setSender(const QString &userId, const QString &login, const QString &displayName)
{
  const QString normalizedUserId = userId.trimmed();
  const QString normalizedLogin = login.trimmed().toLower();
  const QString normalizedDisplayName = displayName.trimmed();
  if (m_senderUserId == normalizedUserId && m_senderLogin == normalizedLogin && m_senderDisplayName == normalizedDisplayName) return;
  ++m_requestGeneration;
  m_senderUserId = normalizedUserId;
  m_senderLogin = normalizedLogin;
  m_senderDisplayName = normalizedDisplayName;
  m_senderColor = m_userColors.value(m_senderLogin);
  requestBadgeAssets();
  requestSenderColor();
  if (!m_events.isEmpty()) emit dataChanged(index(0), index(m_events.size() - 1), {MentionedRole, MessagePartsRole});
}

void ChatModel::join(const QString &channelLogin)
{
  const QString login = channelLogin.trimmed().toLower();
  join(login, login == m_channel ? m_broadcasterId : QString());
}

void ChatModel::join(const QString &channelLogin, const QString &broadcasterId)
{
  const QString login = channelLogin.trimmed().toLower();
  if (login.isEmpty()) return;
  const QString normalizedBroadcasterId = broadcasterId.trimmed();
  m_shouldReconnect = true;
  m_reconnectTimer.stop();
  if (m_channel != login) {
    ++m_requestGeneration;
    ++m_sevenTvChannelRequestId;
    if (m_sevenTvChannelReply) m_sevenTvChannelReply->abort();
    clear();
    m_channel = login;
    m_broadcasterId = normalizedBroadcasterId;
    const bool hadChannelBadges = !m_channelBadgeAssets.isEmpty();
    m_channelBadgeAssets.clear();
    m_channelEmotes.clear();
    m_sevenTvChannelEmotes.clear();
    m_ffzChannelEmotes.clear();
    m_bttvChannelEmotes.clear();
    m_sevenTvChannelSetId.clear();
    m_sevenTvChannelBroadcasterId.clear();
    m_sevenTvChannelLoaded = false;
    if (hadChannelBadges) notifyBadgeAssetsChanged();
    emit emotePickerEmotesChanged();
    emit channelChanged();
  } else if (!normalizedBroadcasterId.isEmpty() && normalizedBroadcasterId != m_broadcasterId) {
    ++m_requestGeneration;
    ++m_sevenTvChannelRequestId;
    if (m_sevenTvChannelReply) m_sevenTvChannelReply->abort();
    m_broadcasterId = normalizedBroadcasterId;
    m_channelBadgeAssets.clear();
    m_channelEmotes.clear();
    m_sevenTvChannelEmotes.clear();
    m_ffzChannelEmotes.clear();
    m_bttvChannelEmotes.clear();
    m_sevenTvChannelSetId.clear();
    m_sevenTvChannelBroadcasterId.clear();
    m_sevenTvChannelLoaded = false;
    notifyBadgeAssetsChanged();
    emit emotePickerEmotesChanged();
    notifyMessagePartsChanged();
  }
  m_socket.abort();
  setStatus(tr("Connecting to #%1 chat...").arg(login));
  requestBadgeAssets();
  m_socket.open(QUrl(QStringLiteral("wss://irc-ws.chat.twitch.tv:443")));
}

void ChatModel::updateChannelIdentity(const QString &channelLogin, const QString &broadcasterId)
{
  const QString login = channelLogin.trimmed().toLower();
  const QString normalizedBroadcasterId = broadcasterId.trimmed();
  if (login.isEmpty() || login != m_channel || normalizedBroadcasterId.isEmpty() || normalizedBroadcasterId == m_broadcasterId) return;
  ++m_requestGeneration;
  ++m_sevenTvChannelRequestId;
  if (m_sevenTvChannelReply) m_sevenTvChannelReply->abort();
  m_broadcasterId = normalizedBroadcasterId;
  m_channelBadgeAssets.clear();
  m_channelEmotes.clear();
  m_sevenTvChannelEmotes.clear();
  m_ffzChannelEmotes.clear();
  m_bttvChannelEmotes.clear();
  m_sevenTvChannelSetId.clear();
  m_sevenTvChannelBroadcasterId.clear();
  m_sevenTvChannelLoaded = false;
  notifyBadgeAssetsChanged();
  emit emotePickerEmotesChanged();
  notifyMessagePartsChanged();
  requestBadgeAssets();
}

void ChatModel::disconnectChat()
{
  m_shouldReconnect = false;
  m_reconnectTimer.stop();
  m_socket.close();
  setStatus(tr("Chat disconnected."));
}

void ChatModel::refreshEmotePicker()
{
  requestEmoteAssets();
}

void ChatModel::reportEmoteImageStatus(const QString &provider, const QString &emoteId, const QString &imageUrl, const QString &status) const
{
  if (provider != QLatin1String("7TV")) return;
  qCDebug(lcSevenTv).noquote() << "image" << status << "emote" << emoteId << "url" << imageUrl << "cache-key" << imageUrl;
}

bool ChatModel::sendMessage(const QString &body)
{
  const QString text = body.trimmed().left(500);
  if (text.isEmpty()) return false;
  if (m_channel.isEmpty()) {
    setStatus(tr("Open a channel before sending chat."));
    return false;
  }
  if (m_clientId.isEmpty() || m_accessToken.isEmpty() || m_senderUserId.isEmpty()) {
    setStatus(tr("Connect Twitch before sending chat."));
    return false;
  }
  if (m_broadcasterId.isEmpty()) {
    requestChannelIdForSend(text);
    return true;
  }
  postChatMessage(m_channel, m_broadcasterId, text);
  return true;
}

bool ChatModel::sendReply(const QString &body, const QString &parentMessageId)
{
  const QString parentId = parentMessageId.trimmed();
  if (parentId.isEmpty()) {
    setStatus(tr("Choose a message before replying."));
    return false;
  }
  if (!eventById(parentId)) {
    setStatus(tr("The reply target is no longer available."));
    return false;
  }
  const QString text = body.trimmed().left(500);
  if (text.isEmpty()) return false;
  if (m_channel.isEmpty()) {
    setStatus(tr("Open a channel before sending chat."));
    return false;
  }
  if (m_clientId.isEmpty() || m_accessToken.isEmpty() || m_senderUserId.isEmpty()) {
    setStatus(tr("Connect Twitch before sending chat."));
    return false;
  }
  if (m_broadcasterId.isEmpty()) {
    requestChannelIdForSend(text, parentId);
    return true;
  }
  postChatMessage(m_channel, m_broadcasterId, text, parentId);
  return true;
}

void ChatModel::clear()
{
  Q_ASSERT(thread() == QThread::currentThread());
  Q_ASSERT(!m_modelMutationActive);
  m_modelMutationActive = true;
  beginResetModel();
  m_events.clear();
  m_seenIds.clear();
  const bool hadKnownUsers = !m_knownUsers.isEmpty();
  m_knownUsers.clear();
  endResetModel();
  m_modelMutationActive = false;
  if (hadKnownUsers) emit knownUsersChanged();
}

void ChatModel::setStatus(QString status)
{
  if (m_status == status) return;
  m_status = std::move(status);
  emit statusChanged();
}

void ChatModel::insertEvent(ChatEvent event)
{
  Q_ASSERT(thread() == QThread::currentThread());
  Q_ASSERT(!m_modelMutationActive);
  if (!event.id.isEmpty() && m_seenIds.contains(event.id)) {
    const int existingIndex = eventIndexById(event.id);
    if (existingIndex < 0 || !m_events.at(existingIndex).provisional || event.provisional) return;
    event.deleted = event.deleted || m_events.at(existingIndex).deleted;
    rememberUser(event);
    m_events[existingIndex] = std::move(event);
    emit dataChanged(index(existingIndex), index(existingIndex));
    return;
  }
  if (!event.id.isEmpty()) m_seenIds.insert(event.id);
  rememberUser(event);
  m_modelMutationActive = true;
  beginInsertRows(QModelIndex(), m_events.size(), m_events.size());
  m_events.push_back(std::move(event));
  endInsertRows();
  m_modelMutationActive = false;
  trimHistory();
}

void ChatModel::rememberUser(const ChatEvent &event)
{
  if (event.authorLogin.isEmpty() || event.type == ChatEvent::Notice) return;
  const QString login = event.authorLogin.trimmed().toLower();
  const QString displayName = event.displayName.isEmpty() ? event.authorLogin : event.displayName;
  if (m_knownUsers.value(login) != displayName) {
    m_knownUsers.insert(login, displayName);
    emit knownUsersChanged();
  }
  const QString color = normalizedColor(event.color);
  if (!color.isEmpty() && m_userColors.value(login) != color) {
    m_userColors.insert(login, color);
    if (login == m_senderLogin) m_senderColor = color;
    for (int i = 0; i < m_events.size(); ++i) {
      if (m_events.at(i).authorLogin.compare(login, Qt::CaseInsensitive) == 0 && normalizedColor(m_events.at(i).color).isEmpty()) {
        emit dataChanged(index(i), index(i), {ColorRole});
      }
    }
  }
}

void ChatModel::applyModeration(const ChatEvent &event)
{
  if (event.type == ChatEvent::ClearMessage) {
    for (int i = 0; i < m_events.size(); ++i) {
      if (m_events[i].id == event.targetMessageId) {
        m_events[i].deleted = true;
        emit dataChanged(index(i), index(i), {DeletedRole, MessagePartsRole, PlainTextRole});
        return;
      }
    }
  }
  if (event.type == ChatEvent::ClearChat) {
    for (int i = 0; i < m_events.size(); ++i) {
      if (event.targetUserLogin.isEmpty() || m_events[i].authorLogin == event.targetUserLogin) {
        m_events[i].deleted = true;
        emit dataChanged(index(i), index(i), {DeletedRole, MessagePartsRole, PlainTextRole});
      }
    }
  }
}

void ChatModel::trimHistory()
{
  Q_ASSERT(thread() == QThread::currentThread());
  Q_ASSERT(!m_modelMutationActive);
  const int overflow = m_events.size() - m_historyLimit;
  if (overflow <= 0) return;
  Q_ASSERT(overflow <= m_events.size());
  m_modelMutationActive = true;
  beginRemoveRows(QModelIndex(), 0, overflow - 1);
  m_events.erase(m_events.begin(), m_events.begin() + overflow);
  endRemoveRows();
  m_modelMutationActive = false;
  m_seenIds.clear();
  for (const ChatEvent &event : std::as_const(m_events)) {
    if (!event.id.isEmpty()) m_seenIds.insert(event.id);
  }
}

void ChatModel::connectSocketSignals()
{
  connect(&m_socket, &QWebSocket::connected, this, [this]() {
    m_reconnectTimer.stop();
    m_reconnectAttempts = 0;
    emit connectedChanged();
    setStatus(tr("Connected to #%1 chat.").arg(m_channel));
    m_socket.sendTextMessage(QStringLiteral("CAP REQ :twitch.tv/tags twitch.tv/commands"));
    m_socket.sendTextMessage(QStringLiteral("PASS SCHMOOPIIE"));
    m_socket.sendTextMessage(QStringLiteral("NICK justinfan%1").arg(QRandomGenerator::global()->bounded(100000, 999999)));
    m_socket.sendTextMessage(QStringLiteral("JOIN #%1").arg(m_channel));
  });
  connect(&m_socket, &QWebSocket::disconnected, this, [this]() {
    emit connectedChanged();
    if (m_shouldReconnect && !m_channel.isEmpty()) requestReconnect();
  });
  connect(&m_socket, &QWebSocket::textMessageReceived, this, [this](const QString &message) {
    for (const QString &line : message.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts)) {
      auto event = IrcParser::parseLine(line);
      if (!event) continue;
      if (event->type == ChatEvent::Ping) {
        m_socket.sendTextMessage(QStringLiteral("PONG :tmi.twitch.tv"));
      } else if (event->type == ChatEvent::Reconnect) {
        requestReconnect();
      } else if (event->type == ChatEvent::ClearMessage || event->type == ChatEvent::ClearChat) {
        applyModeration(*event);
      } else if (event->type == ChatEvent::Message || event->type == ChatEvent::Notice) {
        insertEvent(std::move(*event));
      }
    }
  });
  connect(&m_socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
    setStatus(tr("Chat connection failed: %1").arg(m_socket.errorString()));
  });
}

void ChatModel::requestReconnect()
{
  if (!m_shouldReconnect || m_channel.isEmpty() || m_reconnectTimer.isActive()) return;
  const int delay = qMin(30000, 1000 * (1 << qMin(m_reconnectAttempts, 5)));
  ++m_reconnectAttempts;
  setStatus(tr("Chat disconnected. Reconnecting in %1s...").arg(delay / 1000));
  m_reconnectTimer.start(delay);
}

void ChatModel::requestBadgeAssets()
{
  if (m_channel.isEmpty()) return;
  const QString channelSnapshot = m_channel;
  const quint64 generation = m_requestGeneration;
  if (!m_sevenTvGlobalLoaded) requestSevenTvGlobalEmotes();
  if (!m_broadcasterId.isEmpty()) requestSevenTvChannelEmotes(m_broadcasterId, channelSnapshot);

  if (m_clientId.isEmpty() || m_accessToken.isEmpty()) return;
  if (m_ffzGlobalEmotes.isEmpty()) requestFfzGlobalEmotes(channelSnapshot, generation);
  if (m_bttvGlobalEmotes.isEmpty()) requestBttvGlobalEmotes(channelSnapshot, generation);
  const quint64 badgeGeneration = ++m_badgeRequestGeneration;
  requestBadges(QStringLiteral("/chat/badges/global"), channelSnapshot, generation, badgeGeneration, false);
  if (m_globalEmotes.isEmpty()) requestEmotes(QStringLiteral("/chat/emotes/global"), tr("Global"), channelSnapshot, generation, false);

  if (!m_broadcasterId.isEmpty()) {
    const QString encodedId = QString::fromLatin1(QUrl::toPercentEncoding(m_broadcasterId));
    requestBadges(QStringLiteral("/chat/badges?broadcaster_id=%1").arg(encodedId), channelSnapshot, generation, badgeGeneration, true);
    requestEmotes(QStringLiteral("/chat/emotes?broadcaster_id=%1").arg(encodedId), tr("Channel"), channelSnapshot, generation, true);
    requestFfzChannelEmotes(m_broadcasterId, channelSnapshot, generation);
    requestBttvChannelEmotes(m_broadcasterId, channelSnapshot, generation);
    return;
  }

  QNetworkRequest request = authenticatedTwitchRequest(QStringLiteral("/users?login=%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(channelSnapshot))));
  QNetworkReply *reply = m_network.get(request);
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, channelSnapshot, generation, badgeGeneration]() {
    if (generation == m_requestGeneration && channelSnapshot == m_channel && reply->error() == QNetworkReply::NoError) {
      const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
      const QJsonArray data = object.value(QStringLiteral("data")).toArray();
      const QString broadcasterId = data.isEmpty() ? QString() : data.first().toObject().value(QStringLiteral("id")).toString();
      if (!broadcasterId.isEmpty()) {
        m_broadcasterId = broadcasterId;
        requestBadges(QStringLiteral("/chat/badges?broadcaster_id=%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(broadcasterId))), channelSnapshot, generation, badgeGeneration, true);
        requestEmotes(QStringLiteral("/chat/emotes?broadcaster_id=%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(broadcasterId))), tr("Channel"), channelSnapshot, generation, true);
        requestSevenTvChannelEmotes(broadcasterId, channelSnapshot);
        requestFfzChannelEmotes(broadcasterId, channelSnapshot, generation);
        requestBttvChannelEmotes(broadcasterId, channelSnapshot, generation);
      }
    }
    reply->deleteLater();
  });
}

void ChatModel::requestBadges(const QString &path, const QString &channelSnapshot, quint64 generation, quint64 badgeGeneration, bool channelScoped)
{
  QNetworkReply *reply = m_network.get(authenticatedTwitchRequest(path));
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, channelSnapshot, generation, badgeGeneration, channelScoped]() {
    const QByteArray payload = reply->readAll();
    if (reply->error() == QNetworkReply::NoError) applyBadgePayload(payload, channelScoped, generation, badgeGeneration, channelSnapshot);
    reply->deleteLater();
  });
}

void ChatModel::requestEmoteAssets()
{
  const QString channelSnapshot = m_channel;
  const quint64 generation = m_requestGeneration;
  requestSevenTvGlobalEmotes(true);
  if (!m_broadcasterId.isEmpty()) {
    requestSevenTvChannelEmotes(m_broadcasterId, channelSnapshot, true);
  }
  if (m_clientId.isEmpty() || m_accessToken.isEmpty()) return;
  requestFfzGlobalEmotes(channelSnapshot, generation);
  requestBttvGlobalEmotes(channelSnapshot, generation);
  requestEmotes(QStringLiteral("/chat/emotes/global"), tr("Global"), channelSnapshot, generation, false);
  if (!m_broadcasterId.isEmpty()) {
    requestEmotes(QStringLiteral("/chat/emotes?broadcaster_id=%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(m_broadcasterId))), tr("Channel"), channelSnapshot, generation, true);
    requestFfzChannelEmotes(m_broadcasterId, channelSnapshot, generation);
    requestBttvChannelEmotes(m_broadcasterId, channelSnapshot, generation);
  } else if (!m_channel.isEmpty()) requestBadgeAssets();
}

void ChatModel::requestEmotes(const QString &path, const QString &owner, const QString &channelSnapshot, quint64 generation, bool channelScoped)
{
  QNetworkReply *reply = m_network.get(authenticatedTwitchRequest(path));
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, owner, channelSnapshot, generation, channelScoped]() {
    if (generation == m_requestGeneration && (channelSnapshot.isEmpty() || channelSnapshot == m_channel) && reply->error() == QNetworkReply::NoError) {
      const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
      QVector<EmoteAsset> parsed;
      for (const QJsonValue &value : object.value(QStringLiteral("data")).toArray()) {
        const QJsonObject item = value.toObject();
        EmoteAsset emote;
        emote.id = item.value(QStringLiteral("id")).toString();
        emote.name = item.value(QStringLiteral("name")).toString();
        const QJsonObject images = item.value(QStringLiteral("images")).toObject();
        emote.imageUrl = images.value(QStringLiteral("url_2x")).toString(images.value(QStringLiteral("url_1x")).toString());
        emote.provider = QStringLiteral("Twitch");
        emote.owner = owner;
        if (!emote.name.isEmpty() && !emote.imageUrl.isEmpty()) parsed.push_back(std::move(emote));
      }
      if (channelScoped) m_channelEmotes = std::move(parsed);
      else m_globalEmotes = std::move(parsed);
      emit emotePickerEmotesChanged();
      notifyMessagePartsChanged();
    }
    reply->deleteLater();
  });
}

void ChatModel::requestSevenTvGlobalEmotes(bool force)
{
  if ((!force && m_sevenTvGlobalLoaded) || m_sevenTvGlobalReply) return;
  const QUrl url = m_sevenTvApiBaseUrl.resolved(QUrl(QStringLiteral("emote-sets/global")));
  const quint64 requestId = ++m_sevenTvGlobalRequestId;
  QNetworkReply *reply = m_network.get(providerRequest(url));
  m_sevenTvGlobalReply = reply;
  reply->setParent(this);
  qCDebug(lcSevenTv).noquote() << "request" << requestId << "global url" << diagnosticUrl(url);
  connect(reply, &QNetworkReply::finished, this, [this, reply, requestId]() {
    if (m_sevenTvGlobalReply == reply) m_sevenTvGlobalReply.clear();
    const QByteArray payload = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qCDebug(lcSevenTv).noquote() << "response" << requestId << "global status" << status << "url" << diagnosticUrl(reply->url())
                                    << "network-error" << (reply->error() == QNetworkReply::NoError ? QStringLiteral("none") : reply->errorString());
    if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
      applySevenTvPayload(payload, false, {}, {}, requestId);
    } else {
      qCWarning(lcSevenTv) << "global request failed with status" << status << "network error" << int(reply->error());
    }
    reply->deleteLater();
  });
}

void ChatModel::requestSevenTvChannelEmotes(const QString &broadcasterId, const QString &channelSnapshot, bool force)
{
  if (broadcasterId.isEmpty()) return;
  if (!force && m_sevenTvChannelLoaded && m_sevenTvChannelBroadcasterId == broadcasterId) return;
  if (m_sevenTvChannelReply && m_sevenTvChannelBroadcasterId == broadcasterId) return;
  if (m_sevenTvChannelReply) m_sevenTvChannelReply->abort();
  m_sevenTvChannelBroadcasterId = broadcasterId;
  const QUrl url = m_sevenTvApiBaseUrl.resolved(QUrl(QStringLiteral("users/twitch/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(broadcasterId)))));
  const quint64 requestId = ++m_sevenTvChannelRequestId;
  QNetworkReply *reply = m_network.get(providerRequest(url));
  m_sevenTvChannelReply = reply;
  reply->setParent(this);
  qCDebug(lcSevenTv).noquote() << "request" << requestId << "channel" << channelSnapshot << "user" << broadcasterId << "url" << diagnosticUrl(url);
  connect(reply, &QNetworkReply::finished, this, [this, reply, channelSnapshot, broadcasterId, requestId]() {
    if (m_sevenTvChannelReply == reply) m_sevenTvChannelReply.clear();
    const QByteArray payload = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    qCDebug(lcSevenTv).noquote() << "response" << requestId << "channel" << channelSnapshot << "user" << broadcasterId
                                    << "status" << status << "url" << diagnosticUrl(reply->url()) << "network-error"
                                    << (reply->error() == QNetworkReply::NoError ? QStringLiteral("none") : reply->errorString());
    if (reply->error() == QNetworkReply::ContentNotFoundError && status == 404) {
      applySevenTvChannelNotFound(channelSnapshot, broadcasterId, requestId);
    } else if (reply->error() == QNetworkReply::NoError && status >= 200 && status < 300) {
      applySevenTvPayload(payload, true, channelSnapshot, broadcasterId, requestId);
    } else {
      qCWarning(lcSevenTv) << "channel request failed with status" << status << "network error" << int(reply->error());
    }
    reply->deleteLater();
  });
}

std::optional<ChatModel::SevenTvSet> ChatModel::parseSevenTvPayload(const QByteArray &payload, bool channelScoped, QString *errorMessage)
{
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    if (errorMessage) *errorMessage = parseError.error != QJsonParseError::NoError ? parseError.errorString() : QStringLiteral("root is not an object");
    return std::nullopt;
  }
  const QJsonObject root = document.object();
  const QJsonValue setValue = channelScoped ? root.value(QStringLiteral("emote_set")) : QJsonValue(root);
  if (!setValue.isObject()) {
    if (errorMessage) *errorMessage = QStringLiteral("emote set is missing");
    return std::nullopt;
  }
  const QJsonObject set = setValue.toObject();
  if (set.value(QStringLiteral("id")).toString().isEmpty()) {
    if (errorMessage) *errorMessage = QStringLiteral("emote set id is missing");
    return std::nullopt;
  }
  if (!set.value(QStringLiteral("emotes")).isArray()) {
    if (errorMessage) *errorMessage = QStringLiteral("emotes is not an array");
    return std::nullopt;
  }

  SevenTvSet result;
  result.id = set.value(QStringLiteral("id")).toString();
  const QString owner = channelScoped ? tr("Channel") : tr("Global");
  const QJsonArray emotes = set.value(QStringLiteral("emotes")).toArray();
  for (const QJsonValue &value : emotes) {
    if (!value.isObject()) continue;
    const QJsonObject item = value.toObject();
    EmoteAsset emote;
    emote.id = item.value(QStringLiteral("id")).toString();
    emote.name = item.value(QStringLiteral("name")).toString();
    emote.imageUrl = sevenTvImageUrl(item.value(QStringLiteral("data")).toObject());
    emote.provider = QStringLiteral("7TV");
    emote.owner = owner;
    if (emote.id.isEmpty() || emote.name.isEmpty() || emote.imageUrl.isEmpty()) continue;
    qCDebug(lcSevenTv).noquote() << "selected emote" << emote.id << "url" << emote.imageUrl << "cache-key" << emote.imageUrl;
    result.emotes.push_back(std::move(emote));
  }
  if (!emotes.isEmpty() && result.emotes.isEmpty()) {
    if (errorMessage) *errorMessage = QStringLiteral("emote set contains no usable assets");
    return std::nullopt;
  }
  return result;
}

bool ChatModel::applySevenTvPayload(const QByteArray &payload, bool channelScoped, const QString &channelSnapshot,
                                    const QString &broadcasterId, quint64 requestId)
{
  if (channelScoped) {
    if (requestId != m_sevenTvChannelRequestId || channelSnapshot != m_channel || broadcasterId != m_broadcasterId) {
      qCDebug(lcSevenTv) << "ignored stale channel response" << requestId << channelSnapshot << broadcasterId;
      return false;
    }
  } else if (requestId != m_sevenTvGlobalRequestId) {
    qCDebug(lcSevenTv) << "ignored stale global response" << requestId;
    return false;
  }

  QString error;
  std::optional<SevenTvSet> parsed = parseSevenTvPayload(payload, channelScoped, &error);
  if (!parsed) {
    qCWarning(lcSevenTv).noquote() << "response parse failed" << (channelScoped ? "channel" : "global") << error;
    return false;
  }
  qCDebug(lcSevenTv) << "parsed" << (channelScoped ? "channel" : "global") << "set" << parsed->id << "emotes" << parsed->emotes.size();
  if (channelScoped) {
    if (m_sevenTvChannelLoaded && m_sevenTvChannelSetId == parsed->id && m_sevenTvChannelEmotes == parsed->emotes) return true;
    m_sevenTvChannelSetId = parsed->id;
    m_sevenTvChannelBroadcasterId = broadcasterId;
    m_sevenTvChannelEmotes = std::move(parsed->emotes);
    m_sevenTvChannelLoaded = true;
  } else {
    if (m_sevenTvGlobalLoaded && m_sevenTvGlobalSetId == parsed->id && m_sevenTvGlobalEmotes == parsed->emotes) return true;
    m_sevenTvGlobalSetId = parsed->id;
    m_sevenTvGlobalEmotes = std::move(parsed->emotes);
    m_sevenTvGlobalLoaded = true;
  }
  emit emotePickerEmotesChanged();
  notifyMessagePartsChanged();
  return true;
}

bool ChatModel::applySevenTvChannelNotFound(const QString &channelSnapshot, const QString &broadcasterId, quint64 requestId)
{
  if (requestId != m_sevenTvChannelRequestId || channelSnapshot != m_channel || broadcasterId != m_broadcasterId) return false;
  const bool changed = !m_sevenTvChannelEmotes.isEmpty();
  m_sevenTvChannelEmotes.clear();
  m_sevenTvChannelSetId.clear();
  m_sevenTvChannelBroadcasterId = broadcasterId;
  m_sevenTvChannelLoaded = true;
  if (changed) {
    emit emotePickerEmotesChanged();
    notifyMessagePartsChanged();
  }
  qCDebug(lcSevenTv) << "channel has no 7TV emote set" << channelSnapshot << broadcasterId;
  return true;
}

QString ChatModel::sevenTvImageUrl(const QJsonObject &data)
{
  const QJsonObject host = data.value(QStringLiteral("host")).toObject();
  const QString baseUrl = normalizedSevenTvHostUrl(host.value(QStringLiteral("url")).toString());
  if (baseUrl.isEmpty()) return {};

  QSet<QString> files;
  for (const QJsonValue &fileValue : host.value(QStringLiteral("files")).toArray()) {
    const QJsonObject file = fileValue.toObject();
    const QString name = file.value(QStringLiteral("name")).toString();
    const QString staticName = file.value(QStringLiteral("static_name")).toString();
    if (!name.isEmpty()) files.insert(name);
    if (!staticName.isEmpty()) files.insert(staticName);
  }

  const QStringList preferredFiles{QStringLiteral("2x_static.webp"), QStringLiteral("1x_static.webp"), QStringLiteral("3x_static.webp"), QStringLiteral("4x_static.webp"),
                                   QStringLiteral("2x.webp"), QStringLiteral("1x.webp"), QStringLiteral("3x.webp"), QStringLiteral("4x.webp"),
                                   QStringLiteral("2x.png"), QStringLiteral("1x.png"), QStringLiteral("3x.png"), QStringLiteral("4x.png"),
                                   QStringLiteral("2x_static.gif"), QStringLiteral("1x_static.gif"), QStringLiteral("3x_static.gif"), QStringLiteral("4x_static.gif"),
                                   QStringLiteral("2x.gif"), QStringLiteral("1x.gif"), QStringLiteral("3x.gif"), QStringLiteral("4x.gif")};
  for (const QString &fileName : preferredFiles) {
    if (files.contains(fileName)) return QStringLiteral("%1/%2").arg(baseUrl, fileName);
  }
  return {};
}

void ChatModel::requestFfzGlobalEmotes(const QString &channelSnapshot, quint64 generation)
{
  QNetworkReply *reply = m_network.get(providerRequest(QUrl(QStringLiteral("https://api.frankerfacez.com/v1/set/global"))));
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, channelSnapshot, generation]() {
    if (generation == m_requestGeneration && (channelSnapshot.isEmpty() || channelSnapshot == m_channel) && reply->error() == QNetworkReply::NoError) {
      const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
      QVector<EmoteAsset> parsed;
      const QJsonObject sets = object.value(QStringLiteral("sets")).toObject();
      for (auto setIt = sets.begin(); setIt != sets.end(); ++setIt) {
        for (const QJsonValue &value : setIt.value().toObject().value(QStringLiteral("emoticons")).toArray()) {
          const QJsonObject item = value.toObject();
          const QJsonObject urls = item.value(QStringLiteral("urls")).toObject();
          QString imageUrl = urls.value(QStringLiteral("2")).toString(urls.value(QStringLiteral("1")).toString());
          if (imageUrl.startsWith(QStringLiteral("//"))) imageUrl.prepend(QStringLiteral("https:"));
          EmoteAsset emote;
          emote.id = QString::number(item.value(QStringLiteral("id")).toInt());
          emote.name = item.value(QStringLiteral("name")).toString();
          emote.imageUrl = imageUrl;
          emote.provider = QStringLiteral("FrankerFaceZ");
          emote.owner = tr("Global");
          if (!emote.name.isEmpty() && !emote.imageUrl.isEmpty()) parsed.push_back(std::move(emote));
        }
      }
      m_ffzGlobalEmotes = std::move(parsed);
      emit emotePickerEmotesChanged();
      notifyMessagePartsChanged();
    }
    reply->deleteLater();
  });
}

void ChatModel::requestFfzChannelEmotes(const QString &broadcasterId, const QString &channelSnapshot, quint64 generation)
{
  QNetworkReply *reply = m_network.get(providerRequest(QUrl(QStringLiteral("https://api.frankerfacez.com/v1/room/id/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(broadcasterId))))));
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, channelSnapshot, generation]() {
    if (generation == m_requestGeneration && channelSnapshot == m_channel && reply->error() == QNetworkReply::NoError) {
      const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
      QVector<EmoteAsset> parsed;
      const QJsonObject sets = object.value(QStringLiteral("sets")).toObject();
      for (auto setIt = sets.begin(); setIt != sets.end(); ++setIt) {
        for (const QJsonValue &value : setIt.value().toObject().value(QStringLiteral("emoticons")).toArray()) {
          const QJsonObject item = value.toObject();
          const QJsonObject urls = item.value(QStringLiteral("urls")).toObject();
          QString imageUrl = urls.value(QStringLiteral("2")).toString(urls.value(QStringLiteral("1")).toString());
          if (imageUrl.startsWith(QStringLiteral("//"))) imageUrl.prepend(QStringLiteral("https:"));
          EmoteAsset emote;
          emote.id = QString::number(item.value(QStringLiteral("id")).toInt());
          emote.name = item.value(QStringLiteral("name")).toString();
          emote.imageUrl = imageUrl;
          emote.provider = QStringLiteral("FrankerFaceZ");
          emote.owner = tr("Channel");
          if (!emote.name.isEmpty() && !emote.imageUrl.isEmpty()) parsed.push_back(std::move(emote));
        }
      }
      m_ffzChannelEmotes = std::move(parsed);
      emit emotePickerEmotesChanged();
      notifyMessagePartsChanged();
    }
    reply->deleteLater();
  });
}

void ChatModel::requestBttvGlobalEmotes(const QString &channelSnapshot, quint64 generation)
{
  QNetworkReply *reply = m_network.get(providerRequest(QUrl(QStringLiteral("https://api.betterttv.net/3/cached/emotes/global"))));
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, channelSnapshot, generation]() {
    if (generation == m_requestGeneration && (channelSnapshot.isEmpty() || channelSnapshot == m_channel) && reply->error() == QNetworkReply::NoError) {
      const QJsonArray items = QJsonDocument::fromJson(reply->readAll()).array();
      QVector<EmoteAsset> parsed;
      for (const QJsonValue &value : items) {
        const QJsonObject item = value.toObject();
        EmoteAsset emote;
        emote.id = item.value(QStringLiteral("id")).toString();
        emote.name = item.value(QStringLiteral("code")).toString();
        emote.imageUrl = QStringLiteral("https://cdn.betterttv.net/emote/%1/2x.png").arg(QString::fromLatin1(QUrl::toPercentEncoding(emote.id)));
        emote.provider = QStringLiteral("BetterTTV");
        emote.owner = tr("Global");
        if (!emote.id.isEmpty() && !emote.name.isEmpty()) parsed.push_back(std::move(emote));
      }
      m_bttvGlobalEmotes = std::move(parsed);
      emit emotePickerEmotesChanged();
      notifyMessagePartsChanged();
    }
    reply->deleteLater();
  });
}

void ChatModel::requestBttvChannelEmotes(const QString &broadcasterId, const QString &channelSnapshot, quint64 generation)
{
  QNetworkReply *reply = m_network.get(providerRequest(QUrl(QStringLiteral("https://api.betterttv.net/3/cached/users/twitch/%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(broadcasterId))))));
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, channelSnapshot, generation]() {
    if (generation == m_requestGeneration && channelSnapshot == m_channel && reply->error() == QNetworkReply::NoError) {
      const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
      QVector<EmoteAsset> parsed;
      const auto appendArray = [&parsed, this](const QJsonArray &items) {
        for (const QJsonValue &value : items) {
          const QJsonObject item = value.toObject();
          EmoteAsset emote;
          emote.id = item.value(QStringLiteral("id")).toString();
          emote.name = item.value(QStringLiteral("code")).toString();
          emote.imageUrl = QStringLiteral("https://cdn.betterttv.net/emote/%1/2x.png").arg(QString::fromLatin1(QUrl::toPercentEncoding(emote.id)));
          emote.provider = QStringLiteral("BetterTTV");
          emote.owner = tr("Channel");
          if (!emote.id.isEmpty() && !emote.name.isEmpty()) parsed.push_back(std::move(emote));
        }
      };
      appendArray(object.value(QStringLiteral("channelEmotes")).toArray());
      appendArray(object.value(QStringLiteral("sharedEmotes")).toArray());
      m_bttvChannelEmotes = std::move(parsed);
      emit emotePickerEmotesChanged();
      notifyMessagePartsChanged();
    }
    reply->deleteLater();
  });
}

void ChatModel::requestChannelIdForSend(const QString &body, const QString &replyParentMessageId)
{
  const QString channelSnapshot = m_channel;
  const quint64 generation = m_requestGeneration;
  QNetworkRequest request = authenticatedTwitchRequest(QStringLiteral("/users?login=%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(channelSnapshot))));
  QNetworkReply *reply = m_network.get(request);
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, channelSnapshot, generation, body, replyParentMessageId]() {
    const QByteArray payload = reply->readAll();
    if (generation == m_requestGeneration && channelSnapshot == m_channel && reply->error() == QNetworkReply::NoError) {
      const QJsonObject object = QJsonDocument::fromJson(payload).object();
      const QJsonArray data = object.value(QStringLiteral("data")).toArray();
      m_broadcasterId = data.isEmpty() ? QString() : data.first().toObject().value(QStringLiteral("id")).toString();
      if (!m_broadcasterId.isEmpty()) postChatMessage(channelSnapshot, m_broadcasterId, body, replyParentMessageId);
      else setStatus(tr("Could not resolve #%1 before sending chat.").arg(channelSnapshot));
    } else if (generation == m_requestGeneration && channelSnapshot == m_channel) {
      const QJsonObject object = QJsonDocument::fromJson(payload).object();
      const QString message = object.value(QStringLiteral("message")).toString(reply->errorString());
      setStatus(tr("Could not resolve chat channel: %1").arg(message));
    }
    reply->deleteLater();
  });
}

void ChatModel::postChatMessage(const QString &channel, const QString &broadcasterId, const QString &body, const QString &replyParentMessageId)
{
  QNetworkRequest request = authenticatedTwitchRequest(QStringLiteral("/chat/messages"));
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

  const QString senderLogin = m_senderLogin;
  const QString senderDisplayName = m_senderDisplayName;
  const QString senderUserId = m_senderUserId;
  const quint64 generation = m_requestGeneration;
  QJsonObject payload{{QStringLiteral("broadcaster_id"), broadcasterId},
                      {QStringLiteral("sender_id"), senderUserId},
                      {QStringLiteral("message"), body}};
  if (!replyParentMessageId.isEmpty()) payload.insert(QStringLiteral("reply_parent_message_id"), replyParentMessageId);
  QNetworkReply *reply = m_network.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, channel, generation, senderUserId, body, senderLogin, senderDisplayName, replyParentMessageId]() {
    const QByteArray payload = reply->readAll();
    const QJsonObject object = QJsonDocument::fromJson(payload).object();
    const bool currentOperation = generation == m_requestGeneration && channel == m_channel && senderUserId == m_senderUserId;
    if (reply->error() != QNetworkReply::NoError) {
      const QString message = object.value(QStringLiteral("message")).toString(reply->errorString());
      if (currentOperation) setStatus(tr("Could not send chat message: %1").arg(message));
      reply->deleteLater();
      return;
    }

    const QJsonArray data = object.value(QStringLiteral("data")).toArray();
    const QJsonObject result = data.isEmpty() ? QJsonObject() : data.first().toObject();
    if (data.isEmpty() || !result.value(QStringLiteral("is_sent")).toBool(false)) {
      const QJsonObject dropReason = result.value(QStringLiteral("drop_reason")).toObject();
      const QString reason = dropReason.value(QStringLiteral("message")).toString(tr("Twitch rejected the message."));
      if (currentOperation) setStatus(tr("Could not send chat message: %1").arg(reason));
      reply->deleteLater();
      return;
    }

    const QString messageId = result.value(QStringLiteral("message_id")).toString().trimmed();
    if (messageId.isEmpty()) {
      if (currentOperation) setStatus(tr("Twitch sent an incomplete chat response."));
      reply->deleteLater();
      return;
    }

    if (currentOperation) {
      insertSentMessage(channel, messageId, body, senderLogin, senderDisplayName, replyParentMessageId);
      emit sendSucceeded();
    }
    reply->deleteLater();
  });
}

void ChatModel::insertSentMessage(const QString &channel, const QString &messageId, const QString &body, const QString &senderLogin, const QString &senderDisplayName,
                                  const QString &replyParentMessageId)
{
  ChatEvent event;
  event.type = ChatEvent::Message;
  if (messageId.isEmpty()) return;
  event.id = messageId;
  event.channel = channel;
  event.authorLogin = senderLogin.isEmpty() ? QStringLiteral("you") : senderLogin;
  event.displayName = senderDisplayName.isEmpty() ? QStringLiteral("You") : senderDisplayName;
  event.body = body;
  if (const ChatEvent *parent = eventById(replyParentMessageId)) {
    event.replyParentId = parent->id;
    event.replyParentUser = parent->displayName.isEmpty() ? parent->authorLogin : parent->displayName;
    event.replyParentBody = parent->deleted ? tr("<message deleted>") : parent->body.left(180);
  }
  event.color = normalizedColor(m_senderColor);
  event.provisional = true;
  event.timestamp = QDateTime::currentDateTimeUtc();
  insertEvent(std::move(event));
}

const ChatEvent *ChatModel::eventById(const QString &messageId) const
{
  const int eventIndex = eventIndexById(messageId);
  return eventIndex < 0 ? nullptr : &m_events.at(eventIndex);
}

int ChatModel::eventIndexById(const QString &messageId) const
{
  if (messageId.isEmpty()) return -1;
  for (int i = 0; i < m_events.size(); ++i) {
    if (m_events.at(i).id == messageId) return i;
  }
  return -1;
}

QString ChatModel::normalizedColor(const QString &color)
{
  const QColor parsed(color.trimmed());
  if (!parsed.isValid() || parsed.alpha() == 0) return {};
  return parsed.name(QColor::HexRgb).toLower();
}

QString ChatModel::fallbackUserColor(const QString &login)
{
  static const QStringList palette{QStringLiteral("#ff6b6b"), QStringLiteral("#f59f00"), QStringLiteral("#94d82d"), QStringLiteral("#20c997"),
                                   QStringLiteral("#22b8cf"), QStringLiteral("#4dabf7"), QStringLiteral("#748ffc"), QStringLiteral("#9775fa"),
                                   QStringLiteral("#da77f2"), QStringLiteral("#f06595")};
  quint32 hash = 2166136261u;
  const QByteArray bytes = login.trimmed().toLower().toUtf8();
  for (const char byte : bytes) {
    hash ^= static_cast<quint8>(byte);
    hash *= 16777619u;
  }
  return palette.at(static_cast<int>(hash % static_cast<quint32>(palette.size())));
}

QString ChatModel::resolvedUserColor(const QString &login, const QString &metadataColor) const
{
  const QString direct = normalizedColor(metadataColor);
  if (!direct.isEmpty()) return direct;
  const QString normalizedLogin = login.trimmed().toLower();
  const QString cached = normalizedColor(m_userColors.value(normalizedLogin));
  if (!cached.isEmpty()) return cached;
  if (normalizedLogin == m_senderLogin) {
    const QString senderColor = normalizedColor(m_senderColor);
    if (!senderColor.isEmpty()) return senderColor;
  }
  return fallbackUserColor(normalizedLogin);
}

bool ChatModel::applyBadgePayload(const QByteArray &payload, bool channelScoped, quint64 generation, quint64 badgeGeneration, const QString &channelSnapshot)
{
  if (generation != m_requestGeneration || badgeGeneration != m_badgeRequestGeneration || channelSnapshot != m_channel) return false;
  const QJsonDocument document = QJsonDocument::fromJson(payload);
  if (!document.isObject() || !document.object().value(QStringLiteral("data")).isArray()) return false;

  QHash<QString, BadgeAsset> parsed;
  for (const QJsonValue &setValue : document.object().value(QStringLiteral("data")).toArray()) {
    const QJsonObject set = setValue.toObject();
    const QString setId = set.value(QStringLiteral("set_id")).toString();
    if (setId.isEmpty()) continue;
    for (const QJsonValue &versionValue : set.value(QStringLiteral("versions")).toArray()) {
      const QJsonObject version = versionValue.toObject();
      const QString id = version.value(QStringLiteral("id")).toString();
      if (id.isEmpty()) continue;
      const QString key = QStringLiteral("%1/%2").arg(setId, id);
      BadgeAsset asset;
      asset.title = version.value(QStringLiteral("title")).toString(key);
      asset.imageUrl = version.value(QStringLiteral("image_url_2x")).toString(version.value(QStringLiteral("image_url_1x")).toString());
      if (!asset.imageUrl.isEmpty()) parsed.insert(key, std::move(asset));
    }
  }

  QHash<QString, BadgeAsset> &target = channelScoped ? m_channelBadgeAssets : m_globalBadgeAssets;
  if (target == parsed) return true;
  target = std::move(parsed);
  notifyBadgeAssetsChanged();
  return true;
}

void ChatModel::requestSenderColor()
{
  if (m_clientId.isEmpty() || m_accessToken.isEmpty() || m_senderUserId.isEmpty() || m_senderLogin.isEmpty()) return;
  const quint64 generation = m_requestGeneration;
  const QString userId = m_senderUserId;
  const QString login = m_senderLogin;
  QNetworkReply *reply = m_network.get(authenticatedTwitchRequest(
      QStringLiteral("/chat/color?user_id=%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(userId)))));
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, generation, userId, login]() {
    if (generation == m_requestGeneration && userId == m_senderUserId && login == m_senderLogin && reply->error() == QNetworkReply::NoError) {
      const QJsonArray data = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("data")).toArray();
      const QString color = data.isEmpty() ? QString() : normalizedColor(data.first().toObject().value(QStringLiteral("color")).toString());
      if (!color.isEmpty() && color != m_senderColor) {
        m_senderColor = color;
        m_userColors.insert(login, color);
        for (int i = 0; i < m_events.size(); ++i) {
          if (m_events.at(i).authorLogin.compare(login, Qt::CaseInsensitive) != 0 || !normalizedColor(m_events.at(i).color).isEmpty()) continue;
          if (m_events.at(i).provisional) m_events[i].color = color;
          emit dataChanged(index(i), index(i), {ColorRole});
        }
      }
    }
    reply->deleteLater();
  });
}

void ChatModel::notifyBadgeAssetsChanged()
{
  if (m_events.isEmpty()) return;
  emit dataChanged(index(0), index(m_events.size() - 1), {BadgeAssetsRole});
}

void ChatModel::notifyMessagePartsChanged()
{
  if (m_events.isEmpty()) return;
  emit dataChanged(index(0), index(m_events.size() - 1), {MessagePartsRole});
}

QNetworkRequest ChatModel::authenticatedTwitchRequest(const QString &path) const
{
  QNetworkRequest request(QUrl(QStringLiteral("https://api.twitch.tv/helix%1").arg(path)));
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Shudder/%1").arg(QString::fromLatin1(SHUDDER_VERSION)));
  request.setTransferTimeout(15000);
  request.setRawHeader("Client-Id", m_clientId.toUtf8());
  request.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
  return request;
}

QNetworkRequest ChatModel::providerRequest(const QUrl &url) const
{
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Shudder/%1").arg(QString::fromLatin1(SHUDDER_VERSION)));
  request.setTransferTimeout(15000);
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  return request;
}

QVariantList ChatModel::badgeAssetsFor(const ChatEvent &event) const
{
  QVariantList assets;
  for (const QString &badge : event.badges) {
    BadgeAsset asset = m_channelBadgeAssets.value(badge);
    if (asset.imageUrl.isEmpty()) asset = m_globalBadgeAssets.value(badge);
    if (asset.imageUrl.isEmpty()) continue;
    QVariantMap map;
    map.insert(QStringLiteral("key"), badge);
    map.insert(QStringLiteral("title"), asset.title.isEmpty() ? badge : asset.title);
    map.insert(QStringLiteral("imageUrl"), asset.imageUrl);
    assets.push_back(map);
  }
  return assets;
}

const ChatModel::EmoteAsset *ChatModel::emoteAssetForName(const QString &name) const
{
  const auto find = [&name](const QVector<EmoteAsset> &source) -> const EmoteAsset * {
    for (const EmoteAsset &emote : source) {
      if (emote.name == name) return &emote;
    }
    return nullptr;
  };
  if (const EmoteAsset *emote = find(m_channelEmotes)) return emote;
  if (const EmoteAsset *emote = find(m_globalEmotes)) return emote;
  if (const EmoteAsset *emote = find(m_sevenTvChannelEmotes)) return emote;
  if (const EmoteAsset *emote = find(m_sevenTvGlobalEmotes)) return emote;
  if (const EmoteAsset *emote = find(m_ffzChannelEmotes)) return emote;
  if (const EmoteAsset *emote = find(m_ffzGlobalEmotes)) return emote;
  if (const EmoteAsset *emote = find(m_bttvChannelEmotes)) return emote;
  if (const EmoteAsset *emote = find(m_bttvGlobalEmotes)) return emote;
  return nullptr;
}

QVariantList ChatModel::messagePartsFor(const ChatEvent &event) const
{
  QVariantList parts;
  const QString text = event.deleted ? tr("<message deleted>") : event.body;
  if (event.deleted) {
    QVariantMap textPart;
    textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
    textPart.insert(QStringLiteral("text"), text);
    parts.push_back(textPart);
    return parts;
  }
  const auto appendTextParts = [this, &parts](const QString &segment) {
    QString bufferedText;
    const auto flushText = [&parts, &bufferedText]() {
      if (bufferedText.isEmpty()) return;
      QVariantMap textPart;
      textPart.insert(QStringLiteral("type"), QStringLiteral("text"));
      textPart.insert(QStringLiteral("text"), bufferedText);
      parts.push_back(textPart);
      bufferedText.clear();
    };
    static const QRegularExpression tokenExpression(QStringLiteral("\\S+|\\s+"));
    QRegularExpressionMatchIterator matches = tokenExpression.globalMatch(segment);
    while (matches.hasNext()) {
      const QRegularExpressionMatch match = matches.next();
      const QString token = match.captured(0);
      if (!token.trimmed().isEmpty()) {
        const EmoteAsset *emote = emoteAssetForName(token);
        int leading = 0;
        int trailing = token.size();
        if (!emote) {
          while (leading < trailing && isEmoteEdgePunctuation(token.at(leading))) ++leading;
          while (trailing > leading && isEmoteEdgePunctuation(token.at(trailing - 1))) --trailing;
          if (leading < trailing) emote = emoteAssetForName(token.mid(leading, trailing - leading));
        }
        if (emote) {
          if (leading > 0) bufferedText += token.left(leading);
          flushText();
          QVariantMap emotePart;
          emotePart.insert(QStringLiteral("type"), QStringLiteral("emote"));
          emotePart.insert(QStringLiteral("id"), emote->id);
          emotePart.insert(QStringLiteral("name"), emote->name);
          emotePart.insert(QStringLiteral("imageUrl"), emote->imageUrl);
          emotePart.insert(QStringLiteral("provider"), emote->provider);
          parts.push_back(emotePart);
          if (trailing < token.size()) bufferedText += token.mid(trailing);
          continue;
        }
      }
      bufferedText += token;
    }
    flushText();
  };
  int cursor = 0;
  for (const ChatEmoteRange &emote : event.emotes) {
    if (emote.start < cursor || emote.start >= text.size() || emote.end >= text.size()) continue;
    if (emote.start > cursor) appendTextParts(text.mid(cursor, emote.start - cursor));
    const QString name = text.mid(emote.start, emote.end - emote.start + 1);
    QVariantMap emotePart;
    emotePart.insert(QStringLiteral("type"), QStringLiteral("emote"));
    emotePart.insert(QStringLiteral("id"), emote.id);
    emotePart.insert(QStringLiteral("name"), name);
    emotePart.insert(QStringLiteral("imageUrl"), QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/%1/default/dark/2.0").arg(QString::fromLatin1(QUrl::toPercentEncoding(emote.id))));
    emotePart.insert(QStringLiteral("provider"), QStringLiteral("Twitch"));
    parts.push_back(emotePart);
    cursor = emote.end + 1;
  }
  if (cursor < text.size() || parts.isEmpty()) {
    appendTextParts(text.mid(cursor));
  }
  return parts;
}
