#include "twitch/TwitchDirectoryModel.h"

#include "shudder_config.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrlQuery>

#include <algorithm>
#include <utility>

namespace {
constexpr auto kBundledPublicClientId = "6kt5h1zfzmdv5vre9ios9qfr3lobmq";

QString configuredClientId()
{
  const QByteArray runtimeClientId = qgetenv("SHUDDER_TWITCH_CLIENT_ID");
  if (!runtimeClientId.isEmpty()) return QString::fromLatin1(runtimeClientId).trimmed();
  const QString configured = QString::fromLatin1(SHUDDER_TWITCH_CLIENT_ID).trimmed();
  return configured.isEmpty() ? QString::fromLatin1(kBundledPublicClientId) : configured;
}
}

TwitchDirectoryModel::TwitchDirectoryModel(QObject *parent) : QAbstractListModel(parent)
{
  m_clientId = configuredClientId();
}

int TwitchDirectoryModel::rowCount(const QModelIndex &parent) const
{
  if (parent.isValid()) return 0;
  return static_cast<int>(m_items.size());
}

QVariant TwitchDirectoryModel::data(const QModelIndex &index, int role) const
{
  if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) return {};
  const Item &item = m_items.at(index.row());
  switch (role) {
  case KindRole: return item.kind;
  case IdRole: return item.id;
  case BroadcasterIdRole: return item.broadcasterId;
  case LoginRole: return item.login;
  case DisplayNameRole: return item.displayName;
  case TitleRole: return item.title;
  case CategoryRole: return item.category;
  case CategoryIdRole: return item.categoryId;
  case ThumbnailRole: return item.thumbnail;
  case AvatarRole: return item.avatar;
  case ViewerCountRole: return item.viewerCount;
  case LanguageRole: return item.language;
  case TagsRole: return item.tags;
  case MatureRole: return item.mature;
  case UptimeRole: return item.uptime;
  case StartedAtRole: return item.startedAt;
  case LiveRole: return item.live;
  default: return {};
  }
}

QHash<int, QByteArray> TwitchDirectoryModel::roleNames() const
{
  return {{KindRole, "kind"},
          {IdRole, "itemId"},
          {BroadcasterIdRole, "broadcasterId"},
          {LoginRole, "login"},
          {DisplayNameRole, "displayName"},
          {TitleRole, "title"},
          {CategoryRole, "category"},
          {CategoryIdRole, "categoryId"},
          {ThumbnailRole, "thumbnail"},
          {AvatarRole, "avatar"},
          {ViewerCountRole, "viewerCount"},
          {LanguageRole, "language"},
          {TagsRole, "tags"},
          {MatureRole, "mature"},
          {UptimeRole, "uptime"},
          {StartedAtRole, "startedAt"},
          {LiveRole, "live"}};
}

bool TwitchDirectoryModel::busy() const { return m_busy; }
int TwitchDirectoryModel::count() const { return static_cast<int>(m_items.size()); }
QString TwitchDirectoryModel::error() const { return m_error; }
QString TwitchDirectoryModel::pageTitle() const { return m_pageTitle; }
bool TwitchDirectoryModel::hasMore() const { return m_hasMore; }
QString TwitchDirectoryModel::clientId() const { return m_clientId; }

QString TwitchDirectoryModel::accessToken() const { return m_accessToken; }

QVariantList TwitchDirectoryModel::searchCategoryItems() const
{
  QVariantList items;
  for (const Item &item : m_items) {
    if (item.kind == QLatin1String("category")) items.push_back(toMap(item));
  }
  return items;
}

QVariantList TwitchDirectoryModel::searchChannelItems() const
{
  QVariantList items;
  for (const Item &item : m_items) {
    if (item.kind != QLatin1String("category")) items.push_back(toMap(item));
  }
  return items;
}

void TwitchDirectoryModel::setClientId(const QString &clientId)
{
  const QString trimmed = clientId.trimmed();
  if (m_clientId == trimmed) return;
  m_clientId = trimmed;
  emit clientIdChanged();
}

void TwitchDirectoryModel::setAccessToken(const QString &accessToken)
{
  const QString trimmed = accessToken.trimmed();
  if (m_accessToken == trimmed) return;
  m_accessToken = trimmed;
  ++m_requestGeneration;
  ++m_searchGeneration;
  ++m_categoryViewerGeneration;
  emit accessTokenChanged();
}

void TwitchDirectoryModel::loadLive()
{
  request(QStringLiteral("/streams?first=24"), tr("Live Channels"), false, false, true);
}

void TwitchDirectoryModel::loadFollowedLive(const QString &userId)
{
  const QString trimmed = userId.trimmed();
  if (trimmed.isEmpty()) {
    clearItems(tr("Followed Channels"));
    return;
  }
  request(QStringLiteral("/streams/followed?first=50&user_id=%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(trimmed))), tr("Followed Channels"));
}

void TwitchDirectoryModel::loadCategories()
{
  request(QStringLiteral("/games/top?first=100"), tr("Popular Categories"), false, false, true);
}

void TwitchDirectoryModel::loadCategoryStreams(const QString &categoryId, const QString &categoryName)
{
  if (categoryId.isEmpty()) return;
  request(QStringLiteral("/streams?first=24&game_id=%1").arg(QString::fromLatin1(QUrl::toPercentEncoding(categoryId))), categoryName);
}

void TwitchDirectoryModel::search(const QString &query)
{
  const QString trimmed = query.trimmed();
  if (trimmed.isEmpty()) {
    loadLive();
    return;
  }
  if (m_clientId.isEmpty()) {
    setError(tr("This Shudder build has no Twitch Client ID. Set SHUDDER_TWITCH_CLIENT_ID at runtime or configure -DSHUDDER_TWITCH_CLIENT_ID for Twitch API browsing."));
    return;
  }
  const QByteArray environmentToken = qgetenv("SHUDDER_TWITCH_ACCESS_TOKEN");
  const QString token = m_accessToken.isEmpty() && !environmentToken.isEmpty()
                            ? QString::fromLatin1(environmentToken)
                            : m_accessToken;
  if (token.isEmpty()) {
    setError(tr("Sign in with Twitch to search channels and categories."));
    return;
  }

  const QString encoded = QString::fromLatin1(QUrl::toPercentEncoding(trimmed));
  ++m_requestGeneration;
  const int generation = ++m_searchGeneration;
  m_searchItems.clear();
  m_searchTitle = tr("Search: %1").arg(trimmed);
  const bool exactLoginQuery = QRegularExpression(QStringLiteral("^[A-Za-z0-9_]{1,25}$")).match(trimmed).hasMatch();
  m_pendingSearchRequests = exactLoginQuery ? 3 : 2;
  m_after.clear();
  m_lastPath.clear();
  setHasMore(false);
  setError({});
  setPageTitle(m_searchTitle);
  setBusy(true);
  beginResetModel();
  m_items.clear();
  endResetModel();

  if (exactLoginQuery) requestSearchPart(QStringLiteral("/streams?first=8&user_login=%1").arg(encoded), token, generation);
  requestSearchPart(QStringLiteral("/search/categories?first=18&query=%1").arg(encoded), token, generation);
  requestSearchPart(QStringLiteral("/search/channels?first=24&query=%1").arg(encoded), token, generation);
}

void TwitchDirectoryModel::refresh()
{
  if (m_lastPath.isEmpty() || m_busy) return;
  request(m_lastPath, m_pageTitle, false, false, true);
}

void TwitchDirectoryModel::loadMore()
{
  if (!m_hasMore || m_after.isEmpty() || m_lastPath.isEmpty() || m_busy) return;
  const QChar separator = m_lastPath.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?');
  request(QStringLiteral("%1%2after=%3").arg(m_lastPath, QString(separator), QString::fromLatin1(QUrl::toPercentEncoding(m_after))), m_pageTitle, true);
}

QVariantMap TwitchDirectoryModel::itemAt(int row) const
{
  if (row < 0 || row >= m_items.size()) return {};
  return toMap(m_items.at(row));
}

QVariantMap TwitchDirectoryModel::itemForLogin(const QString &login) const
{
  const QString normalized = login.trimmed().toLower();
  if (normalized.isEmpty()) return {};
  for (const Item &item : m_items) {
    if (item.login == normalized) return toMap(item);
  }
  return {};
}

void TwitchDirectoryModel::setBusy(bool busy)
{
  if (m_busy == busy) return;
  m_busy = busy;
  emit busyChanged();
}

void TwitchDirectoryModel::setError(QString error)
{
  if (m_error == error) return;
  m_error = std::move(error);
  emit errorChanged();
}

void TwitchDirectoryModel::setPageTitle(QString title)
{
  if (m_pageTitle == title) return;
  m_pageTitle = std::move(title);
  emit pageTitleChanged();
}

void TwitchDirectoryModel::setHasMore(bool hasMore)
{
  if (m_hasMore == hasMore) return;
  m_hasMore = hasMore;
  emit hasMoreChanged();
}

void TwitchDirectoryModel::clearItems(QString title)
{
  ++m_requestGeneration;
  ++m_searchGeneration;
  ++m_categoryViewerGeneration;
  setError({});
  setPageTitle(std::move(title));
  replaceItems({}, {});
}

void TwitchDirectoryModel::replaceItems(QVector<Item> items, QString cursor)
{
  const int oldSize = static_cast<int>(m_items.size());
  QHash<QString, int> categoryViewers;
  QHash<QString, QString> avatars;
  for (const Item &item : std::as_const(m_items)) {
    if (item.kind == QLatin1String("category") && !item.categoryId.isEmpty() && item.viewerCount > 0) categoryViewers.insert(item.categoryId, item.viewerCount);
    const QString avatarKey = !item.broadcasterId.isEmpty() ? item.broadcasterId : item.login;
    if (!avatarKey.isEmpty() && !item.avatar.isEmpty()) avatars.insert(avatarKey, item.avatar);
  }
  for (Item &item : items) {
    if (item.kind == QLatin1String("category") && item.viewerCount <= 0) item.viewerCount = categoryViewers.value(item.categoryId, 0);
    const QString avatarKey = !item.broadcasterId.isEmpty() ? item.broadcasterId : item.login;
    if (item.avatar.isEmpty()) item.avatar = avatars.value(avatarKey);
  }

  beginResetModel();
  m_items = std::move(items);
  endResetModel();
  if (oldSize != static_cast<int>(m_items.size())) emit countChanged();
  emit itemsChanged();
  m_after = std::move(cursor);
  setHasMore(!m_after.isEmpty());
}

void TwitchDirectoryModel::updateItems(QVector<Item> items, QString cursor)
{
  QHash<QString, int> categoryViewers;
  QHash<QString, QString> avatars;
  for (const Item &item : std::as_const(m_items)) {
    if (item.kind == QLatin1String("category") && !item.categoryId.isEmpty() && item.viewerCount > 0) categoryViewers.insert(item.categoryId, item.viewerCount);
    const QString avatarKey = !item.broadcasterId.isEmpty() ? item.broadcasterId : item.login;
    if (!avatarKey.isEmpty() && !item.avatar.isEmpty()) avatars.insert(avatarKey, item.avatar);
  }
  for (Item &item : items) {
    if (item.kind == QLatin1String("category") && item.viewerCount <= 0) item.viewerCount = categoryViewers.value(item.categoryId, 0);
    const QString avatarKey = !item.broadcasterId.isEmpty() ? item.broadcasterId : item.login;
    if (item.avatar.isEmpty()) item.avatar = avatars.value(avatarKey);
  }

  const int oldSize = static_cast<int>(m_items.size());
  const int newSize = static_cast<int>(items.size());
  const int sharedSize = std::min(oldSize, newSize);
  if (sharedSize > 0) {
    for (int i = 0; i < sharedSize; ++i) {
      const QList<int> roles = changedRoles(m_items[i], items[i]);
      m_items[i] = std::move(items[i]);
      if (!roles.isEmpty()) emit dataChanged(index(i), index(i), roles);
    }
  }
  if (newSize > oldSize) {
    beginInsertRows(QModelIndex(), oldSize, newSize - 1);
    for (int i = oldSize; i < newSize; ++i) m_items.push_back(std::move(items[i]));
    endInsertRows();
    emit countChanged();
  } else if (newSize < oldSize) {
    beginRemoveRows(QModelIndex(), newSize, oldSize - 1);
    m_items.erase(m_items.begin() + newSize, m_items.end());
    endRemoveRows();
    emit countChanged();
  }
  if (oldSize != 0 || newSize != 0) {
    emit itemsChanged();
  }
  m_after = std::move(cursor);
  setHasMore(!m_after.isEmpty());
}

void TwitchDirectoryModel::appendItems(QVector<Item> &&items, QString cursor)
{
  if (!items.isEmpty()) {
    const int start = static_cast<int>(m_items.size());
    beginInsertRows(QModelIndex(), start, start + static_cast<int>(items.size()) - 1);
    m_items += items;
    endInsertRows();
    emit countChanged();
    emit itemsChanged();
  }
  m_after = std::move(cursor);
  setHasMore(!m_after.isEmpty());
}

void TwitchDirectoryModel::request(const QString &path, const QString &title, bool append, bool clearBeforeRequest, bool preserveRows)
{
  ++m_searchGeneration;
  m_pendingSearchRequests = 0;
  if (m_clientId.isEmpty()) {
    setError(tr("This Shudder build has no Twitch Client ID. Set SHUDDER_TWITCH_CLIENT_ID at runtime or configure -DSHUDDER_TWITCH_CLIENT_ID for Twitch API browsing."));
    return;
  }
  const QByteArray environmentToken = qgetenv("SHUDDER_TWITCH_ACCESS_TOKEN");
  const QString token = m_accessToken.isEmpty() && !environmentToken.isEmpty()
                            ? QString::fromLatin1(environmentToken)
                            : m_accessToken;
  if (token.isEmpty()) {
    setError(tr("Sign in with Twitch to load live channels, categories, search, and followed channels."));
    return;
  }
  if (!append && m_busy && m_lastPath == path && m_pendingPath == path) return;
  setBusy(true);
  setError({});
  if (!append) {
    const bool currentPath = m_lastPath == path && !m_items.isEmpty();
    if (!currentPath && !m_lastPath.isEmpty() && !m_items.isEmpty()) {
      if (m_pageCache.size() >= 16) m_pageCache.clear();
      m_pageCache.insert(m_lastPath, CachedPage{m_items, m_after});
    }
    m_lastPath = path;
    const auto cached = m_pageCache.constFind(path);
    if (!currentPath && cached != m_pageCache.constEnd()) {
      setPageTitle(title);
      updateItems(cached->items, cached->cursor);
      preserveRows = true;
    } else if (currentPath) {
      setPageTitle(title);
      preserveRows = true;
    } else if (clearBeforeRequest && !m_items.isEmpty()) {
      setPageTitle(title);
      beginResetModel();
      m_items.clear();
      endResetModel();
      emit itemsChanged();
      m_after.clear();
      setHasMore(false);
    } else if (!preserveRows || m_items.isEmpty()) {
      setPageTitle(title);
    }
  }

  QUrl url(QStringLiteral("https://api.twitch.tv/helix%1").arg(path));
  QNetworkRequest networkRequest(url);
  networkRequest.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Shudder/%1").arg(QString::fromLatin1(SHUDDER_VERSION)));
  networkRequest.setTransferTimeout(15000);
  networkRequest.setRawHeader("Client-Id", m_clientId.toUtf8());
  networkRequest.setRawHeader("Authorization", "Bearer " + token.toUtf8());

  const int generation = append ? m_requestGeneration : ++m_requestGeneration;
  m_pendingPath = path;
  QNetworkReply *reply = m_network.get(networkRequest);
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, path, title, append, preserveRows, generation]() {
    handleResponse(reply, path, title, append, preserveRows, generation);
    reply->deleteLater();
  });
}

void TwitchDirectoryModel::requestSearchPart(const QString &path, const QString &token, int generation)
{
  QUrl url(QStringLiteral("https://api.twitch.tv/helix%1").arg(path));
  QNetworkRequest networkRequest(url);
  networkRequest.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Shudder/%1").arg(QString::fromLatin1(SHUDDER_VERSION)));
  networkRequest.setTransferTimeout(15000);
  networkRequest.setRawHeader("Client-Id", m_clientId.toUtf8());
  networkRequest.setRawHeader("Authorization", "Bearer " + token.toUtf8());

  QNetworkReply *reply = m_network.get(networkRequest);
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, path, generation]() {
    if (generation != m_searchGeneration) {
      reply->deleteLater();
      return;
    }
    if (reply->error() == QNetworkReply::NoError) {
      QJsonParseError parseError;
      const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
      if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        m_searchItems += parseItems(path, document.object());
      }
    } else if (m_searchItems.isEmpty()) {
      setError(tr("Twitch search failed: %1").arg(reply->errorString()));
    }

    --m_pendingSearchRequests;
    if (m_pendingSearchRequests <= 0) {
      const auto priority = [](const Item &item) {
        if (item.kind == QLatin1String("stream")) return 0;
        if (item.kind == QLatin1String("category")) return 1;
        if (item.live) return 2;
        return 3;
      };
      std::stable_sort(m_searchItems.begin(), m_searchItems.end(), [priority](const Item &left, const Item &right) {
        const int leftPriority = priority(left);
        const int rightPriority = priority(right);
        if (leftPriority != rightPriority) return leftPriority < rightPriority;
        return QString::localeAwareCompare(left.displayName.isEmpty() ? left.category : left.displayName,
                                           right.displayName.isEmpty() ? right.category : right.displayName) < 0;
      });
      replaceItems(std::move(m_searchItems), {});
      setPageTitle(m_searchTitle);
      requestMissingAvatars();
      setBusy(false);
    }
    reply->deleteLater();
  });
}

void TwitchDirectoryModel::handleResponse(QNetworkReply *reply, const QString &path, const QString &title, bool append, bool preserveRows, int generation)
{
  if (generation != m_requestGeneration) return;
  if (m_pendingPath == path) m_pendingPath.clear();
  setBusy(false);
  if (reply->error() != QNetworkReply::NoError) {
    if (append) {
      setHasMore(false);
      return;
    }
    setError(tr("Twitch request failed: %1").arg(reply->errorString()));
    return;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    setError(tr("Twitch returned invalid JSON."));
    return;
  }
  const QJsonObject object = document.object();
  QVector<Item> parsed = parseItems(path, object);
  const QString cursor = object.value(QStringLiteral("pagination")).toObject().value(QStringLiteral("cursor")).toString();
  setPageTitle(title);
  if (append) appendItems(std::move(parsed), cursor);
  else if (preserveRows) updateItems(std::move(parsed), cursor);
  else replaceItems(std::move(parsed), cursor);
  if (!m_lastPath.isEmpty() && (append || path == m_lastPath)) {
    if (m_pageCache.size() >= 16) m_pageCache.clear();
    m_pageCache.insert(m_lastPath, CachedPage{m_items, m_after});
  }
  if (isCategoryPath(path)) requestCategoryViewerCounts();
  else {
    ++m_categoryViewerGeneration;
    m_pendingCategoryViewerRequests = 0;
    m_pendingCategoryViewerCounts.clear();
  }
  requestMissingAvatars();
}

QString TwitchDirectoryModel::effectiveAccessToken() const
{
  const QByteArray environmentToken = qgetenv("SHUDDER_TWITCH_ACCESS_TOKEN");
  return m_accessToken.isEmpty() && !environmentToken.isEmpty() ? QString::fromLatin1(environmentToken) : m_accessToken;
}

bool TwitchDirectoryModel::isCategoryPath(const QString &path)
{
  return path.startsWith(QStringLiteral("/games/top")) || path.startsWith(QStringLiteral("/search/categories"));
}

void TwitchDirectoryModel::requestCategoryViewerCounts()
{
  const QString token = effectiveAccessToken();
  if (m_clientId.isEmpty() || token.isEmpty()) return;

  QSet<QString> seen;
  QStringList categoryIds;
  for (const Item &item : std::as_const(m_items)) {
    if (item.kind != QLatin1String("category") || item.categoryId.isEmpty() || item.viewerCount > 0) continue;
    if (seen.contains(item.categoryId)) continue;
    seen.insert(item.categoryId);
    categoryIds.push_back(item.categoryId);
  }
  if (categoryIds.isEmpty()) return;

  const int generation = ++m_categoryViewerGeneration;
  m_pendingCategoryViewerRequests = static_cast<int>(categoryIds.size());
  m_pendingCategoryViewerCounts.clear();
  for (const QString &categoryId : categoryIds) {
    QUrl url(QStringLiteral("https://api.twitch.tv/helix/streams"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("first"), QStringLiteral("100"));
    query.addQueryItem(QStringLiteral("game_id"), categoryId);
    url.setQuery(query);

    QNetworkRequest networkRequest(url);
    networkRequest.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Shudder/%1").arg(QString::fromLatin1(SHUDDER_VERSION)));
    networkRequest.setTransferTimeout(15000);
    networkRequest.setRawHeader("Client-Id", m_clientId.toUtf8());
    networkRequest.setRawHeader("Authorization", "Bearer " + token.toUtf8());

    QNetworkReply *reply = m_network.get(networkRequest);
    reply->setParent(this);
    connect(reply, &QNetworkReply::finished, this, [this, reply, categoryId, generation]() {
      if (generation != m_categoryViewerGeneration) {
        reply->deleteLater();
        return;
      }
      int total = 0;
      if (reply->error() == QNetworkReply::NoError) {
        const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
        for (const auto &value : object.value(QStringLiteral("data")).toArray()) {
          total += value.toObject().value(QStringLiteral("viewer_count")).toInt();
        }
      }
      m_pendingCategoryViewerCounts.insert(categoryId, total);
      --m_pendingCategoryViewerRequests;
      reply->deleteLater();
      if (m_pendingCategoryViewerRequests <= 0) applyPendingCategoryViewerCounts(generation);
    });
  }
}

void TwitchDirectoryModel::applyPendingCategoryViewerCounts(int generation)
{
  if (generation != m_categoryViewerGeneration || m_pendingCategoryViewerCounts.isEmpty()) return;
  int firstChanged = -1;
  int lastChanged = -1;
  for (int i = 0; i < m_items.size(); ++i) {
    Item &item = m_items[i];
    if (item.kind != QLatin1String("category") || !m_pendingCategoryViewerCounts.contains(item.categoryId)) continue;
    const int total = m_pendingCategoryViewerCounts.value(item.categoryId);
    if (item.viewerCount == total) continue;
    item.viewerCount = total;
    if (firstChanged < 0) firstChanged = i;
    lastChanged = i;
  }
  m_pendingCategoryViewerCounts.clear();
  if (firstChanged >= 0) {
    emit dataChanged(index(firstChanged), index(lastChanged), {ViewerCountRole});
    emit itemsChanged();
    if (!m_lastPath.isEmpty()) m_pageCache.insert(m_lastPath, CachedPage{m_items, m_after});
  }
}

void TwitchDirectoryModel::requestMissingAvatars()
{
  const QString token = effectiveAccessToken();
  if (m_clientId.isEmpty() || token.isEmpty()) return;

  QStringList ids;
  QSet<QString> seen;
  for (const Item &item : std::as_const(m_items)) {
    if (item.kind != QLatin1String("stream") || !item.avatar.isEmpty() || item.broadcasterId.isEmpty()) continue;
    if (seen.contains(item.broadcasterId)) continue;
    seen.insert(item.broadcasterId);
    ids.push_back(item.broadcasterId);
    if (ids.size() >= 100) break;
  }
  if (ids.isEmpty()) return;

  QUrl url(QStringLiteral("https://api.twitch.tv/helix/users"));
  QUrlQuery query;
  for (const QString &id : ids) query.addQueryItem(QStringLiteral("id"), id);
  url.setQuery(query);

  QNetworkRequest networkRequest(url);
  networkRequest.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Shudder/%1").arg(QString::fromLatin1(SHUDDER_VERSION)));
  networkRequest.setTransferTimeout(15000);
  networkRequest.setRawHeader("Client-Id", m_clientId.toUtf8());
  networkRequest.setRawHeader("Authorization", "Bearer " + token.toUtf8());

  QNetworkReply *reply = m_network.get(networkRequest);
  reply->setParent(this);
  const int generation = m_requestGeneration;
  connect(reply, &QNetworkReply::finished, this, [this, reply, generation]() {
    if (generation != m_requestGeneration) {
      reply->deleteLater();
      return;
    }
    if (reply->error() == QNetworkReply::NoError) {
      const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
      QHash<QString, QString> avatarById;
      for (const auto &value : object.value(QStringLiteral("data")).toArray()) {
        const QJsonObject user = value.toObject();
        avatarById.insert(user.value(QStringLiteral("id")).toString(), user.value(QStringLiteral("profile_image_url")).toString());
      }
      int firstChanged = -1;
      int lastChanged = -1;
      for (int i = 0; i < m_items.size(); ++i) {
        Item &item = m_items[i];
        if (!item.avatar.isEmpty()) continue;
        const QString avatar = avatarById.value(item.broadcasterId);
        if (avatar.isEmpty()) continue;
        item.avatar = avatar;
        if (firstChanged < 0) firstChanged = i;
        lastChanged = i;
      }
      if (firstChanged >= 0) {
        emit dataChanged(index(firstChanged), index(lastChanged), {AvatarRole});
        emit itemsChanged();
      }
    }
    reply->deleteLater();
  });
}

QVector<TwitchDirectoryModel::Item> TwitchDirectoryModel::parseItems(const QString &path, const QJsonObject &object) const
{
  QVector<Item> result;
  const QJsonArray data = object.value(QStringLiteral("data")).toArray();
  const bool categories = isCategoryPath(path);
  const bool searchChannels = path.startsWith(QStringLiteral("/search/channels"));
  for (const auto &value : data) {
    const QJsonObject entry = value.toObject();
    Item item;
    if (categories) {
      item.kind = QStringLiteral("category");
      item.id = entry.value(QStringLiteral("id")).toString();
      item.broadcasterId = item.id;
      item.categoryId = item.id;
      item.category = entry.value(QStringLiteral("name")).toString();
      item.title = item.category;
      item.thumbnail = resizedTwitchImage(entry.value(QStringLiteral("box_art_url")).toString(), 570, 760);
      result.push_back(std::move(item));
      continue;
    }
    if (searchChannels) {
      item.kind = QStringLiteral("channel");
      item.id = entry.value(QStringLiteral("id")).toString();
      item.broadcasterId = item.id;
      item.login = entry.value(QStringLiteral("broadcaster_login")).toString();
      item.displayName = entry.value(QStringLiteral("display_name")).toString();
      item.title = entry.value(QStringLiteral("title")).toString();
      item.category = entry.value(QStringLiteral("game_name")).toString();
      item.avatar = entry.value(QStringLiteral("thumbnail_url")).toString();
      item.thumbnail = item.avatar;
      item.live = entry.value(QStringLiteral("is_live")).toBool();
      result.push_back(std::move(item));
      continue;
    }
    item.kind = QStringLiteral("stream");
    item.id = entry.value(QStringLiteral("id")).toString();
    item.broadcasterId = entry.value(QStringLiteral("user_id")).toString();
    item.login = entry.value(QStringLiteral("user_login")).toString();
    item.displayName = entry.value(QStringLiteral("user_name")).toString();
    item.title = entry.value(QStringLiteral("title")).toString();
    item.category = entry.value(QStringLiteral("game_name")).toString();
    item.categoryId = entry.value(QStringLiteral("game_id")).toString();
    item.thumbnail = resizedTwitchImage(entry.value(QStringLiteral("thumbnail_url")).toString(), 440, 248);
    item.viewerCount = entry.value(QStringLiteral("viewer_count")).toInt();
    item.language = entry.value(QStringLiteral("language")).toString();
    item.mature = entry.value(QStringLiteral("is_mature")).toBool();
    item.startedAt = entry.value(QStringLiteral("started_at")).toString();
    item.uptime = uptimeFromStartedAt(item.startedAt);
    item.live = true;
    const QJsonArray tags = entry.value(QStringLiteral("tags")).toArray();
    for (const auto &tag : tags) item.tags.push_back(tag.toString());
    result.push_back(std::move(item));
  }
  return result;
}

QString TwitchDirectoryModel::resizedTwitchImage(QString url, int width, int height)
{
  url.replace(QStringLiteral("{width}"), QString::number(width));
  url.replace(QStringLiteral("{height}"), QString::number(height));
  url.replace(QRegularExpression(QStringLiteral("-\\d+x\\d+(?=\\.[A-Za-z0-9]+(?:\\?|$))")),
              QStringLiteral("-%1x%2").arg(width).arg(height));
  return url;
}

QList<int> TwitchDirectoryModel::changedRoles(const Item &left, const Item &right)
{
  QList<int> roles;
  if (left.kind != right.kind) roles.push_back(KindRole);
  if (left.id != right.id) roles.push_back(IdRole);
  if (left.broadcasterId != right.broadcasterId) roles.push_back(BroadcasterIdRole);
  if (left.login != right.login) roles.push_back(LoginRole);
  if (left.displayName != right.displayName) roles.push_back(DisplayNameRole);
  if (left.title != right.title) roles.push_back(TitleRole);
  if (left.category != right.category) roles.push_back(CategoryRole);
  if (left.categoryId != right.categoryId) roles.push_back(CategoryIdRole);
  if (left.thumbnail != right.thumbnail) roles.push_back(ThumbnailRole);
  if (left.avatar != right.avatar) roles.push_back(AvatarRole);
  if (left.viewerCount != right.viewerCount) roles.push_back(ViewerCountRole);
  if (left.language != right.language) roles.push_back(LanguageRole);
  if (left.tags != right.tags) roles.push_back(TagsRole);
  if (left.mature != right.mature) roles.push_back(MatureRole);
  if (left.uptime != right.uptime) roles.push_back(UptimeRole);
  if (left.startedAt != right.startedAt) roles.push_back(StartedAtRole);
  if (left.live != right.live) roles.push_back(LiveRole);
  return roles;
}

QString TwitchDirectoryModel::uptimeFromStartedAt(const QString &startedAt)
{
  const QDateTime start = QDateTime::fromString(startedAt, Qt::ISODate);
  if (!start.isValid()) return {};
  const qint64 seconds = start.secsTo(QDateTime::currentDateTimeUtc());
  const qint64 hours = seconds / 3600;
  const qint64 minutes = (seconds % 3600) / 60;
  if (hours > 0) return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
  return QStringLiteral("%1m").arg(qMax<qint64>(0, minutes));
}

QVariantMap TwitchDirectoryModel::toMap(const Item &item) const
{
  return {{QStringLiteral("kind"), item.kind},
          {QStringLiteral("itemId"), item.id},
          {QStringLiteral("broadcasterId"), item.broadcasterId},
          {QStringLiteral("login"), item.login},
          {QStringLiteral("displayName"), item.displayName},
          {QStringLiteral("title"), item.title},
          {QStringLiteral("category"), item.category},
          {QStringLiteral("categoryId"), item.categoryId},
          {QStringLiteral("thumbnail"), item.thumbnail},
          {QStringLiteral("avatar"), item.avatar},
          {QStringLiteral("viewerCount"), item.viewerCount},
          {QStringLiteral("language"), item.language},
          {QStringLiteral("tags"), item.tags},
          {QStringLiteral("mature"), item.mature},
          {QStringLiteral("uptime"), item.uptime},
          {QStringLiteral("startedAt"), item.startedAt},
          {QStringLiteral("live"), item.live}};
}
