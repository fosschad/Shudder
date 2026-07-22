#include "app/AppController.h"

#include "core/UrlUtils.h"
#include "shudder_config.h"

#include <QCoreApplication>
#include <QDesktopServices>

AppController::AppController(TwitchDirectoryModel *directory, PlayerController *player, ChatModel *chat,
                             PreferencesService *preferences, QObject *parent)
    : QObject(parent), m_directory(directory), m_player(player), m_chat(chat), m_preferences(preferences)
{
  if (m_player && m_chat) {
    connect(m_player, &PlayerController::chatChannelRequested, m_chat, &ChatModel::join);
  }
}

QString AppController::applicationName() const { return QStringLiteral(SHUDDER_PRODUCT_NAME); }
QString AppController::applicationId() const { return QStringLiteral(SHUDDER_APP_ID); }
QString AppController::version() const { return QStringLiteral(SHUDDER_VERSION); }

void AppController::openItem(const QVariantMap &item)
{
  const QString kind = item.value(QStringLiteral("kind")).toString();
  if (kind == QLatin1String("category")) {
    if (m_directory) m_directory->loadCategoryStreams(item.value(QStringLiteral("categoryId")).toString(), item.value(QStringLiteral("category")).toString());
    return;
  }
  if (m_player) m_player->playChannel(item);
}

void AppController::openExternalUrl(const QString &url)
{
  const QUrl parsed(url);
  if (UrlUtils::isSafeHttpUrl(parsed)) QDesktopServices::openUrl(parsed);
}

void AppController::quit()
{
  if (m_preferences) m_preferences->saveNow();
  QCoreApplication::quit();
}
