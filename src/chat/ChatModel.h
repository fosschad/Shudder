#pragma once

#include "chat/IrcParser.h"

#include <QAbstractListModel>
#include <QHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QWebSocket>

#include <optional>

class QJsonObject;
class QNetworkReply;

class ChatModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString channel READ channel NOTIFY channelChanged)
  Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(int historyLimit READ historyLimit WRITE setHistoryLimit NOTIFY historyLimitChanged)
  Q_PROPERTY(QString clientId READ clientId WRITE setClientId NOTIFY clientIdChanged)
  Q_PROPERTY(QString accessToken READ accessToken WRITE setAccessToken NOTIFY accessTokenChanged)
  Q_PROPERTY(QVariantList knownUsers READ knownUsers NOTIFY knownUsersChanged)
  Q_PROPERTY(QVariantList emotePickerEmotes READ emotePickerEmotes NOTIFY emotePickerEmotesChanged)
  Q_PROPERTY(QStringList preloadEmoteImageUrls READ preloadEmoteImageUrls NOTIFY emotePickerEmotesChanged)

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    AuthorRole,
    DisplayNameRole,
    BodyRole,
    ColorRole,
    TimestampRole,
    ActionRole,
    NoticeRole,
    DeletedRole,
    ReplyParentIdRole,
    ReplyParentBodyRole,
    ReplyParentUserRole,
    BadgesRole,
    BadgeAssetsRole,
    MessagePartsRole,
    PlainTextRole,
    MentionedRole,
  };

  explicit ChatModel(QObject *parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] QString channel() const;
  [[nodiscard]] bool connected() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] int historyLimit() const;
  void setHistoryLimit(int limit);
  [[nodiscard]] QString clientId() const;
  void setClientId(const QString &clientId);
  [[nodiscard]] QString accessToken() const;
  void setAccessToken(const QString &accessToken);
  [[nodiscard]] QVariantList knownUsers() const;
  [[nodiscard]] QVariantList emotePickerEmotes() const;
  [[nodiscard]] QStringList preloadEmoteImageUrls() const;
  void setSender(const QString &userId, const QString &login, const QString &displayName);
  void updateChannelIdentity(const QString &channelLogin, const QString &broadcasterId);

  Q_INVOKABLE void join(const QString &channelLogin);
  Q_INVOKABLE void join(const QString &channelLogin, const QString &broadcasterId);
  Q_INVOKABLE void disconnectChat();
  Q_INVOKABLE void refreshEmotePicker();
  Q_INVOKABLE void reportEmoteImageStatus(const QString &provider, const QString &emoteId, const QString &imageUrl, const QString &status) const;
  Q_INVOKABLE bool sendMessage(const QString &body);
  Q_INVOKABLE bool sendReply(const QString &body, const QString &parentMessageId);
  Q_INVOKABLE void clear();

signals:
  void channelChanged();
  void connectedChanged();
  void statusChanged();
  void historyLimitChanged();
  void clientIdChanged();
  void accessTokenChanged();
  void knownUsersChanged();
  void emotePickerEmotesChanged();
  void sendRequested(const QString &channel, const QString &body, const QString &replyParentId);
  void sendSucceeded();

private:
  struct BadgeAsset {
    QString title;
    QString imageUrl;

    bool operator==(const BadgeAsset &) const = default;
  };
  struct EmoteAsset {
    QString id;
    QString name;
    QString imageUrl;
    QString provider;
    QString owner;

    bool operator==(const EmoteAsset &) const = default;
  };
  struct SevenTvSet {
    QString id;
    QVector<EmoteAsset> emotes;
  };

  QVector<ChatEvent> m_events;
  QSet<QString> m_seenIds;
  QHash<QString, QString> m_knownUsers;
  QHash<QString, QString> m_userColors;
  QHash<QString, BadgeAsset> m_globalBadgeAssets;
  QHash<QString, BadgeAsset> m_channelBadgeAssets;
  QVector<EmoteAsset> m_globalEmotes;
  QVector<EmoteAsset> m_channelEmotes;
  QVector<EmoteAsset> m_sevenTvGlobalEmotes;
  QVector<EmoteAsset> m_sevenTvChannelEmotes;
  QVector<EmoteAsset> m_ffzGlobalEmotes;
  QVector<EmoteAsset> m_ffzChannelEmotes;
  QVector<EmoteAsset> m_bttvGlobalEmotes;
  QVector<EmoteAsset> m_bttvChannelEmotes;
  QNetworkAccessManager m_network;
  QUrl m_sevenTvApiBaseUrl{QStringLiteral("https://7tv.io/v3/")};
  QPointer<QNetworkReply> m_sevenTvGlobalReply;
  QPointer<QNetworkReply> m_sevenTvChannelReply;
  QString m_channel;
  QString m_broadcasterId;
  QString m_sevenTvGlobalSetId;
  QString m_sevenTvChannelSetId;
  QString m_sevenTvChannelBroadcasterId;
  QString m_senderUserId;
  QString m_senderLogin;
  QString m_senderDisplayName;
  QString m_senderColor;
  QString m_status;
  QString m_clientId;
  QString m_accessToken;
  int m_historyLimit = 500;
  QWebSocket m_socket;
  QTimer m_reconnectTimer;
  int m_reconnectAttempts = 0;
  quint64 m_requestGeneration = 0;
  quint64 m_badgeRequestGeneration = 0;
  quint64 m_sevenTvGlobalRequestId = 0;
  quint64 m_sevenTvChannelRequestId = 0;
  bool m_sevenTvGlobalLoaded = false;
  bool m_sevenTvChannelLoaded = false;
  bool m_modelMutationActive = false;
  bool m_shouldReconnect = false;

  void setStatus(QString status);
  void insertEvent(ChatEvent event);
  void rememberUser(const ChatEvent &event);
  void applyModeration(const ChatEvent &event);
  void trimHistory();
  void connectSocketSignals();
  void requestReconnect();
  void requestBadgeAssets();
  void requestBadges(const QString &path, const QString &channelSnapshot, quint64 generation, quint64 badgeGeneration, bool channelScoped);
  void requestEmoteAssets();
  void requestEmotes(const QString &path, const QString &owner, const QString &channelSnapshot, quint64 generation, bool channelScoped);
  void requestSevenTvGlobalEmotes(bool force = false);
  void requestSevenTvChannelEmotes(const QString &broadcasterId, const QString &channelSnapshot, bool force = false);
  [[nodiscard]] static std::optional<SevenTvSet> parseSevenTvPayload(const QByteArray &payload, bool channelScoped, QString *errorMessage = nullptr);
  bool applySevenTvPayload(const QByteArray &payload, bool channelScoped, const QString &channelSnapshot,
                           const QString &broadcasterId, quint64 requestId);
  bool applySevenTvChannelNotFound(const QString &channelSnapshot, const QString &broadcasterId, quint64 requestId);
  [[nodiscard]] static QString sevenTvImageUrl(const QJsonObject &data);
  void requestFfzGlobalEmotes(const QString &channelSnapshot, quint64 generation);
  void requestFfzChannelEmotes(const QString &broadcasterId, const QString &channelSnapshot, quint64 generation);
  void requestBttvGlobalEmotes(const QString &channelSnapshot, quint64 generation);
  void requestBttvChannelEmotes(const QString &broadcasterId, const QString &channelSnapshot, quint64 generation);
  void requestSenderColor();
  void requestChannelIdForSend(const QString &body, const QString &replyParentMessageId = {});
  void postChatMessage(const QString &channel, const QString &broadcasterId, const QString &body, const QString &replyParentMessageId = {});
  void insertSentMessage(const QString &channel, const QString &messageId, const QString &body, const QString &senderLogin, const QString &senderDisplayName,
                         const QString &replyParentMessageId = {});
  [[nodiscard]] const ChatEvent *eventById(const QString &messageId) const;
  [[nodiscard]] int eventIndexById(const QString &messageId) const;
  [[nodiscard]] QString resolvedUserColor(const QString &login, const QString &metadataColor = {}) const;
  [[nodiscard]] static QString normalizedColor(const QString &color);
  [[nodiscard]] static QString fallbackUserColor(const QString &login);
  bool applyBadgePayload(const QByteArray &payload, bool channelScoped, quint64 generation, quint64 badgeGeneration, const QString &channelSnapshot);
  void notifyBadgeAssetsChanged();
  void notifyMessagePartsChanged();
  [[nodiscard]] QNetworkRequest authenticatedTwitchRequest(const QString &path) const;
  [[nodiscard]] QNetworkRequest providerRequest(const QUrl &url) const;
  [[nodiscard]] QVariantList badgeAssetsFor(const ChatEvent &event) const;
  [[nodiscard]] QVariantList messagePartsFor(const ChatEvent &event) const;
  [[nodiscard]] const EmoteAsset *emoteAssetForName(const QString &name) const;

  friend class ChatModelTests;
};
