#pragma once

#include <QObject>
#include <QString>
#include <QThread>

#include <functional>

class ShudderSecretStore : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool available READ available NOTIFY availabilityChanged)

public:
  explicit ShudderSecretStore(QObject *parent = nullptr);
  ~ShudderSecretStore() override;
  [[nodiscard]] bool available() const;

  Q_INVOKABLE bool store(const QString &kind, const QString &secret);
  Q_INVOKABLE void storeAsync(const QString &kind, const QString &secret);
  Q_INVOKABLE QString load(const QString &kind) const;
  Q_INVOKABLE bool clear(const QString &kind);

signals:
  void availabilityChanged();
  void error(const QString &message) const;

private:
  using StoreFunction = std::function<bool(const QString &, const QString &, QString *)>;
  using LoadFunction = std::function<QString(const QString &, QString *)>;
  using ClearFunction = std::function<bool(const QString &, QString *)>;

  bool m_available = false;
  QThread m_workerThread;
  QObject *m_workerContext = nullptr;
  StoreFunction m_storeFunction;
  LoadFunction m_loadFunction;
  ClearFunction m_clearFunction;

  friend class SecretStoreTests;
};
