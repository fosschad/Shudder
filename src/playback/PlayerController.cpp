#include "playback/PlayerController.h"

#include <QSet>
#include <QRegularExpression>
#include <QTimer>
#include <QtGlobal>

PlayerController::PlayerController(PlayerHostServer *host, QObject *parent) : QObject(parent), m_host(host)
{
  m_liveDurationTimer.setInterval(30000);
  connect(&m_liveDurationTimer, &QTimer::timeout, this, &PlayerController::updateLiveDuration);
  connect(&m_streamlink, &StreamlinkResolver::resolved, this, [this](const QString &channel, const QString &url) {
    if (channel != m_channel) return;
    m_nativeSource = url;
    emit nativeSourceChanged();
    setStatus(tr("Native stream ready."));
  });
  connect(&m_streamlink, &StreamlinkResolver::failed, this, [this](const QString &channel, const QString &message) {
    if (channel != m_channel) return;
    setStatus(message);
  });
  connect(&m_streamlink, &StreamlinkResolver::qualitiesResolved, this, [this](const QString &channel, const QStringList &qualities) {
    if (channel != m_channel) return;
    applyAvailableQualities(qualities);
    if (m_waitingForQualities && m_mode == QLatin1String("native")) {
      m_waitingForQualities = false;
      resolveNative();
    }
  });
  connect(&m_streamlink, &StreamlinkResolver::qualitiesFailed, this, [this](const QString &channel, const QString &) {
    if (channel != m_channel) return;
    if (!m_qualityOptions.isEmpty()) {
      m_qualityOptions.clear();
      emit qualityOptionsChanged();
    }
    if (m_waitingForQualities && m_mode == QLatin1String("native")) {
      m_waitingForQualities = false;
      resolveNative();
    }
  });
}

QString PlayerController::channel() const { return m_channel; }
QString PlayerController::broadcasterId() const { return m_broadcasterId; }
QString PlayerController::title() const { return m_title; }
QString PlayerController::category() const { return m_category; }
QString PlayerController::categoryId() const { return m_categoryId; }
int PlayerController::viewerCount() const { return m_viewerCount; }
QString PlayerController::liveDuration() const { return m_liveDuration; }
QString PlayerController::mode() const { return m_mode; }

void PlayerController::setMode(const QString &mode)
{
  const QString normalized = mode == QLatin1String("native") ? QStringLiteral("native") : QStringLiteral("standard");
  if (m_mode == normalized) return;
  if (!m_channel.isEmpty()) {
    if (normalized == QLatin1String("native") && !m_standardUrl.isEmpty()) {
      m_standardUrl = QUrl();
      emit standardUrlChanged();
    } else if (normalized == QLatin1String("standard")) {
      const QUrl nextUrl = m_host ? m_host->playerUrl(m_channel) : QUrl();
      if (m_standardUrl != nextUrl) {
        m_standardUrl = nextUrl;
        emit standardUrlChanged();
      }
    }
  }
  if (normalized == QLatin1String("standard")) {
    m_streamlink.cancel();
    if (!m_nativeSource.isEmpty()) {
      m_nativeSource.clear();
      emit nativeSourceChanged();
    }
  }
  m_mode = normalized;
  emit modeChanged();
  if (!m_channel.isEmpty() && m_mode == QLatin1String("native")) resolveNative();
}

QString PlayerController::quality() const { return m_quality; }

void PlayerController::setQuality(const QString &quality)
{
  QString normalized = quality.trimmed().toLower();
  const bool autoQuality = normalized.isEmpty() || normalized == QLatin1String("source") || normalized == QLatin1String("best") || normalized == QLatin1String("worst");
  if (autoQuality) normalized.clear();
  if (normalized == QLatin1String("1440p+")) normalized = QStringLiteral("1440p60");
  if (m_quality == normalized && m_autoQuality == autoQuality) return;
  m_quality = normalized;
  m_autoQuality = autoQuality;
  emit qualityChanged();
  if (!m_channel.isEmpty() && m_mode == QLatin1String("native")) {
    resolveNative();
  }
}

void PlayerController::setAccessToken(const QString &accessToken)
{
  const QString trimmed = accessToken.trimmed();
  if (m_accessToken == trimmed) return;
  m_accessToken = trimmed;
}

void PlayerController::setWebsiteAccessToken(const QString &accessToken)
{
  const QString trimmed = accessToken.trimmed();
  if (m_websiteAccessToken == trimmed) return;
  m_websiteAccessToken = trimmed;
  if (!m_channel.isEmpty() && m_mode == QLatin1String("native")) {
    setStatus(trimmed.isEmpty() ? tr("Refreshing native qualities without Twitch website session...")
                                : tr("Refreshing native qualities with Twitch website session..."));
    requestNativeQualities();
    m_waitingForQualities = true;
  }
}

QStringList PlayerController::qualityOptions() const
{
  return m_qualityOptions;
}

QUrl PlayerController::standardUrl() const { return m_standardUrl; }
QString PlayerController::nativeSource() const { return m_nativeSource; }
QString PlayerController::status() const { return m_status; }
bool PlayerController::paused() const { return m_paused; }
bool PlayerController::muted() const { return m_muted; }
int PlayerController::volume() const { return m_volume; }

void PlayerController::setPaused(bool paused)
{
  if (m_paused == paused) return;
  m_paused = paused;
  emit pausedChanged();
  if (!m_paused && !m_channel.isEmpty()) QTimer::singleShot(0, this, &PlayerController::goLive);
}

void PlayerController::setMuted(bool muted)
{
  if (m_muted == muted) return;
  m_muted = muted;
  emit mutedChanged();
}

void PlayerController::setVolume(int volume)
{
  const int bounded = qBound(0, volume, 200);
  if (m_volume == bounded) return;
  m_volume = bounded;
  emit volumeChanged();
}

void PlayerController::playChannel(const QVariantMap &item)
{
  ++m_stopGeneration;
  const QString login = item.value(QStringLiteral("login")).toString().toLower();
  if (login.isEmpty()) return;
  QString newBroadcasterId = item.value(QStringLiteral("broadcasterId")).toString();
  if (newBroadcasterId.isEmpty()) newBroadcasterId = item.value(QStringLiteral("itemId")).toString();
  const QString newTitle = item.value(QStringLiteral("title")).toString();
  const QString newCategory = item.value(QStringLiteral("category")).toString();
  const QString newCategoryId = item.value(QStringLiteral("categoryId")).toString();
  const int newViewerCount = item.value(QStringLiteral("viewerCount")).toInt();
  const QDateTime newStartedAt = QDateTime::fromString(item.value(QStringLiteral("startedAt")).toString(), Qt::ISODate);
  const bool channelChangedValue = m_channel != login;
  const bool broadcasterChangedValue = m_broadcasterId != newBroadcasterId;
  const bool categoryIdChangedValue = m_categoryId != newCategoryId;
  const bool viewerCountChangedValue = m_viewerCount != newViewerCount;
  m_channel = login;
  m_broadcasterId = newBroadcasterId;
  m_title = newTitle.isEmpty() ? login : newTitle;
  m_category = newCategory;
  m_categoryId = newCategoryId;
  m_viewerCount = newViewerCount;
  m_startedAt = newStartedAt;
  const QString nextDuration = durationText(m_startedAt);
  const bool liveDurationChangedValue = m_liveDuration != nextDuration;
  m_liveDuration = nextDuration;
  if (m_paused) {
    m_paused = false;
    emit pausedChanged();
  }
  if (channelChangedValue) emit channelChanged();
  if (broadcasterChangedValue) emit broadcasterIdChanged();
  emit titleChanged();
  emit categoryChanged();
  if (categoryIdChangedValue) emit categoryIdChanged();
  if (viewerCountChangedValue) emit viewerCountChanged();
  if (liveDurationChangedValue) emit liveDurationChanged();
  if (m_startedAt.isValid() && !m_liveDurationTimer.isActive()) m_liveDurationTimer.start();
  else if (!m_startedAt.isValid()) m_liveDurationTimer.stop();
  m_standardUrl = m_host ? m_host->playerUrl(login) : QUrl();
  emit standardUrlChanged();
  m_nativeSource.clear();
  emit nativeSourceChanged();
  emit chatChannelRequested(login);
  setStatus(tr("Opening %1...").arg(login));
  if (!m_qualityOptions.isEmpty()) {
    m_qualityOptions.clear();
    emit qualityOptionsChanged();
  }
  requestNativeQualities();
  if (m_mode == QLatin1String("native")) m_waitingForQualities = true;
}

void PlayerController::updateFromItem(const QVariantMap &item)
{
  const QString login = item.value(QStringLiteral("login")).toString().toLower();
  if (login.isEmpty() || login != m_channel) return;

  const QString newTitle = item.value(QStringLiteral("title")).toString();
  const QString newCategory = item.value(QStringLiteral("category")).toString();
  const QString newCategoryId = item.value(QStringLiteral("categoryId")).toString();
  QString newBroadcasterId = item.value(QStringLiteral("broadcasterId")).toString();
  if (newBroadcasterId.isEmpty()) newBroadcasterId = m_broadcasterId;
  const int newViewerCount = item.contains(QStringLiteral("viewerCount")) ? item.value(QStringLiteral("viewerCount")).toInt() : m_viewerCount;
  const QDateTime newStartedAt = QDateTime::fromString(item.value(QStringLiteral("startedAt")).toString(), Qt::ISODate);

  if (!newTitle.isEmpty() && m_title != newTitle) {
    m_title = newTitle;
    emit titleChanged();
  }
  if (m_category != newCategory) {
    m_category = newCategory;
    emit categoryChanged();
  }
  if (m_categoryId != newCategoryId) {
    m_categoryId = newCategoryId;
    emit categoryIdChanged();
  }
  if (m_broadcasterId != newBroadcasterId) {
    m_broadcasterId = newBroadcasterId;
    emit broadcasterIdChanged();
  }
  if (m_viewerCount != newViewerCount) {
    m_viewerCount = newViewerCount;
    emit viewerCountChanged();
  }
  if (newStartedAt.isValid() && m_startedAt != newStartedAt) {
    m_startedAt = newStartedAt;
    updateLiveDuration();
    if (!m_liveDurationTimer.isActive()) m_liveDurationTimer.start();
  }
}

void PlayerController::stop()
{
  const int generation = ++m_stopGeneration;
  m_streamlink.cancel();
  const bool standardChanged = !m_standardUrl.isEmpty();
  const bool nativeChanged = !m_nativeSource.isEmpty();

  m_standardUrl = QUrl();
  m_nativeSource.clear();
  if (standardChanged) emit standardUrlChanged();
  if (nativeChanged) emit nativeSourceChanged();

  setStatus(tr("Playback stopped."));
  QTimer::singleShot(120, this, [this, generation]() { finishStop(generation); });
}

void PlayerController::finishStop(int generation)
{
  if (generation != m_stopGeneration) return;
  const bool channelChangedValue = !m_channel.isEmpty();
  const bool broadcasterChangedValue = !m_broadcasterId.isEmpty();
  const bool titleChangedValue = !m_title.isEmpty();
  const bool categoryChangedValue = !m_category.isEmpty();
  const bool categoryIdChangedValue = !m_categoryId.isEmpty();
  const bool viewerCountChangedValue = m_viewerCount != 0;
  const bool liveDurationChangedValue = !m_liveDuration.isEmpty();

  m_channel.clear();
  m_broadcasterId.clear();
  m_title.clear();
  m_category.clear();
  m_categoryId.clear();
  m_viewerCount = 0;
  m_startedAt = QDateTime();
  m_liveDuration.clear();
  m_liveDurationTimer.stop();
  if (channelChangedValue) emit channelChanged();
  if (broadcasterChangedValue) emit broadcasterIdChanged();
  if (titleChangedValue) emit titleChanged();
  if (categoryChangedValue) emit categoryChanged();
  if (categoryIdChangedValue) emit categoryIdChanged();
  if (viewerCountChangedValue) emit viewerCountChanged();
  if (liveDurationChangedValue) emit liveDurationChanged();
}

void PlayerController::goLive()
{
  if (m_channel.isEmpty()) return;
  if (m_mode == QLatin1String("native")) resolveNative();
  else {
    m_standardUrl = m_host ? m_host->playerUrl(m_channel) : QUrl();
    emit standardUrlChanged();
  }
  setStatus(tr("Returning to live edge."));
}

void PlayerController::resolveNative()
{
  if (m_channel.isEmpty()) return;
  if (!m_nativeSource.isEmpty()) {
    m_nativeSource.clear();
    emit nativeSourceChanged();
  }
  m_streamlink.resolve(m_channel, effectiveQualityForResolve(), streamlinkAccessToken());
}

void PlayerController::requestNativeQualities()
{
  if (m_channel.isEmpty()) return;
  m_streamlink.requestQualities(m_channel, streamlinkAccessToken());
}

void PlayerController::applyAvailableQualities(const QStringList &qualities)
{
  QSet<QString> available;
  for (const QString &quality : qualities) available.insert(quality.trimmed().toLower());
  available.remove(QStringLiteral("source"));
  available.remove(QStringLiteral("best"));
  available.remove(QStringLiteral("worst"));

  const auto qualityScore = [](const QString &quality) {
    const QString normalized = quality.trimmed().toLower();
    if (normalized == QLatin1String("audio_only")) return -2;
    const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("^(\\d+)p(?:(\\d+))?$"), QRegularExpression::CaseInsensitiveOption).match(normalized);
    if (!match.hasMatch()) return 0;
    const int height = match.captured(1).toInt();
    const int fps = match.captured(2).isEmpty() ? 0 : match.captured(2).toInt();
    return height * 1000 + fps;
  };

  QStringList next;
  for (const QString &quality : std::as_const(available)) if (!quality.isEmpty()) next.push_back(quality);
  std::stable_sort(next.begin(), next.end(), [qualityScore](const QString &left, const QString &right) {
    const int leftScore = qualityScore(left);
    const int rightScore = qualityScore(right);
    if (leftScore != rightScore) return leftScore > rightScore;
    return QString::localeAwareCompare(left, right) < 0;
  });
  if (m_qualityOptions != next) {
    m_qualityOptions = next;
    emit qualityOptionsChanged();
  }
  const QString nextQuality = m_qualityOptions.isEmpty() ? QString() : (m_autoQuality || !m_qualityOptions.contains(m_quality) ? m_qualityOptions.first() : m_quality);
  const bool nextAutoQuality = m_autoQuality || !m_qualityOptions.contains(m_quality);
  if (m_quality != nextQuality || m_autoQuality != nextAutoQuality) {
    m_quality = nextQuality;
    m_autoQuality = nextAutoQuality;
    emit qualityChanged();
  }
}

QString PlayerController::effectiveQualityForResolve() const
{
  if (!m_quality.isEmpty()) return m_quality;
  if (!m_qualityOptions.isEmpty()) return m_qualityOptions.first();
  return QStringLiteral("best");
}

QString PlayerController::streamlinkAccessToken() const
{
  return m_websiteAccessToken.isEmpty() ? m_accessToken : m_websiteAccessToken;
}

void PlayerController::updateLiveDuration()
{
  const QString nextDuration = durationText(m_startedAt);
  if (m_liveDuration == nextDuration) return;
  m_liveDuration = nextDuration;
  emit liveDurationChanged();
}

QString PlayerController::durationText(const QDateTime &startedAt)
{
  if (!startedAt.isValid()) return {};
  const qint64 seconds = startedAt.secsTo(QDateTime::currentDateTimeUtc());
  const qint64 hours = seconds / 3600;
  const qint64 minutes = (seconds % 3600) / 60;
  if (hours > 0) return QStringLiteral("%1h %2m").arg(hours).arg(minutes);
  return QStringLiteral("%1m").arg(qMax<qint64>(0, minutes));
}

void PlayerController::setStatus(QString status)
{
  if (m_status == status) return;
  m_status = std::move(status);
  emit statusChanged();
}
