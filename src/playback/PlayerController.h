#pragma once

#include "playback/StreamlinkResolver.h"
#include "web/PlayerHostServer.h"

#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

class PlayerController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString channel READ channel NOTIFY channelChanged)
  Q_PROPERTY(QString broadcasterId READ broadcasterId NOTIFY broadcasterIdChanged)
  Q_PROPERTY(QString title READ title NOTIFY titleChanged)
  Q_PROPERTY(QString category READ category NOTIFY categoryChanged)
  Q_PROPERTY(QString categoryId READ categoryId NOTIFY categoryIdChanged)
  Q_PROPERTY(int viewerCount READ viewerCount NOTIFY viewerCountChanged)
  Q_PROPERTY(QString liveDuration READ liveDuration NOTIFY liveDurationChanged)
  Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
  Q_PROPERTY(QString quality READ quality WRITE setQuality NOTIFY qualityChanged)
  Q_PROPERTY(QStringList qualityOptions READ qualityOptions NOTIFY qualityOptionsChanged)
  Q_PROPERTY(QUrl standardUrl READ standardUrl NOTIFY standardUrlChanged)
  Q_PROPERTY(QString nativeSource READ nativeSource NOTIFY nativeSourceChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(bool paused READ paused WRITE setPaused NOTIFY pausedChanged)
  Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
  Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)

public:
  explicit PlayerController(PlayerHostServer *host, QObject *parent = nullptr);

  [[nodiscard]] QString channel() const;
  [[nodiscard]] QString broadcasterId() const;
  [[nodiscard]] QString title() const;
  [[nodiscard]] QString category() const;
  [[nodiscard]] QString categoryId() const;
  [[nodiscard]] int viewerCount() const;
  [[nodiscard]] QString liveDuration() const;
  [[nodiscard]] QString mode() const;
  void setMode(const QString &mode);
  [[nodiscard]] QString quality() const;
  void setQuality(const QString &quality);
  void setAccessToken(const QString &accessToken);
  void setWebsiteAccessToken(const QString &accessToken);
  [[nodiscard]] QStringList qualityOptions() const;
  [[nodiscard]] QUrl standardUrl() const;
  [[nodiscard]] QString nativeSource() const;
  [[nodiscard]] QString status() const;
  [[nodiscard]] bool paused() const;
  void setPaused(bool paused);
  [[nodiscard]] bool muted() const;
  void setMuted(bool muted);
  [[nodiscard]] int volume() const;
  void setVolume(int volume);

  Q_INVOKABLE void playChannel(const QVariantMap &item);
  Q_INVOKABLE void updateFromItem(const QVariantMap &item);
  Q_INVOKABLE void stop();
  Q_INVOKABLE void goLive();

signals:
  void channelChanged();
  void broadcasterIdChanged();
  void titleChanged();
  void categoryChanged();
  void categoryIdChanged();
  void viewerCountChanged();
  void liveDurationChanged();
  void modeChanged();
  void qualityChanged();
  void standardUrlChanged();
  void nativeSourceChanged();
  void statusChanged();
  void pausedChanged();
  void mutedChanged();
  void volumeChanged();
  void qualityOptionsChanged();
  void chatChannelRequested(const QString &channel);

private:
  PlayerHostServer *m_host = nullptr;
  StreamlinkResolver m_streamlink;
  QString m_channel;
  QString m_broadcasterId;
  QString m_title;
  QString m_category;
  QString m_categoryId;
  QString m_liveDuration;
  QString m_mode = QStringLiteral("native");
  QString m_quality;
  QStringList m_qualityOptions;
  QString m_accessToken;
  QString m_websiteAccessToken;
  QUrl m_standardUrl;
  QString m_nativeSource;
  QString m_status;
  bool m_paused = false;
  bool m_muted = false;
  int m_volume = 80;
  int m_viewerCount = 0;
  int m_stopGeneration = 0;
  bool m_waitingForQualities = false;
  bool m_autoQuality = true;
  QDateTime m_startedAt;
  QTimer m_liveDurationTimer;

  void resolveNative();
  void requestNativeQualities();
  void applyAvailableQualities(const QStringList &qualities);
  [[nodiscard]] QString effectiveQualityForResolve() const;
  [[nodiscard]] QString streamlinkAccessToken() const;
  void finishStop(int generation);
  void updateLiveDuration();
  [[nodiscard]] static QString durationText(const QDateTime &startedAt);
  void setStatus(QString status);
};
