#include "core/ExternalProcessEnvironment.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

namespace {

QString normalizedAbsolutePath(const QString &path)
{
  if (path.isEmpty() || !QDir::isAbsolutePath(path)) return {};
  const QString canonical = QFileInfo(path).canonicalFilePath();
  return canonical.isEmpty() ? QDir::cleanPath(path) : canonical;
}

bool isUnderRoot(const QString &path, const QString &root)
{
  return !root.isEmpty() && (path == root || path.startsWith(root + QLatin1Char('/')));
}

QStringList appImageRoots(const QProcessEnvironment &source)
{
  QStringList roots;
  const QString appDir = normalizedAbsolutePath(source.value(QStringLiteral("APPDIR")));
  if (!appDir.isEmpty()) roots.push_back(appDir);
  return roots;
}

bool isAppImagePath(const QString &path, const QStringList &roots)
{
  const QString absolute = QDir::isAbsolutePath(path) ? path : QFileInfo(path).absoluteFilePath();
  const QString normalized = normalizedAbsolutePath(absolute);
  if (normalized.isEmpty()) return false;
  for (const QString &root : roots) {
    if (isUnderRoot(normalized, root)) return true;
  }
  return false;
}

QString filteredPathList(const QString &value, const QStringList &roots)
{
  QStringList filtered;
  QSet<QString> seen;
  for (const QString &entry : value.split(QLatin1Char(':'), Qt::KeepEmptyParts)) {
    if (entry.isEmpty() || isAppImagePath(entry, roots)) continue;
    const QString canonical = QFileInfo(entry).canonicalFilePath();
    const QString key = canonical.isEmpty() ? (QDir::isAbsolutePath(entry) ? QDir::cleanPath(entry) : entry) : canonical;
    if (seen.contains(key)) continue;
    seen.insert(key);
    filtered.push_back(entry);
  }
  return filtered.join(QLatin1Char(':'));
}

void filterPathVariable(QProcessEnvironment &environment, const QString &name, const QStringList &roots)
{
  if (!environment.contains(name)) return;
  const QString filtered = filteredPathList(environment.value(name), roots);
  if (filtered.isEmpty()) environment.remove(name);
  else environment.insert(name, filtered);
}

void removeAppImageScalar(QProcessEnvironment &environment, const QString &name, const QStringList &roots)
{
  if (environment.contains(name) && isAppImagePath(environment.value(name), roots)) environment.remove(name);
}

} // namespace

namespace ExternalProcessEnvironment {

QProcessEnvironment forHostTool(const QProcessEnvironment &source)
{
  QProcessEnvironment environment = source;
  const QStringList roots = appImageRoots(source);

  const QString hostLibraryPathVariable = QStringLiteral("SHUDDER_APPIMAGE_HOST_LD_LIBRARY_PATH");
  if (!roots.isEmpty() && source.contains(hostLibraryPathVariable)) environment.insert(QStringLiteral("LD_LIBRARY_PATH"), source.value(hostLibraryPathVariable));
  environment.remove(hostLibraryPathVariable);

  filterPathVariable(environment, QStringLiteral("PATH"), roots);
  filterPathVariable(environment, QStringLiteral("LD_LIBRARY_PATH"), roots);
  filterPathVariable(environment, QStringLiteral("PYTHONPATH"), roots);
  filterPathVariable(environment, QStringLiteral("QT_PLUGIN_PATH"), roots);
  filterPathVariable(environment, QStringLiteral("QML2_IMPORT_PATH"), roots);
  filterPathVariable(environment, QStringLiteral("QML_IMPORT_PATH"), roots);

  if (environment.contains(QStringLiteral("LD_PRELOAD"))) {
    QStringList filtered;
    QSet<QString> seen;
    const QStringList entries = environment.value(QStringLiteral("LD_PRELOAD")).split(QRegularExpression(QStringLiteral("[\\s:]+")), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
      if (isAppImagePath(entry, roots)) continue;
      const QString canonical = QFileInfo(entry).canonicalFilePath();
      const QString key = canonical.isEmpty() ? (QDir::isAbsolutePath(entry) ? QDir::cleanPath(entry) : entry) : canonical;
      if (seen.contains(key)) continue;
      seen.insert(key);
      filtered.push_back(entry);
    }
    if (filtered.isEmpty()) environment.remove(QStringLiteral("LD_PRELOAD"));
    else environment.insert(QStringLiteral("LD_PRELOAD"), filtered.join(QLatin1Char(' ')));
  }

  if (environment.contains(QStringLiteral("PYTHONHOME"))) {
    const QStringList entries = environment.value(QStringLiteral("PYTHONHOME")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (const QString &entry : entries) {
      if (isAppImagePath(entry, roots)) {
        environment.remove(QStringLiteral("PYTHONHOME"));
        break;
      }
    }
  }

  removeAppImageScalar(environment, QStringLiteral("QTWEBENGINEPROCESS_PATH"), roots);
  removeAppImageScalar(environment, QStringLiteral("QTWEBENGINE_RESOURCES_PATH"), roots);
  removeAppImageScalar(environment, QStringLiteral("QTWEBENGINE_LOCALES_PATH"), roots);
  environment.remove(QStringLiteral("APPDIR"));
  environment.remove(QStringLiteral("APPIMAGE"));
  environment.remove(QStringLiteral("ARGV0"));
  return environment;
}

QString resolveHostExecutable(const QString &executable, const QProcessEnvironment &environment)
{
  const QString candidate = executable.trimmed();
  if (candidate.isEmpty()) return {};
  const QStringList roots = appImageRoots(QProcessEnvironment::systemEnvironment());

  if (candidate.contains(QLatin1Char('/'))) {
    const QFileInfo info(candidate);
    const QString absolute = info.absoluteFilePath();
    if (!info.isFile() || !info.isExecutable() || isAppImagePath(absolute, roots)) return {};
    return QDir::cleanPath(absolute);
  }

  const QStringList paths = environment.value(QStringLiteral("PATH")).split(QLatin1Char(':'), Qt::SkipEmptyParts);
  const QString resolved = QStandardPaths::findExecutable(candidate, paths);
  if (resolved.isEmpty() || isAppImagePath(resolved, roots)) return {};
  return resolved;
}

} // namespace ExternalProcessEnvironment
