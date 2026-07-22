#include "twitch/TwitchWebsiteSession.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QWebEngineCookieStore>
#include <QWebEngineProfile>

#include <utility>

namespace {
constexpr auto kSecretKind = "twitch-website-session";

bool isTwitchAuthTokenCookie(const QNetworkCookie &cookie)
{
  return cookie.name() == QByteArrayLiteral("auth-token") && cookie.domain().contains(QStringLiteral("twitch.tv")) && !cookie.value().isEmpty();
}
}

TwitchWebsiteSession::TwitchWebsiteSession(ShudderSecretStore *secretStore, QObject *parent)
    : QObject(parent), m_secretStore(secretStore)
{
  if (QWebEngineProfile *profile = QWebEngineProfile::defaultProfile()) {
    m_cookieStore = profile->cookieStore();
    connect(m_cookieStore, &QWebEngineCookieStore::cookieAdded, this, &TwitchWebsiteSession::handleCookieAdded);
  }
  loadStoredSession();
}

bool TwitchWebsiteSession::linked() const { return !m_token.isEmpty(); }
bool TwitchWebsiteSession::linking() const { return m_linking; }
QString TwitchWebsiteSession::login() const { return m_login; }
QString TwitchWebsiteSession::status() const { return m_status; }
QUrl TwitchWebsiteSession::linkUrl() const { return QUrl(QStringLiteral("https://www.twitch.tv/login")); }
QString TwitchWebsiteSession::accessToken() const { return m_token; }

void TwitchWebsiteSession::beginLink()
{
  if (!m_cookieStore) {
    setStatus(tr("Twitch website session linking requires Qt WebEngine cookies."));
    return;
  }
  ++m_validationGeneration;
  m_clearBrowserSessionCookies = false;
  setStatus(tr("Sign in to Twitch in the browser window. Shudder will link when Twitch sets the session cookie."));
  setLinking(true);
  m_cookieStore->loadAllCookies();
}

void TwitchWebsiteSession::cancelLink()
{
  if (!m_linking) return;
  ++m_validationGeneration;
  setLinking(false);
  setStatus(tr("Twitch website session linking cancelled."));
}

void TwitchWebsiteSession::link(const QString &token)
{
  QString cleaned = token.trimmed();
  cleaned.remove(QStringLiteral("oauth:"), Qt::CaseInsensitive);
  cleaned.remove(QStringLiteral("OAuth "), Qt::CaseInsensitive);
  cleaned.remove(QStringLiteral("Bearer "), Qt::CaseInsensitive);
  if (cleaned.isEmpty()) {
    setStatus(tr("Paste the Twitch website auth-token cookie value."));
    return;
  }
  validateAndStore(cleaned);
}

void TwitchWebsiteSession::clear()
{
  ++m_validationGeneration;
  setLinking(false);
  m_token.clear();
  m_login.clear();
  if (m_secretStore) m_secretStore->clear(QString::fromLatin1(kSecretKind));
  if (m_cookieStore) {
    m_clearBrowserSessionCookies = true;
    m_cookieStore->loadAllCookies();
  }
  emit sessionChanged();
  setStatus(tr("Twitch website session removed."));
}

void TwitchWebsiteSession::loadStoredSession()
{
  if (!m_secretStore || !m_secretStore->available()) return;
  const QString token = m_secretStore->load(QString::fromLatin1(kSecretKind)).trimmed();
  if (!token.isEmpty()) validateAndStore(token);
}

void TwitchWebsiteSession::validateAndStore(QString token)
{
  const int generation = ++m_validationGeneration;
  setStatus(tr("Validating Twitch website session..."));
  QNetworkRequest request(QUrl(QStringLiteral("https://id.twitch.tv/oauth2/validate")));
  request.setTransferTimeout(15000);
  request.setRawHeader("Authorization", "OAuth " + token.toUtf8());
  QNetworkReply *reply = m_network.get(request);
  reply->setParent(this);
  connect(reply, &QNetworkReply::finished, this, [this, reply, token = std::move(token), generation]() mutable {
    if (generation != m_validationGeneration) {
      reply->deleteLater();
      return;
    }
    if (reply->error() != QNetworkReply::NoError) {
      setStatus(tr("Twitch website session validation failed: %1").arg(reply->errorString()));
      reply->deleteLater();
      return;
    }
    const QJsonObject object = QJsonDocument::fromJson(reply->readAll()).object();
    const QString login = object.value(QStringLiteral("login")).toString();
    if (login.isEmpty()) {
      setStatus(tr("Twitch website session validation returned no login."));
      reply->deleteLater();
      return;
    }
    m_token = token;
    m_login = login;
    if (m_secretStore) m_secretStore->storeAsync(QString::fromLatin1(kSecretKind), m_token);
    setLinking(false);
    emit sessionChanged();
    setStatus(tr("Linked Twitch website session as %1.").arg(login));
    reply->deleteLater();
  });
}

void TwitchWebsiteSession::setLinking(bool linking)
{
  if (m_linking == linking) return;
  m_linking = linking;
  emit linkingChanged();
}

void TwitchWebsiteSession::handleCookieAdded(const QNetworkCookie &cookie)
{
  if (!isTwitchAuthTokenCookie(cookie)) return;
  if (m_clearBrowserSessionCookies && m_cookieStore) {
    m_cookieStore->deleteCookie(cookie);
    return;
  }
  if (!m_linking) return;
  setLinking(false);
  validateAndStore(QString::fromUtf8(cookie.value()).trimmed());
}

void TwitchWebsiteSession::setStatus(QString status)
{
  if (m_status == status) return;
  m_status = std::move(status);
  emit statusChanged();
}
