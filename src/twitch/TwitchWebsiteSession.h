#pragma once

#include "storage/SecretService.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QUrl>

class QNetworkCookie;
class QWebEngineCookieStore;

class TwitchWebsiteSession : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool linked READ linked NOTIFY sessionChanged)
  Q_PROPERTY(bool linking READ linking NOTIFY linkingChanged)
  Q_PROPERTY(QString login READ login NOTIFY sessionChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QUrl linkUrl READ linkUrl CONSTANT)

public:
  explicit TwitchWebsiteSession(ShudderSecretStore *secretStore, QObject *parent = nullptr);

  [[nodiscard]] bool linked() const;
  [[nodiscard]] bool linking() const;
  [[nodiscard]] QString login() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] QUrl linkUrl() const;
  [[nodiscard]] QString accessToken() const;

  Q_INVOKABLE void beginLink();
  Q_INVOKABLE void cancelLink();
  Q_INVOKABLE void link(const QString &token);
  Q_INVOKABLE void clear();

signals:
  void sessionChanged();
  void linkingChanged();
  void statusChanged();

private:
  ShudderSecretStore *m_secretStore = nullptr;
  QNetworkAccessManager m_network;
  QWebEngineCookieStore *m_cookieStore = nullptr;
  QString m_token;
  QString m_login;
  QString m_status;
  int m_validationGeneration = 0;
  bool m_linking = false;
  bool m_clearBrowserSessionCookies = false;

  void loadStoredSession();
  void validateAndStore(QString token);
  void setLinking(bool linking);
  void handleCookieAdded(const QNetworkCookie &cookie);
  void setStatus(QString status);
};
