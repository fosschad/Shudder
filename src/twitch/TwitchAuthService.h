#pragma once

#include "storage/SecretService.h"

#include <QDateTime>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUrl>

class TwitchAuthService : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString clientId READ clientId WRITE setClientId NOTIFY clientIdChanged)
  Q_PROPERTY(bool canSignIn READ canSignIn NOTIFY canSignInChanged)
  Q_PROPERTY(bool signedIn READ signedIn NOTIFY signedInChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
  Q_PROPERTY(QString userId READ userId NOTIFY accountChanged)
  Q_PROPERTY(QString login READ login NOTIFY accountChanged)
  Q_PROPERTY(QString displayName READ displayName NOTIFY accountChanged)
  Q_PROPERTY(QUrl avatarUrl READ avatarUrl NOTIFY accountChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString deviceUserCode READ deviceUserCode NOTIFY deviceAuthorizationChanged)
  Q_PROPERTY(QUrl deviceVerificationUri READ deviceVerificationUri NOTIFY deviceAuthorizationChanged)
  Q_PROPERTY(QString accessToken READ accessToken NOTIFY accessTokenChanged)

public:
  explicit TwitchAuthService(ShudderSecretStore *secretStore, QObject *parent = nullptr);

  [[nodiscard]] QString clientId() const;
  void setClientId(const QString &clientId);
  [[nodiscard]] bool canSignIn() const;
  [[nodiscard]] bool signedIn() const;
  [[nodiscard]] bool busy() const;
  [[nodiscard]] QString userId() const;
  [[nodiscard]] QString login() const;
  [[nodiscard]] QString displayName() const;
  [[nodiscard]] QUrl avatarUrl() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QString deviceUserCode() const;
  [[nodiscard]] QUrl deviceVerificationUri() const;
  [[nodiscard]] QString accessToken() const;

  Q_INVOKABLE void beginDeviceAuthorization();
  Q_INVOKABLE void cancelDeviceAuthorization();
  Q_INVOKABLE void signOut();
  Q_INVOKABLE void refreshNow();
  Q_INVOKABLE bool isFollowing(const QString &broadcasterId) const;
  Q_INVOKABLE void refreshFollowState(const QString &broadcasterId);

signals:
  void clientIdChanged();
  void canSignInChanged();
  void signedInChanged();
  void busyChanged();
  void accountChanged();
  void statusChanged();
  void deviceAuthorizationChanged();
  void accessTokenChanged();
  void followStateChanged(const QString &broadcasterId);

private:
  struct TokenSet {
    QString accessToken;
    QString refreshToken;
    QStringList scopes;
    QDateTime expiresAt;
  };

  ShudderSecretStore *m_secretStore = nullptr;
  QNetworkAccessManager m_network;
  QTimer m_pollTimer;
  QTimer m_refreshTimer;
  QString m_clientId;
  QString m_userId;
  QString m_login;
  QString m_displayName;
  QUrl m_avatarUrl;
  QString m_status;
  QString m_deviceCode;
  QString m_deviceUserCode;
  QUrl m_deviceVerificationUri;
  QDateTime m_deviceExpiresAt;
  TokenSet m_token;
  QSet<QString> m_followedBroadcasterIds;
  int m_authGeneration = 0;
  bool m_pollInFlight = false;
  bool m_busy = false;

  [[nodiscard]] bool hasToken() const;
  void setBusy(bool busy);
  void setStatus(QString status);
  void clearDeviceAuthorization();
  void pollDeviceToken();
  void validateToken();
  void requestUserProfile();
  void refreshToken();
  void storeTokenAsync();
  void loadStoredToken();
  void applyToken(TokenSet token);
  [[nodiscard]] QNetworkRequest helixRequest(const QUrl &url) const;
  [[nodiscard]] QNetworkRequest oauthRequest(const QUrl &url) const;
  [[nodiscard]] static QStringList scopes();
  [[nodiscard]] static QString formEncode(const QUrlQuery &query);
};
