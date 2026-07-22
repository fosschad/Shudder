#include "core/XdgPaths.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

XdgPaths::XdgPaths(QString directoryName) : m_directoryName(std::move(directoryName)) {}

QString XdgPaths::configDirectory() const
{
  return xdgDirectory("XDG_CONFIG_HOME", homeFallback(QStringLiteral(".config")));
}

QString XdgPaths::dataDirectory() const
{
  return xdgDirectory("XDG_DATA_HOME", homeFallback(QStringLiteral(".local/share")));
}

QString XdgPaths::cacheDirectory() const
{
  return xdgDirectory("XDG_CACHE_HOME", homeFallback(QStringLiteral(".cache")));
}

QString XdgPaths::stateDirectory() const
{
  return xdgDirectory("XDG_STATE_HOME", homeFallback(QStringLiteral(".local/state")));
}

QString XdgPaths::runtimeDirectory() const
{
  const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
  if (!runtime.isEmpty()) {
    return QDir(QString::fromLocal8Bit(runtime)).filePath(m_directoryName);
  }
  return cacheDirectory();
}

QString XdgPaths::preferencesFile() const
{
  return QDir(configDirectory()).filePath(QStringLiteral("preferences.json"));
}

bool XdgPaths::ensureAll() const
{
  return QDir().mkpath(configDirectory()) && QDir().mkpath(dataDirectory()) &&
         QDir().mkpath(cacheDirectory()) && QDir().mkpath(stateDirectory()) &&
         QDir().mkpath(runtimeDirectory());
}

QString XdgPaths::xdgDirectory(const char *environmentVariable, const QString &fallback) const
{
  const QByteArray value = qgetenv(environmentVariable);
  const QString root = value.isEmpty() ? fallback : QString::fromLocal8Bit(value);
  return QDir(root).filePath(m_directoryName);
}

QString XdgPaths::homeFallback(const QString &relativePath) const
{
  const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  return QDir(home).filePath(relativePath);
}
