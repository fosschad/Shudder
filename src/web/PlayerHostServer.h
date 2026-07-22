#pragma once

#include <QByteArray>
#include <QHash>
#include <QTcpServer>
#include <QUrl>

class PlayerHostServer : public QTcpServer {
  Q_OBJECT
  Q_PROPERTY(QUrl baseUrl READ baseUrl NOTIFY baseUrlChanged)
  Q_PROPERTY(QString nonce READ nonce CONSTANT)

public:
  explicit PlayerHostServer(QObject *parent = nullptr);

  bool start();
  [[nodiscard]] QUrl baseUrl() const;
  [[nodiscard]] QString nonce() const;
  [[nodiscard]] QUrl playerUrl(const QString &channel) const;

signals:
  void baseUrlChanged();

protected:
  void incomingConnection(qintptr socketDescriptor) override;

private:
  QString m_nonce;
  QUrl m_baseUrl;

  [[nodiscard]] QByteArray responseForPath(const QString &path, const QHash<QString, QString> &query) const;
  [[nodiscard]] QByteArray playerDocument(const QString &channel) const;
};
