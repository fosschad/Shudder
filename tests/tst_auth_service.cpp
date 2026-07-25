#include "twitch/TwitchAuthService.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

class AuthServiceTests final : public QObject {
  Q_OBJECT

private slots:
  void staleDeviceCodePayloadCannotRestartAuthorization();
  void deviceCodeRequiresAbsoluteHttpsVerificationUrl();
  void clientChangeCancelsDeviceAuthorization();
  void clientChangeInvalidatesExistingAccount();
  void malformedRefreshPayloadPreservesCurrentToken();
  void authRetriesUseBoundedBackoff();
  void permanentRefreshFailuresAreClassified();
};

void AuthServiceTests::staleDeviceCodePayloadCannotRestartAuthorization()
{
  TwitchAuthService service(nullptr);
  const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("device_code"), QStringLiteral("device")},
                                                       {QStringLiteral("user_code"), QStringLiteral("USER-CODE")},
                                                       {QStringLiteral("verification_uri"), QStringLiteral("https://www.twitch.tv/activate")},
                                                       {QStringLiteral("interval"), 5},
                                                       {QStringLiteral("expires_in"), 600}}).toJson(QJsonDocument::Compact);
  service.m_deviceGeneration = 7;
  QVERIFY(!service.applyDeviceCodePayload(payload, 6, service.clientId()));
  QVERIFY(service.deviceUserCode().isEmpty());
  QVERIFY(!service.m_pollTimer.isActive());

  QVERIFY(service.applyDeviceCodePayload(payload, 7, service.clientId()));
  QCOMPARE(service.deviceUserCode(), QStringLiteral("USER-CODE"));
  QVERIFY(service.m_pollTimer.isActive());
  service.cancelDeviceAuthorization();
}

void AuthServiceTests::deviceCodeRequiresAbsoluteHttpsVerificationUrl()
{
  TwitchAuthService service(nullptr);
  service.m_deviceGeneration = 3;
  const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("device_code"), QStringLiteral("device")},
                                                       {QStringLiteral("user_code"), QStringLiteral("USER-CODE")},
                                                       {QStringLiteral("verification_uri"), QStringLiteral("/missing-host")}}).toJson(QJsonDocument::Compact);
  QVERIFY(!service.applyDeviceCodePayload(payload, 3, service.clientId()));
  QVERIFY(service.deviceUserCode().isEmpty());
  QVERIFY(!service.m_pollTimer.isActive());
}

void AuthServiceTests::clientChangeCancelsDeviceAuthorization()
{
  TwitchAuthService service(nullptr);
  service.m_deviceCode = QStringLiteral("device");
  service.m_deviceUserCode = QStringLiteral("USER-CODE");
  service.m_deviceVerificationUri = QUrl(QStringLiteral("https://www.twitch.tv/activate"));
  service.m_pollTimer.start(5000);
  const quint64 generation = service.m_deviceGeneration;
  service.setClientId(QStringLiteral("replacement-client"));
  QVERIFY(service.m_deviceGeneration > generation);
  QVERIFY(service.deviceUserCode().isEmpty());
  QVERIFY(!service.m_pollTimer.isActive());
}

void AuthServiceTests::clientChangeInvalidatesExistingAccount()
{
  TwitchAuthService service(nullptr);
  service.m_token.accessToken = QStringLiteral("access");
  service.m_token.refreshToken = QStringLiteral("refresh");
  service.m_userId = QStringLiteral("user-id");
  service.m_login = QStringLiteral("login");
  QVERIFY(service.signedIn());

  QSignalSpy signedInChanged(&service, &TwitchAuthService::signedInChanged);
  QSignalSpy accountChanged(&service, &TwitchAuthService::accountChanged);
  service.setClientId(QStringLiteral("replacement-client"));
  QVERIFY(!service.signedIn());
  QVERIFY(service.accessToken().isEmpty());
  QVERIFY(service.userId().isEmpty());
  QVERIFY(service.login().isEmpty());
  QCOMPARE(signedInChanged.count(), 1);
  QCOMPARE(accountChanged.count(), 1);
}

void AuthServiceTests::malformedRefreshPayloadPreservesCurrentToken()
{
  TwitchAuthService service(nullptr);
  service.m_token.accessToken = QStringLiteral("current-access");
  service.m_token.refreshToken = QStringLiteral("current-refresh");
  const auto missing = service.tokenFromRefreshPayload(QByteArrayLiteral("{}"), service.m_token.refreshToken);
  QVERIFY(!missing.has_value());
  QCOMPARE(service.accessToken(), QStringLiteral("current-access"));

  const QByteArray validPayload = QJsonDocument(QJsonObject{{QStringLiteral("access_token"), QStringLiteral("next-access")},
                                                            {QStringLiteral("refresh_token"), QString()},
                                                            {QStringLiteral("expires_in"), 3600}}).toJson(QJsonDocument::Compact);
  auto token = service.tokenFromRefreshPayload(validPayload, service.m_token.refreshToken);
  QVERIFY(token.has_value());
  QCOMPARE(token->accessToken, QStringLiteral("next-access"));
  QCOMPARE(token->refreshToken, QStringLiteral("current-refresh"));
}

void AuthServiceTests::authRetriesUseBoundedBackoff()
{
  TwitchAuthService service(nullptr);
  service.scheduleAuthRetry(TwitchAuthService::AuthTimerAction::Validate);
  QCOMPARE(service.m_authTimerAction, TwitchAuthService::AuthTimerAction::Validate);
  QCOMPARE(service.m_refreshTimer.interval(), 60000);

  for (int attempt = 0; attempt < 10; ++attempt) service.scheduleAuthRetry(TwitchAuthService::AuthTimerAction::Refresh);
  QCOMPARE(service.m_authTimerAction, TwitchAuthService::AuthTimerAction::Refresh);
  QCOMPARE(service.m_refreshTimer.interval(), 15 * 60 * 1000);
  QCOMPARE(service.m_authRetryDelayMs, 15 * 60 * 1000);
}

void AuthServiceTests::permanentRefreshFailuresAreClassified()
{
  QVERIFY(TwitchAuthService::isPermanentRefreshFailure(400));
  QVERIFY(TwitchAuthService::isPermanentRefreshFailure(401));
  QVERIFY(!TwitchAuthService::isPermanentRefreshFailure(0));
  QVERIFY(!TwitchAuthService::isPermanentRefreshFailure(429));
  QVERIFY(!TwitchAuthService::isPermanentRefreshFailure(500));
}

QTEST_MAIN(AuthServiceTests)
#include "tst_auth_service.moc"
