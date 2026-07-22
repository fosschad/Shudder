#pragma once

#include "core/XdgPaths.h"

#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

class PreferencesService : public QObject {
  Q_OBJECT
  Q_PROPERTY(QVariantMap values READ values NOTIFY valuesChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY dirtyChanged)

public:
  explicit PreferencesService(XdgPaths paths, QObject *parent = nullptr);

  [[nodiscard]] QVariantMap values() const;
  [[nodiscard]] bool dirty() const;

  Q_INVOKABLE QVariant get(const QString &key) const;
  Q_INVOKABLE void set(const QString &key, const QVariant &value);
  Q_INVOKABLE void saveNow();

  bool load();

signals:
  void valuesChanged();
  void dirtyChanged();
  void warning(const QString &message);

private:
  XdgPaths m_paths;
  QVariantMap m_values;
  bool m_dirty = false;
  QTimer m_saveTimer;

  [[nodiscard]] QVariantMap defaults() const;
  [[nodiscard]] QVariant sanitizeValue(const QString &key, const QVariant &value, bool *ok = nullptr) const;
  [[nodiscard]] QVariantMap sanitizeObject(const QJsonObject &object, bool *ok = nullptr) const;
  bool writeAtomic(const QString &path, const QJsonObject &object);
  void markDirty();
};
