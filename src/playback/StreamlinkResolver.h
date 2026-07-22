#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QTemporaryFile>
#include <QTimer>

#include <memory>

class StreamlinkResolver : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString streamlinkPath READ streamlinkPath NOTIFY streamlinkPathChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
  explicit StreamlinkResolver(QObject *parent = nullptr);

  [[nodiscard]] QString streamlinkPath() const;
  [[nodiscard]] QString status() const;
  Q_INVOKABLE void resolve(const QString &channel, const QString &quality = QStringLiteral("best"), const QString &accessToken = {});
  Q_INVOKABLE void requestQualities(const QString &channel, const QString &accessToken = {});
  Q_INVOKABLE void cancel();

  [[nodiscard]] static QStringList parseQualitiesJson(const QByteArray &json);
  [[nodiscard]] static QString streamlinkQualityArgument(const QString &quality);
  [[nodiscard]] static QString discoverExecutable();

signals:
  void resolved(const QString &channel, const QString &url);
  void failed(const QString &channel, const QString &message);
  void qualitiesResolved(const QString &channel, const QStringList &qualities);
  void qualitiesFailed(const QString &channel, const QString &message);
  void streamlinkPathChanged();
  void statusChanged();

private:
  QString m_streamlinkPath;
  QString m_status;
  QProcess m_process;
  QTimer m_processTimeout;
  std::unique_ptr<QTemporaryFile> m_processConfig;
  QString m_channel;
  QString m_requestedQuality;
  QString m_streamlinkQuality;
  QString m_accessToken;
  QProcess m_qualityProcess;
  QTimer m_qualityTimeout;
  std::unique_ptr<QTemporaryFile> m_qualityConfig;
  QString m_qualityChannel;
  QString m_qualityAccessToken;
  int m_qualityAttempt = 0;
  bool m_cancelled = false;
  bool m_enhancedAttempt = false;

  void startProcess(bool enhanced);
  void startQualityProcess(int attempt);
  [[nodiscard]] QStringList twitchArguments(bool codecs) const;
  [[nodiscard]] static std::unique_ptr<QTemporaryFile> createAuthConfig(const QString &accessToken);
  static void cleanupConfig(std::unique_ptr<QTemporaryFile> &config);
  void setStatus(QString status);
};
