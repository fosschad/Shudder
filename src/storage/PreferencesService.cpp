#include "storage/PreferencesService.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSaveFile>

namespace {
constexpr int kSchemaVersion = 1;

QVariant boundedInt(const QVariant &value, int fallback, int min, int max, bool *ok)
{
  bool converted = false;
  int result = value.toInt(&converted);
  if (!converted) result = fallback;
  if (ok) *ok = converted;
  return qBound(min, result, max);
}

QVariant boundedDouble(const QVariant &value, double fallback, double min, double max, bool *ok)
{
  bool converted = false;
  double result = value.toDouble(&converted);
  if (!converted) result = fallback;
  if (ok) *ok = converted;
  return qBound(min, result, max);
}

bool isNativeQuality(const QString &quality)
{
  static const QStringList qualities{QStringLiteral("1440p60"), QStringLiteral("1440p"),
                                     QStringLiteral("2160p60"), QStringLiteral("2160p"),
                                     QStringLiteral("1080p60"), QStringLiteral("1080p"),
                                     QStringLiteral("720p60"), QStringLiteral("720p"),
                                     QStringLiteral("480p60"), QStringLiteral("480p"),
                                      QStringLiteral("360p60"), QStringLiteral("360p"), QStringLiteral("160p"),
                                      QStringLiteral("audio_only")};
  return qualities.contains(quality);
}
}

PreferencesService::PreferencesService(XdgPaths paths, QObject *parent)
    : QObject(parent), m_paths(std::move(paths)), m_values(defaults())
{
  m_saveTimer.setSingleShot(true);
  m_saveTimer.setInterval(250);
  connect(&m_saveTimer, &QTimer::timeout, this, &PreferencesService::saveNow);
}

QVariantMap PreferencesService::values() const { return m_values; }

bool PreferencesService::dirty() const { return m_dirty; }

QVariant PreferencesService::get(const QString &key) const { return m_values.value(key); }

void PreferencesService::set(const QString &key, const QVariant &value)
{
  bool ok = false;
  const QVariant sanitized = sanitizeValue(key, value, &ok);
  if (!ok) {
    emit warning(tr("Ignored invalid preference '%1'.").arg(key));
    return;
  }
  if (m_values.value(key) == sanitized) return;
  m_values.insert(key, sanitized);
  emit valuesChanged();
  markDirty();
}

bool PreferencesService::load()
{
  m_paths.ensureAll();
  QFile file(m_paths.preferencesFile());
  if (!file.exists()) {
    m_values = defaults();
    return true;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    emit warning(tr("Could not read preferences: %1").arg(file.errorString()));
    return false;
  }
  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    emit warning(tr("Preferences are invalid JSON; defaults were used."));
    m_values = defaults();
    return false;
  }
  bool ok = false;
  m_values = sanitizeObject(document.object(), &ok);
  emit valuesChanged();
  return ok;
}

void PreferencesService::saveNow()
{
  if (!m_dirty) return;
  QJsonObject object = QJsonObject::fromVariantMap(m_values);
  object.insert(QStringLiteral("schemaVersion"), kSchemaVersion);
  if (writeAtomic(m_paths.preferencesFile(), object)) {
    m_dirty = false;
    emit dirtyChanged();
  }
}

QVariantMap PreferencesService::defaults() const
{
  return {
      {QStringLiteral("schemaVersion"), kSchemaVersion},
      {QStringLiteral("playerMode"), QStringLiteral("native")},
      {QStringLiteral("nativeQuality"), QString()},
      {QStringLiteral("chatVisible"), true},
      {QStringLiteral("chatPlacement"), QStringLiteral("right")},
      {QStringLiteral("sideChatWidth"), 380},
      {QStringLiteral("overlayChatX"), 24},
      {QStringLiteral("overlayChatY"), 24},
      {QStringLiteral("overlayChatWidth"), 360},
      {QStringLiteral("overlayChatHeight"), 520},
      {QStringLiteral("overlayChatOpacity"), 0.72},
      {QStringLiteral("sidebarCollapsed"), false},
      {QStringLiteral("oledMode"), false},
      {QStringLiteral("chatFontSize"), 15},
      {QStringLiteral("chatEmoteSize"), 28},
      {QStringLiteral("historyLimit"), 500},
      {QStringLiteral("moderatedMessageStyle"), QStringLiteral("placeholder")},
      {QStringLiteral("mentionSound"), QStringLiteral("ping")},
      {QStringLiteral("audioCompression"), false},
      {QStringLiteral("nativeControlAutoHideMs"), 3000},
      {QStringLiteral("emotePickerWidth"), 420},
      {QStringLiteral("emotePickerHeight"), 520},
      {QStringLiteral("emoteFavorites"), QStringList{}},
      {QStringLiteral("miniPlayerX"), 32},
      {QStringLiteral("miniPlayerY"), 32},
      {QStringLiteral("miniPlayerWidth"), 384},
      {QStringLiteral("miniPlayerHeight"), 216},
      {QStringLiteral("lastWhatsNewVersion"), QString()},
  };
}

QVariant PreferencesService::sanitizeValue(const QString &key, const QVariant &value, bool *ok) const
{
  if (ok) *ok = true;
  const QVariantMap fallback = defaults();
  if (!fallback.contains(key)) {
    if (ok) *ok = false;
    return {};
  }
  if (key == QLatin1String("schemaVersion")) {
    return boundedInt(value, kSchemaVersion, 1, 999, ok);
  }
  if (key == QLatin1String("playerMode")) {
    const QString mode = value.toString();
    if (mode == QLatin1String("standard") || mode == QLatin1String("native")) return mode;
    if (ok) *ok = false;
    return fallback.value(key);
  }
  if (key == QLatin1String("chatPlacement")) {
    const QString placement = value.toString();
    if (placement == QLatin1String("left") || placement == QLatin1String("right")) return placement;
    if (ok) *ok = false;
    return fallback.value(key);
  }
  if (key == QLatin1String("moderatedMessageStyle")) {
    const QString style = value.toString();
    if (style == QLatin1String("placeholder") || style == QLatin1String("dimmed") || style == QLatin1String("hidden")) return style;
    if (ok) *ok = false;
    return fallback.value(key);
  }
  if (key == QLatin1String("nativeQuality")) {
    const QString quality = value.toString().trimmed().toLower();
    if (quality.isEmpty()) return fallback.value(key);
    if (quality == QLatin1String("source") || quality == QLatin1String("best")) return QString();
    if (quality == QLatin1String("1440p+")) return QStringLiteral("1440p60");
    if (isNativeQuality(quality)) return quality;
    if (ok) *ok = false;
    return fallback.value(key);
  }
  if (key == QLatin1String("mentionSound") || key == QLatin1String("lastWhatsNewVersion")) {
    return value.toString().left(128);
  }
  if (key == QLatin1String("chatVisible") || key == QLatin1String("sidebarCollapsed") ||
      key == QLatin1String("oledMode") || key == QLatin1String("audioCompression")) {
    return value.toBool();
  }
  if (key == QLatin1String("sideChatWidth")) return boundedInt(value, fallback.value(key).toInt(), 300, 620, ok);
  if (key == QLatin1String("overlayChatWidth")) return boundedInt(value, fallback.value(key).toInt(), 280, 560, ok);
  if (key == QLatin1String("overlayChatHeight")) return boundedInt(value, fallback.value(key).toInt(), 200, 1000, ok);
  if (key == QLatin1String("chatFontSize")) return boundedInt(value, fallback.value(key).toInt(), 14, 25, ok);
  if (key == QLatin1String("chatEmoteSize")) return boundedInt(value, fallback.value(key).toInt(), 18, 48, ok);
  if (key == QLatin1String("historyLimit")) return boundedInt(value, fallback.value(key).toInt(), 20, 1500, ok);
  if (key == QLatin1String("nativeControlAutoHideMs")) return boundedInt(value, fallback.value(key).toInt(), 1000, 10000, ok);
  if (key == QLatin1String("emotePickerWidth")) return boundedInt(value, fallback.value(key).toInt(), 330, 600, ok);
  if (key == QLatin1String("emotePickerHeight")) return boundedInt(value, fallback.value(key).toInt(), 360, 700, ok);
  if (key == QLatin1String("miniPlayerWidth")) return boundedInt(value, fallback.value(key).toInt(), 240, 720, ok);
  if (key == QLatin1String("miniPlayerHeight")) return boundedInt(value, fallback.value(key).toInt(), 135, 480, ok);
  if (key == QLatin1String("overlayChatX") || key == QLatin1String("overlayChatY") ||
      key == QLatin1String("miniPlayerX") || key == QLatin1String("miniPlayerY")) {
    return boundedInt(value, fallback.value(key).toInt(), 0, 10000, ok);
  }
  if (key == QLatin1String("overlayChatOpacity")) return boundedDouble(value, fallback.value(key).toDouble(), 0.25, 1.0, ok);
  if (key == QLatin1String("emoteFavorites")) {
    const QStringList favorites = value.toStringList();
    QStringList bounded;
    for (const QString &favorite : favorites) {
      if (!favorite.isEmpty() && bounded.size() < 256) bounded.push_back(favorite.left(128));
    }
    return bounded;
  }
  if (ok) *ok = false;
  return {};
}

QVariantMap PreferencesService::sanitizeObject(const QJsonObject &object, bool *ok) const
{
  QVariantMap sanitized = defaults();
  bool allOk = true;
  for (auto it = object.begin(); it != object.end(); ++it) {
    if (it.key() == QLatin1String("showTimestamps") || it.key() == QLatin1String("mentionVolume")) continue;
    bool valueOk = false;
    QVariant value = sanitizeValue(it.key(), it.value().toVariant(), &valueOk);
    if (valueOk) sanitized.insert(it.key(), value);
    else allOk = false;
  }
  if (ok) *ok = allOk;
  return sanitized;
}

bool PreferencesService::writeAtomic(const QString &path, const QJsonObject &object)
{
  QDir().mkpath(QFileInfo(path).absolutePath());
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    emit warning(tr("Could not write %1: %2").arg(path, file.errorString()));
    return false;
  }
  file.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
  if (!file.commit()) {
    emit warning(tr("Could not commit %1: %2").arg(path, file.errorString()));
    return false;
  }
  QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  return true;
}

void PreferencesService::markDirty()
{
  if (!m_dirty) {
    m_dirty = true;
    emit dirtyChanged();
  }
  m_saveTimer.start();
}
