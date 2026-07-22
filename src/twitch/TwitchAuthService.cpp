#include "twitch/TwitchAuthService.h"

#include "core/UrlUtils.h"
#include "shudder_config.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace {
constexpr auto kSecretKind = "twitch-oauth";
constexpr auto kBundledPublicClientId = "6kt5h1zfzmdv5vre9ios9qfr3lobmq";

QString configuredClientId()
{
  const QByteArray environment = qgetenv("SHUDDER_TWITCH_CLIENT_ID");
  if (!environment.isEmpty()) return QString::fromLatin1(environment).trimmed();
  const QString configured = QString::fromLatin1(SHUDDER_TWITCH_CLIENT_ID).trimmed();
  return configured.isEmpty() ? QString::fromLatin1(kBundledPublicClientId) : configured;
}
}

TwitchAuthService::TwitchAuthService(ShudderSecretStore *secretStore, QObject *parent)
    : QObject(parent), m_secretStore(secretStore), m_clientId(configuredClientId())
{
  m_pollTimer.setSingleShot(false);
  connect(&m_pollTimer, &QTimer::timeout, this, &TwitchAuthService::pollDeviceToken);
  m_refreshTimer.setSingleShot(true);
  connect(&m_refreshTimer, &QTimer::timeout, this, &TwitchAuthService::refreshToken);
  loadStoredToken();
  if (hasToken()) validateToken();
}

QString TwitchAuthService::clientId() const { return m_clientId; }

void TwitchAuthService::setClientId(const QString &clientId)
{
  const QString trimmed = clientId.trimmed();
  const bool oldCanSignIn = canSignIn();
  if (m_clientId == trimmed) return;
  m_clientId = trimmed;
  emit clientIdChanged();
  if (oldCanSignIn != canSignIn()) emit canSignInChanged();
}

bool TwitchAuthService::canSignIn() const { return !m_clientId.isEmpty(); }
bool TwitchAuthService::signedIn() const { return hasToken(); }
bool TwitchAuthService::busy() const { return m_busy; }
QString TwitchAuthService::userId() const { return m_userId; }
QString TwitchAuthService::login() const { return m_login; }
QString TwitchAuthService::displayName() const { return m_displayName.isEmpty() ? m_login : m_displayName; }
QUrl TwitchAuthService::avatarUrl() const { return m_avatarUrl; }
QString TwitchAuthService::status() const { return m_status; }
QString TwitchAuthService::deviceUserCode() const { return m_deviceUserCode; }
QUrl TwitchAuthService::deviceVerificationUri() const { return m_deviceVerificationUri; }
QString TwitchAuthService::accessToken() const { return m_token.accessToken; }

void TwitchAuthService::beginDeviceAuthorization()
{
  if (!canSignIn()) {
    setStatus(tr("Enter a public Twitch Client ID to start Device Code sign-in."));
    return;
  }
  if (!m_secretStore || !m_secretStore->available()) {
    setStatus(tr("Secret Service is unavailable or locked, so Shudder cannot save Twitch credentials."));
    return;
  }
  cancelDeviceAuthorization();
  setBusy(true);
  setStatus(tr("Requesting a Twitch device code..."));

  QUrlQuery body;
  body.addQueryItem(QStringLiteral("client_id"), m_clientId);
  body.addQueryItem(QStringLiteral("scopes"), scopes().join(QLatin1Char(' ')));
  QNetworkReply *reply = m_network.post(oauthRequest(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/device"))), formEncode(body).toUtf8());
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply]() {
    setBusy(false);
    const QByteArray payload = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
      setStatus(tr("Twitch Device Code request failed: %1").arg(reply->errorString()));
      reply->deleteLater();
      return;
    }
    const QJsonObject object = QJsonDocument::fromJson(payload).object();
    m_deviceCode = object.value(QStringLiteral("device_code")).toString();
    m_deviceUserCode = object.value(QStringLiteral("user_code")).toString();
    m_deviceVerificationUri = QUrl(object.value(QStringLiteral("verification_uri")).toString());
    const int intervalSeconds = qMax(1, object.value(QStringLiteral("interval")).toInt(5));
    const int expiresSeconds = qMax(1, object.value(QStringLiteral("expires_in")).toInt(600));
    m_deviceExpiresAt = QDateTime::currentDateTimeUtc().addSecs(expiresSeconds);
    if (m_deviceCode.isEmpty() || m_deviceUserCode.isEmpty() || !m_deviceVerificationUri.isValid()) {
      setStatus(tr("Twitch returned an invalid Device Code response."));
      clearDeviceAuthorization();
      reply->deleteLater();
      return;
    }
    emit deviceAuthorizationChanged();
    setStatus(tr("Enter code %1 on Twitch, then keep this window open.").arg(m_deviceUserCode));
    QDesktopServices::openUrl(m_deviceVerificationUri);
    m_pollTimer.start(intervalSeconds * 1000);
    reply->deleteLater();
  });
}

void TwitchAuthService::cancelDeviceAuthorization()
{
  m_pollTimer.stop();
  m_pollInFlight = false;
  clearDeviceAuthorization();
  if (m_busy) setBusy(false);
}

void TwitchAuthService::signOut()
{
  ++m_authGeneration;
  cancelDeviceAuthorization();
  m_refreshTimer.stop();
  m_token = {};
  m_userId.clear();
  m_login.clear();
  m_displayName.clear();
  m_avatarUrl = QUrl();
  m_followedBroadcasterIds.clear();
  if (m_secretStore) m_secretStore->clear(QString::fromLatin1(kSecretKind));
  emit accessTokenChanged();
  emit signedInChanged();
  emit accountChanged();
  setStatus(tr("Signed out of Twitch."));
}

void TwitchAuthService::refreshNow()
{
  if (hasToken()) refreshToken();
  else validateToken();
}

bool TwitchAuthService::isFollowing(const QString &broadcasterId) const
{
  return m_followedBroadcasterIds.contains(broadcasterId.trimmed());
}

void TwitchAuthService::refreshFollowState(const QString &broadcasterId)
{
  const QString channelId = broadcasterId.trimmed();
  if (channelId.isEmpty() || m_userId.isEmpty() || m_clientId.isEmpty() || m_token.accessToken.isEmpty()) return;

  QUrl url(QStringLiteral("https://api.twitch.tv/helix/channels/followed"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("user_id"), m_userId);
  query.addQueryItem(QStringLiteral("broadcaster_id"), channelId);
  url.setQuery(query);

  QNetworkReply *reply = m_network.get(helixRequest(url));
  reply->setParent(this);
  const int generation = m_authGeneration;
  connect(reply, &QNetworkReply::finished, this, [this, reply, channelId, generation]() {
    if (generation != m_authGeneration || m_token.accessToken.isEmpty()) {
      reply->deleteLater();
      return;
    }
    if (reply->error() == QNetworkReply::NoError) {
      const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
      const bool followed = !object.value(QStringLiteral("data")).toArray().isEmpty();
      const bool changed = followed ? !m_followedBroadcasterIds.contains(channelId) : m_followedBroadcasterIds.contains(channelId);
      if (followed) m_followedBroadcasterIds.insert(channelId);
      else m_followedBroadcasterIds.remove(channelId);
      if (changed) emit followStateChanged(channelId);
    }
    reply->deleteLater();
  });
}

bool TwitchAuthService::hasToken() const { return !m_token.accessToken.isEmpty(); }

void TwitchAuthService::setBusy(bool busy)
{
  if (m_busy == busy) return;
  m_busy = busy;
  emit busyChanged();
}

void TwitchAuthService::setStatus(QString status)
{
  if (m_status == status) return;
  m_status = std::move(status);
  emit statusChanged();
}

void TwitchAuthService::clearDeviceAuthorization()
{
  const bool changed = !m_deviceCode.isEmpty() || !m_deviceUserCode.isEmpty() || !m_deviceVerificationUri.isEmpty();
  m_deviceCode.clear();
  m_deviceUserCode.clear();
  m_deviceVerificationUri = QUrl();
  m_deviceExpiresAt = QDateTime();
  if (changed) emit deviceAuthorizationChanged();
}

void TwitchAuthService::pollDeviceToken()
{
  if (m_deviceCode.isEmpty() || m_pollInFlight) return;
  if (QDateTime::currentDateTimeUtc() >= m_deviceExpiresAt) {
    m_pollTimer.stop();
    clearDeviceAuthorization();
    setStatus(tr("The Twitch sign-in code expired. Start sign-in again."));
    return;
  }

  QUrlQuery body;
  body.addQueryItem(QStringLiteral("client_id"), m_clientId);
  body.addQueryItem(QStringLiteral("device_code"), m_deviceCode);
  body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code"));
  const QString requestedDeviceCode = m_deviceCode;
  m_pollInFlight = true;
  QNetworkReply *reply = m_network.post(oauthRequest(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/token"))), formEncode(body).toUtf8());
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, requestedDeviceCode]() {
    m_pollInFlight = false;
    if (requestedDeviceCode != m_deviceCode) {
      reply->deleteLater();
      return;
    }
    const QByteArray payload = reply->readAll();
    const QJsonObject object = QJsonDocument::fromJson(payload).object();
    const QString message = object.value(QStringLiteral("message")).toString();
    const QString error = object.value(QStringLiteral("error")).toString();
    if (reply->error() != QNetworkReply::NoError) {
      if (error == QLatin1String("authorization_pending") ||
          message.contains(QStringLiteral("authorization_pending"), Qt::CaseInsensitive) ||
          message.contains(QStringLiteral("pending authorization"), Qt::CaseInsensitive)) {
        setStatus(tr("Waiting for Twitch authorization..."));
      } else if (error == QLatin1String("slow_down")) {
        m_pollTimer.setInterval(m_pollTimer.interval() + 5000);
        setStatus(tr("Waiting for Twitch authorization..."));
      } else {
        m_pollTimer.stop();
        setStatus(tr("Twitch authorization failed: %1").arg(message.isEmpty() ? reply->errorString() : message));
      }
      reply->deleteLater();
      return;
    }
    TokenSet token;
    token.accessToken = object.value(QStringLiteral("access_token")).toString();
    token.refreshToken = object.value(QStringLiteral("refresh_token")).toString();
    token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(qMax(60, object.value(QStringLiteral("expires_in")).toInt(3600)));
    const QJsonArray scopeArray = object.value(QStringLiteral("scope")).toArray();
    for (const auto &scope : scopeArray) token.scopes.push_back(scope.toString());
    if (token.accessToken.isEmpty() || token.refreshToken.isEmpty()) {
      setStatus(tr("Twitch returned an incomplete token response."));
      reply->deleteLater();
      return;
    }
    m_pollTimer.stop();
    clearDeviceAuthorization();
    applyToken(std::move(token));
    storeTokenAsync();
    validateToken();
    reply->deleteLater();
  });
}

void TwitchAuthService::validateToken()
{
  if (!hasToken()) return;
  QNetworkRequest request(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/validate")));
  request.setTransferTimeout(15000);
  const QString accessToken = m_token.accessToken;
  const int generation = m_authGeneration;
  request.setRawHeader("Authorization", QByteArrayLiteral("OAuth ") + accessToken.toUtf8());
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Shudder/%1").arg(QString::fromLatin1(SHUDDER_VERSION)));
  QNetworkReply *reply = m_network.get(request);
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, accessToken, generation]() {
    if (generation != m_authGeneration || accessToken != m_token.accessToken) {
      reply->deleteLater();
      return;
    }
    const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
    if (reply->error() != QNetworkReply::NoError) {
      if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 401 && !m_token.refreshToken.isEmpty()) refreshToken();
      else setStatus(tr("Could not validate Twitch sign-in: %1").arg(reply->errorString()));
      reply->deleteLater();
      return;
    }
    m_userId = object.value(QStringLiteral("user_id")).toString();
    m_login = object.value(QStringLiteral("login")).toString();
    m_displayName = m_login;
    m_avatarUrl = QUrl();
    emit accountChanged();
    setStatus(tr("Signed in to Twitch as %1.").arg(displayName()));
    requestUserProfile();
    const qint64 seconds = qMax<qint64>(60, QDateTime::currentDateTimeUtc().secsTo(m_token.expiresAt) - 120);
    m_refreshTimer.start(int(qMin<qint64>(seconds, 24 * 3600) * 1000));
    reply->deleteLater();
  });
}

void TwitchAuthService::requestUserProfile()
{
  if (m_clientId.isEmpty() || m_token.accessToken.isEmpty() || m_userId.isEmpty()) return;
  QUrl url(QStringLiteral("https://api.twitch.tv/helix/users"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("id"), m_userId);
  url.setQuery(query);

  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Shudder/%1").arg(QString::fromLatin1(SHUDDER_VERSION)));
  request.setTransferTimeout(15000);
  request.setRawHeader("Client-Id", m_clientId.toUtf8());
  request.setRawHeader("Authorization", "Bearer " + m_token.accessToken.toUtf8());

  QNetworkReply *reply = m_network.get(request);
  reply->setParent(this);
  const int generation = m_authGeneration;
  connect(reply, &QNetworkReply::finished, this, [this, reply, userId = m_userId, generation]() {
    if (generation != m_authGeneration) {
      reply->deleteLater();
      return;
    }
    if (userId == m_userId && reply->error() == QNetworkReply::NoError) {
      const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
      const QJsonArray data = object.value(QStringLiteral("data")).toArray();
      if (!data.isEmpty()) {
        const QJsonObject profile = data.first().toObject();
        const QString displayName = profile.value(QStringLiteral("display_name")).toString(m_login);
        const QUrl avatarUrl(profile.value(QStringLiteral("profile_image_url")).toString());
        if (m_displayName != displayName || m_avatarUrl != avatarUrl) {
          m_displayName = displayName;
          m_avatarUrl = avatarUrl;
          emit accountChanged();
        }
      }
    }
    reply->deleteLater();
  });
}

void TwitchAuthService::refreshToken()
{
  if (m_token.refreshToken.isEmpty() || m_clientId.isEmpty()) return;
  const int generation = m_authGeneration;
  const QString refreshToken = m_token.refreshToken;
  QUrlQuery body;
  body.addQueryItem(QStringLiteral("client_id"), m_clientId);
  body.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("refresh_token"));
  body.addQueryItem(QStringLiteral("refresh_token"), refreshToken);
  QNetworkReply *reply = m_network.post(oauthRequest(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/token"))), formEncode(body).toUtf8());
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, generation, refreshToken]() {
    if (generation != m_authGeneration || refreshToken != m_token.refreshToken) {
      reply->deleteLater();
      return;
    }
    const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
    if (reply->error() != QNetworkReply::NoError) {
      setStatus(tr("Twitch token refresh failed: %1").arg(UrlUtils::redactedForLog(reply->errorString())));
      reply->deleteLater();
      return;
    }
    TokenSet token;
    token.accessToken = object.value(QStringLiteral("access_token")).toString();
    token.refreshToken = object.value(QStringLiteral("refresh_token")).toString(m_token.refreshToken);
    token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(qMax(60, object.value(QStringLiteral("expires_in")).toInt(3600)));
    for (const auto &scope : object.value(QStringLiteral("scope")).toArray()) token.scopes.push_back(scope.toString());
    applyToken(std::move(token));
    storeTokenAsync();
    validateToken();
    reply->deleteLater();
  });
}

void TwitchAuthService::storeTokenAsync()
{
  if (!m_secretStore || !m_secretStore->available() || !hasToken()) return;
  const QJsonObject object{{QStringLiteral("accessToken"), m_token.accessToken},
                           {QStringLiteral("refreshToken"), m_token.refreshToken},
                           {QStringLiteral("expiresAt"), m_token.expiresAt.toString(Qt::ISODateWithMs)},
                           {QStringLiteral("clientId"), m_clientId},
                           {QStringLiteral("scopes"), QJsonArray::fromStringList(m_token.scopes)}};
  m_secretStore->storeAsync(QString::fromLatin1(kSecretKind), QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
}

void TwitchAuthService::loadStoredToken()
{
  if (!m_secretStore || !m_secretStore->available()) return;
  const QString stored = m_secretStore->load(QString::fromLatin1(kSecretKind));
  if (stored.isEmpty()) return;
  const QJsonObject object = QJsonDocument::fromJson(stored.toUtf8()).object();
  TokenSet token;
  token.accessToken = object.value(QStringLiteral("accessToken")).toString();
  token.refreshToken = object.value(QStringLiteral("refreshToken")).toString();
  token.expiresAt = QDateTime::fromString(object.value(QStringLiteral("expiresAt")).toString(), Qt::ISODateWithMs);
  for (const auto &scope : object.value(QStringLiteral("scopes")).toArray()) token.scopes.push_back(scope.toString());
  const QString storedClientId = object.value(QStringLiteral("clientId")).toString();
  if (m_clientId.isEmpty() && !storedClientId.isEmpty()) setClientId(storedClientId);
  if (!token.accessToken.isEmpty()) applyToken(std::move(token));
}

void TwitchAuthService::applyToken(TokenSet token)
{
  const bool wasSignedIn = signedIn();
  ++m_authGeneration;
  m_token = std::move(token);
  emit accessTokenChanged();
  if (wasSignedIn != signedIn()) emit signedInChanged();
}

QNetworkRequest TwitchAuthService::helixRequest(const QUrl &url) const
{
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Shudder/%1").arg(QString::fromLatin1(SHUDDER_VERSION)));
  request.setTransferTimeout(15000);
  request.setRawHeader("Client-Id", m_clientId.toUtf8());
  request.setRawHeader("Authorization", "Bearer " + m_token.accessToken.toUtf8());
  return request;
}

QNetworkRequest TwitchAuthService::oauthRequest(const QUrl &url) const
{
  QNetworkRequest request(url);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
  request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Shudder/%1").arg(QString::fromLatin1(SHUDDER_VERSION)));
  request.setTransferTimeout(15000);
  return request;
}

QStringList TwitchAuthService::scopes()
{
  return {QStringLiteral("user:read:follows"), QStringLiteral("user:read:subscriptions"),
          QStringLiteral("clips:edit"), QStringLiteral("user:read:chat"),
          QStringLiteral("user:write:chat"), QStringLiteral("user:read:emotes")};
}

QString TwitchAuthService::formEncode(const QUrlQuery &query)
{
  return query.toString(QUrl::FullyEncoded);
}
