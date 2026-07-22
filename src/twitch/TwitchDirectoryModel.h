#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSet>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QUrl>

class TwitchDirectoryModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(int count READ count NOTIFY countChanged)
  Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  Q_PROPERTY(QString pageTitle READ pageTitle NOTIFY pageTitleChanged)
  Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
  Q_PROPERTY(QString clientId READ clientId WRITE setClientId NOTIFY clientIdChanged)
  Q_PROPERTY(QString accessToken READ accessToken WRITE setAccessToken NOTIFY accessTokenChanged)
  Q_PROPERTY(QVariantList searchCategoryItems READ searchCategoryItems NOTIFY itemsChanged)
  Q_PROPERTY(QVariantList searchChannelItems READ searchChannelItems NOTIFY itemsChanged)

public:
  enum Roles {
    KindRole = Qt::UserRole + 1,
    IdRole,
    BroadcasterIdRole,
    LoginRole,
    DisplayNameRole,
    TitleRole,
    CategoryRole,
    CategoryIdRole,
    ThumbnailRole,
    AvatarRole,
    ViewerCountRole,
    LanguageRole,
    TagsRole,
    MatureRole,
    UptimeRole,
    StartedAtRole,
    LiveRole,
  };

  explicit TwitchDirectoryModel(QObject *parent = nullptr);

  [[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  [[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
  [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

  [[nodiscard]] bool busy() const;
  [[nodiscard]] int count() const;
  [[nodiscard]] QString error() const;
  [[nodiscard]] QString pageTitle() const;
  [[nodiscard]] bool hasMore() const;
  [[nodiscard]] QString clientId() const;
  void setClientId(const QString &clientId);
  [[nodiscard]] QString accessToken() const;
  void setAccessToken(const QString &accessToken);
  [[nodiscard]] QVariantList searchCategoryItems() const;
  [[nodiscard]] QVariantList searchChannelItems() const;

  Q_INVOKABLE void loadLive();
  Q_INVOKABLE void loadFollowedLive(const QString &userId);
  Q_INVOKABLE void loadCategories();
  Q_INVOKABLE void loadCategoryStreams(const QString &categoryId, const QString &categoryName);
  Q_INVOKABLE void search(const QString &query);
  Q_INVOKABLE void refresh();
  Q_INVOKABLE void loadMore();
  Q_INVOKABLE QVariantMap itemAt(int row) const;
  Q_INVOKABLE QVariantMap itemForLogin(const QString &login) const;

signals:
  void busyChanged();
  void countChanged();
  void errorChanged();
  void pageTitleChanged();
  void hasMoreChanged();
  void clientIdChanged();
  void accessTokenChanged();
  void itemsChanged();

private:
  struct Item {
    QString kind;
    QString id;
    QString broadcasterId;
    QString login;
    QString displayName;
    QString title;
    QString category;
    QString categoryId;
    QString thumbnail;
    QString avatar;
    int viewerCount = 0;
    QString language;
    QStringList tags;
    bool mature = false;
    QString uptime;
    QString startedAt;
    bool live = false;
  };

  struct CachedPage {
    QVector<Item> items;
    QString cursor;
  };

  QVector<Item> m_items;
  QVector<Item> m_searchItems;
  QHash<QString, CachedPage> m_pageCache;
  QNetworkAccessManager m_network;
  QString m_error;
  QString m_pageTitle = tr("Live Channels");
  QString m_clientId;
  QString m_accessToken;
  QString m_after;
  QString m_lastPath;
  QString m_pendingPath;
  QString m_searchTitle;
  int m_requestGeneration = 0;
  int m_searchGeneration = 0;
  int m_pendingSearchRequests = 0;
  int m_categoryViewerGeneration = 0;
  int m_pendingCategoryViewerRequests = 0;
  QHash<QString, int> m_pendingCategoryViewerCounts;
  bool m_busy = false;
  bool m_hasMore = false;

  void setBusy(bool busy);
  void setError(QString error);
  void setPageTitle(QString title);
  void setHasMore(bool hasMore);
  void clearItems(QString title);
  void replaceItems(QVector<Item> items, QString cursor);
  void updateItems(QVector<Item> items, QString cursor);
  void appendItems(QVector<Item> &&items, QString cursor);
  void request(const QString &path, const QString &title, bool append = false, bool clearBeforeRequest = true, bool preserveRows = false);
  void requestSearchPart(const QString &path, const QString &token, int generation);
  void requestMissingAvatars();
  void requestCategoryViewerCounts();
  void applyPendingCategoryViewerCounts(int generation);
  void handleResponse(QNetworkReply *reply, const QString &path, const QString &title, bool append, bool preserveRows, int generation);
  [[nodiscard]] QVector<Item> parseItems(const QString &path, const QJsonObject &object) const;
  [[nodiscard]] static QList<int> changedRoles(const Item &left, const Item &right);
  [[nodiscard]] QString effectiveAccessToken() const;
  [[nodiscard]] static bool isCategoryPath(const QString &path);
  [[nodiscard]] static QString resizedTwitchImage(QString url, int width, int height);
  [[nodiscard]] static QString uptimeFromStartedAt(const QString &startedAt);
  [[nodiscard]] QVariantMap toMap(const Item &item) const;
};
