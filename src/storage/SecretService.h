#pragma once

#include <QObject>
#include <QString>

class ShudderSecretStore : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)

public:
  explicit ShudderSecretStore(QObject *parent = nullptr);
  [[nodiscard]] bool available() const;

  Q_INVOKABLE bool store(const QString &kind, const QString &secret);
  Q_INVOKABLE void storeAsync(const QString &kind, const QString &secret);
  Q_INVOKABLE QString load(const QString &kind) const;
  Q_INVOKABLE bool clear(const QString &kind);

signals:
  void availabilityChanged();
  void error(const QString &message) const;

private:
  bool m_available = false;
};
