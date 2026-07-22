#pragma once

#include <QDir>
#include <QString>

class XdgPaths {
public:
  explicit XdgPaths(QString directoryName = QStringLiteral("shudder"));

  [[nodiscard]] QString configDirectory() const;
  [[nodiscard]] QString dataDirectory() const;
  [[nodiscard]] QString cacheDirectory() const;
  [[nodiscard]] QString stateDirectory() const;
  [[nodiscard]] QString runtimeDirectory() const;

  [[nodiscard]] QString preferencesFile() const;

  bool ensureAll() const;

private:
  QString m_directoryName;

  [[nodiscard]] QString xdgDirectory(const char *environmentVariable, const QString &fallback) const;
  [[nodiscard]] QString homeFallback(const QString &relativePath) const;
};
