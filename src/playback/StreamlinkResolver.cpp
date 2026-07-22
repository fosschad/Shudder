#include "playback/StreamlinkResolver.h"

#include "core/UrlUtils.h"
#include "shudder_config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

StreamlinkResolver::StreamlinkResolver(QObject *parent) : QObject(parent), m_streamlinkPath(discoverExecutable())
{
  m_processTimeout.setSingleShot(true);
  m_processTimeout.setInterval(45000);
  m_qualityTimeout.setSingleShot(true);
  m_qualityTimeout.setInterval(45000);

  connect(&m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
    m_processTimeout.stop();
    cleanupConfig(m_processConfig);
    if (m_cancelled) return;
    const QString out = QString::fromUtf8(m_process.readAllStandardOutput()).trimmed();
    const QString err = UrlUtils::redactedForLog(QString::fromUtf8(m_process.readAllStandardError()).trimmed());
    if (exitStatus == QProcess::NormalExit && exitCode == 0 && out.startsWith(QStringLiteral("http"))) {
      setStatus(tr("Native stream URL resolved."));
      emit resolved(m_channel, out.split(QLatin1Char('\n')).first().trimmed());
    } else {
      if (m_enhancedAttempt) {
        setStatus(tr("Retrying %1 without Twitch auth/header options...").arg(m_requestedQuality));
        startProcess(false);
        return;
      }
      const QString message = err.isEmpty() ? tr("Streamlink could not resolve this stream.") : err.left(800);
      setStatus(message);
      emit failed(m_channel, message);
    }
  });
  connect(&m_qualityProcess, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
    m_qualityTimeout.stop();
    cleanupConfig(m_qualityConfig);
    const QString out = QString::fromUtf8(m_qualityProcess.readAllStandardOutput()).trimmed();
    const QString err = UrlUtils::redactedForLog(QString::fromUtf8(m_qualityProcess.readAllStandardError()).trimmed());
    if (exitStatus == QProcess::NormalExit && exitCode == 0) {
      const QStringList qualities = parseQualitiesJson(out.toUtf8());
      if (!qualities.isEmpty()) {
        emit qualitiesResolved(m_qualityChannel, qualities);
        return;
      }
    }
    if (m_qualityAttempt < 2) {
      startQualityProcess(m_qualityAttempt + 1);
      return;
    }
    const QString message = err.isEmpty() ? tr("Streamlink could not list stream qualities.") : err.left(800);
    emit qualitiesFailed(m_qualityChannel, message);
  });
  connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
    m_processTimeout.stop();
    cleanupConfig(m_processConfig);
    if (m_cancelled) return;
    const QString message = tr("Streamlink failed to start: %1").arg(m_process.errorString());
    setStatus(message);
    emit failed(m_channel, message);
  });
  connect(&m_processTimeout, &QTimer::timeout, this, [this]() {
    if (m_process.state() == QProcess::NotRunning) return;
    m_cancelled = true;
    m_process.kill();
    cleanupConfig(m_processConfig);
    const QString message = tr("Streamlink timed out while resolving this stream.");
    setStatus(message);
    emit failed(m_channel, message);
  });
  connect(&m_qualityTimeout, &QTimer::timeout, this, [this]() {
    if (m_qualityProcess.state() == QProcess::NotRunning) return;
    m_qualityProcess.kill();
    cleanupConfig(m_qualityConfig);
    emit qualitiesFailed(m_qualityChannel, tr("Streamlink timed out while listing stream qualities."));
  });
}

QString StreamlinkResolver::streamlinkPath() const { return m_streamlinkPath; }
QString StreamlinkResolver::status() const { return m_status; }

void StreamlinkResolver::resolve(const QString &channel, const QString &quality, const QString &accessToken)
{
  const QString login = channel.trimmed().toLower();
  if (login.isEmpty()) return;
  if (m_streamlinkPath.isEmpty()) {
    const QString message = tr("Streamlink is unavailable. Install streamlink or set SHUDDER_STREAMLINK_PATH.");
    setStatus(message);
    emit failed(login, message);
    return;
  }
  cancel();
  if (m_process.state() != QProcess::NotRunning) {
    const QString message = tr("Streamlink is still stopping. Try again in a moment.");
    setStatus(message);
    emit failed(login, message);
    return;
  }
  m_cancelled = false;
  m_channel = login;
  m_requestedQuality = quality.trimmed().isEmpty() ? QStringLiteral("source") : quality.trimmed().toLower();
  m_streamlinkQuality = streamlinkQualityArgument(m_requestedQuality);
  m_accessToken = accessToken.trimmed();
  setStatus(tr("Resolving %1 at %2 with Streamlink...").arg(login, m_requestedQuality));
  startProcess(true);
}

void StreamlinkResolver::startProcess(bool enhanced)
{
  m_enhancedAttempt = enhanced;
  cleanupConfig(m_processConfig);
  QStringList args;
  args << QStringLiteral("--stream-url") << twitchArguments(enhanced);
  if (enhanced) {
    m_processConfig = createAuthConfig(m_accessToken);
    if (m_processConfig) args << QStringLiteral("--config") << m_processConfig->fileName();
  }
  args << QStringLiteral("https://www.twitch.tv/%1").arg(m_channel) << m_streamlinkQuality;
  m_process.start(m_streamlinkPath, args);
  m_processTimeout.start();
}

void StreamlinkResolver::requestQualities(const QString &channel, const QString &accessToken)
{
  const QString login = channel.trimmed().toLower();
  if (login.isEmpty()) return;
  if (m_streamlinkPath.isEmpty()) {
    emit qualitiesFailed(login, tr("Streamlink is unavailable. Install streamlink or set SHUDDER_STREAMLINK_PATH."));
    return;
  }
  if (m_qualityProcess.state() != QProcess::NotRunning) {
    m_qualityProcess.kill();
    m_qualityProcess.waitForFinished(1000);
  }
  m_qualityChannel = login;
  m_qualityAccessToken = accessToken.trimmed();
  startQualityProcess(0);
}

void StreamlinkResolver::startQualityProcess(int attempt)
{
  m_qualityAttempt = attempt;
  cleanupConfig(m_qualityConfig);
  QStringList args;
  args << QStringLiteral("--json") << twitchArguments(attempt <= 1);
  if (attempt == 0) {
    m_qualityConfig = createAuthConfig(m_qualityAccessToken);
    if (m_qualityConfig) args << QStringLiteral("--config") << m_qualityConfig->fileName();
  }
  args << QStringLiteral("https://www.twitch.tv/%1").arg(m_qualityChannel);
  m_qualityProcess.start(m_streamlinkPath, args);
  m_qualityTimeout.start();
}

QStringList StreamlinkResolver::twitchArguments(bool codecs) const
{
  QStringList args{QStringLiteral("--twitch-disable-hosting")};
  if (codecs) args << QStringLiteral("--twitch-supported-codecs") << QStringLiteral("av1,h265,h264");
  return args;
}

std::unique_ptr<QTemporaryFile> StreamlinkResolver::createAuthConfig(const QString &accessToken)
{
  const QString token = accessToken.trimmed();
  if (token.isEmpty()) return nullptr;
  auto config = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/shudder-streamlink-XXXXXX.conf"));
  config->setAutoRemove(true);
  if (!config->open()) return nullptr;
  config->setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  const QByteArray header = QByteArrayLiteral("twitch-api-header=Authorization=OAuth ") + token.toUtf8() + QByteArrayLiteral("\n");
  if (config->write(header) != header.size()) return nullptr;
  config->flush();
  return config;
}

void StreamlinkResolver::cleanupConfig(std::unique_ptr<QTemporaryFile> &config)
{
  if (!config) return;
  config->remove();
  config.reset();
}

void StreamlinkResolver::cancel()
{
  m_processTimeout.stop();
  m_qualityTimeout.stop();
  if (m_process.state() != QProcess::NotRunning) {
    m_cancelled = true;
    m_process.kill();
    m_process.waitForFinished(1000);
  }
  cleanupConfig(m_processConfig);
  if (m_qualityProcess.state() != QProcess::NotRunning) {
    m_qualityProcess.kill();
    m_qualityProcess.waitForFinished(1000);
  }
  cleanupConfig(m_qualityConfig);
}

QStringList StreamlinkResolver::parseQualitiesJson(const QByteArray &json)
{
  QJsonParseError error;
  const QJsonDocument document = QJsonDocument::fromJson(json, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
  const QJsonObject streams = document.object().value(QStringLiteral("streams")).toObject();
  QStringList qualities;
  for (auto it = streams.begin(); it != streams.end(); ++it) {
    if (it.key().compare(QStringLiteral("worst"), Qt::CaseInsensitive) == 0) continue;
    qualities.push_back(it.key());
  }
  qualities.sort(Qt::CaseInsensitive);
  if (qualities.removeOne(QStringLiteral("best"))) qualities.prepend(QStringLiteral("best"));
  return qualities;
}

QString StreamlinkResolver::streamlinkQualityArgument(const QString &quality)
{
  QString normalized = quality.trimmed().toLower();
  if (normalized.isEmpty() || normalized == QLatin1String("source")) return QStringLiteral("best");
  if (normalized == QLatin1String("worst")) return QStringLiteral("best");
  if (normalized == QLatin1String("1440p+")) return QStringLiteral("1440p60,1440p");
  if (normalized == QLatin1String("2160p")) return QStringLiteral("2160p60,2160p");
  if (normalized == QLatin1String("1440p")) return QStringLiteral("1440p60,1440p");
  if (normalized == QLatin1String("1080p")) return QStringLiteral("1080p60,1080p");
  if (normalized == QLatin1String("720p")) return QStringLiteral("720p60,720p");
  return normalized;
}

QString StreamlinkResolver::discoverExecutable()
{
  const QStringList environment = {QStringLiteral("SHUDDER_STREAMLINK_PATH")};
  for (const QString &variable : environment) {
    const QByteArray value = qgetenv(variable.toLatin1().constData());
    if (!value.isEmpty()) return QString::fromLocal8Bit(value);
  }
  QString bundled = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("streamlink"));
  if (QFileInfo::exists(bundled)) return bundled;
  return QStandardPaths::findExecutable(QStringLiteral("streamlink"));
}

void StreamlinkResolver::setStatus(QString status)
{
  if (m_status == status) return;
  m_status = std::move(status);
  emit statusChanged();
}
