#include "app/AppController.h"
#include "chat/ChatModel.h"
#include "playback/MpvVideoItem.h"
#include "playback/PlayerController.h"
#include "shudder_config.h"
#include "storage/PreferencesService.h"
#include "storage/SecretService.h"
#include "twitch/TwitchAuthService.h"
#include "twitch/TwitchDirectoryModel.h"
#include "twitch/TwitchWebsiteSession.h"
#include "web/PlayerHostServer.h"

#include <QGuiApplication>
#include <QIcon>
#include <QDebug>
#include <QDir>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlError>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>

namespace {
class DiskCacheNetworkFactory final : public QQmlNetworkAccessManagerFactory {
public:
  explicit DiskCacheNetworkFactory(QString cacheDirectory) : m_cacheDirectory(std::move(cacheDirectory)) {}

  QNetworkAccessManager *create(QObject *parent) override
  {
    auto *manager = new QNetworkAccessManager(parent);
    auto *cache = new QNetworkDiskCache(manager);
    cache->setCacheDirectory(QDir(m_cacheDirectory).filePath(QStringLiteral("qml-network-cache")));
    cache->setMaximumCacheSize(96 * 1024 * 1024);
    manager->setCache(cache);
    return manager;
  }

private:
  QString m_cacheDirectory;
};
}

int main(int argc, char *argv[])
{
  setenv("LC_NUMERIC", "C", 1);
  setlocale(LC_NUMERIC, "C");
  QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QtWebEngineQuick::initialize();

  QGuiApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral(SHUDDER_PRODUCT_NAME));
  QCoreApplication::setApplicationVersion(QStringLiteral(SHUDDER_VERSION));
  QCoreApplication::setOrganizationName(QStringLiteral(SHUDDER_GITHUB_OWNER));
  QGuiApplication::setDesktopFileName(QStringLiteral(SHUDDER_APP_ID));
  app.setApplicationDisplayName(QStringLiteral(SHUDDER_PRODUCT_NAME));
  app.setWindowIcon(QIcon(QStringLiteral(":/shudder/Shudder/icons/shudder.svg")));
  QQuickStyle::setStyle(QStringLiteral("Fusion"));

  qmlRegisterType<MpvVideoItem>("Shudder.Playback", 1, 0, "MpvVideoItem");

  XdgPaths paths;
  PreferencesService preferences(paths);
  preferences.load();
  ShudderSecretStore secrets;
  TwitchAuthService auth(&secrets);
  TwitchWebsiteSession websiteSession(&secrets);
  PlayerHostServer playerHost;
  playerHost.start();
  TwitchDirectoryModel home;
  home.setClientId(auth.clientId());
  home.setAccessToken(auth.accessToken());
  TwitchDirectoryModel directory;
  directory.setClientId(auth.clientId());
  directory.setAccessToken(auth.accessToken());
  TwitchDirectoryModel live;
  live.setClientId(auth.clientId());
  live.setAccessToken(auth.accessToken());
  TwitchDirectoryModel followed;
  followed.setClientId(auth.clientId());
  followed.setAccessToken(auth.accessToken());
  ChatModel chat;
  chat.setClientId(auth.clientId());
  chat.setAccessToken(auth.accessToken());
  chat.setSender(auth.userId(), auth.login(), auth.displayName());
  chat.setHistoryLimit(preferences.get(QStringLiteral("historyLimit")).toInt());
  PlayerController player(&playerHost);
  player.setMode(preferences.get(QStringLiteral("playerMode")).toString());
  player.setQuality(preferences.get(QStringLiteral("nativeQuality")).toString());
  player.setAccessToken(auth.accessToken());
  player.setWebsiteAccessToken(websiteSession.accessToken());
  AppController controller(&directory, &player, &chat, &preferences);

  QObject::connect(&preferences, &PreferencesService::valuesChanged, [&preferences, &chat, &player]() {
    chat.setHistoryLimit(preferences.get(QStringLiteral("historyLimit")).toInt());
    player.setMode(preferences.get(QStringLiteral("playerMode")).toString());
    player.setQuality(preferences.get(QStringLiteral("nativeQuality")).toString());
  });
  QObject::connect(&auth, &TwitchAuthService::clientIdChanged, [&auth, &home, &directory, &live, &followed, &chat]() {
    home.setClientId(auth.clientId());
    directory.setClientId(auth.clientId());
    live.setClientId(auth.clientId());
    followed.setClientId(auth.clientId());
    chat.setClientId(auth.clientId());
  });
  QObject::connect(&auth, &TwitchAuthService::accessTokenChanged, [&auth, &home, &directory, &live, &followed, &chat, &player]() {
    home.setAccessToken(auth.accessToken());
    directory.setAccessToken(auth.accessToken());
    live.setAccessToken(auth.accessToken());
    followed.setAccessToken(auth.accessToken());
    chat.setAccessToken(auth.accessToken());
    player.setAccessToken(auth.accessToken());
    if (!auth.accessToken().isEmpty()) {
      home.loadLive();
      live.loadLive();
      followed.loadFollowedLive(auth.userId());
    }
  });
  QObject::connect(&auth, &TwitchAuthService::accountChanged, [&auth, &followed, &chat]() {
    chat.setSender(auth.userId(), auth.login(), auth.displayName());
    if (auth.signedIn()) followed.loadFollowedLive(auth.userId());
    else followed.loadFollowedLive({});
  });
  QObject::connect(&websiteSession, &TwitchWebsiteSession::sessionChanged, [&websiteSession, &player]() {
    player.setWebsiteAccessToken(websiteSession.accessToken());
  });

  QQmlApplicationEngine engine;
  engine.setNetworkAccessManagerFactory(new DiskCacheNetworkFactory(paths.cacheDirectory()));
  engine.rootContext()->setContextProperty(QStringLiteral("appController"), &controller);
  engine.rootContext()->setContextProperty(QStringLiteral("preferences"), &preferences);
  engine.rootContext()->setContextProperty(QStringLiteral("secrets"), &secrets);
  engine.rootContext()->setContextProperty(QStringLiteral("authService"), &auth);
  engine.rootContext()->setContextProperty(QStringLiteral("websiteSession"), &websiteSession);
  engine.rootContext()->setContextProperty(QStringLiteral("homeModel"), &home);
  engine.rootContext()->setContextProperty(QStringLiteral("directoryModel"), &directory);
  engine.rootContext()->setContextProperty(QStringLiteral("liveModel"), &live);
  engine.rootContext()->setContextProperty(QStringLiteral("followedModel"), &followed);
  engine.rootContext()->setContextProperty(QStringLiteral("chatModel"), &chat);
  engine.rootContext()->setContextProperty(QStringLiteral("playerController"), &player);

  QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError> &warnings) {
    for (const QQmlError &warning : warnings) std::fprintf(stderr, "%s\n", warning.toString().toUtf8().constData());
  });
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, [](const QUrl &url) {
    std::fprintf(stderr, "Failed to create QML object from %s\n", url.toString().toUtf8().constData());
  });
  engine.load(QUrl(QStringLiteral("qrc:/shudder/Shudder/Main.qml")));
  if (engine.rootObjects().isEmpty()) {
    std::fprintf(stderr, "Failed to load Shudder QML root object.\n");
    return 1;
  }
  for (QObject *object : engine.rootObjects()) {
    if (QQuickWindow *window = qobject_cast<QQuickWindow *>(object)) {
      window->setPersistentGraphics(true);
      window->setPersistentSceneGraph(true);
    }
  }
  home.loadLive();
  if (auth.signedIn()) live.loadLive();
  if (auth.signedIn()) followed.loadFollowedLive(auth.userId());
  return app.exec();
}
