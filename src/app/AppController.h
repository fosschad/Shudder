#pragma once

#include "chat/ChatModel.h"
#include "playback/PlayerController.h"
#include "storage/PreferencesService.h"
#include "twitch/TwitchDirectoryModel.h"

#include <QObject>

class AppController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString applicationName READ applicationName CONSTANT)
  Q_PROPERTY(QString applicationId READ applicationId CONSTANT)
  Q_PROPERTY(QString version READ version CONSTANT)

public:
  AppController(TwitchDirectoryModel *directory, PlayerController *player, ChatModel *chat,
                PreferencesService *preferences, QObject *parent = nullptr);

  [[nodiscard]] QString applicationName() const;
  [[nodiscard]] QString applicationId() const;
  [[nodiscard]] QString version() const;

  Q_INVOKABLE void openItem(const QVariantMap &item);
  Q_INVOKABLE void openExternalUrl(const QString &url);
  Q_INVOKABLE void quit();

private:
  TwitchDirectoryModel *m_directory = nullptr;
  PlayerController *m_player = nullptr;
  ChatModel *m_chat = nullptr;
  PreferencesService *m_preferences = nullptr;
};
