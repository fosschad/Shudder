#include "web/PlayerHostServer.h"

#include "shudder_config.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

PlayerHostServer::PlayerHostServer(QObject *parent) : QTcpServer(parent)
{
  QByteArray random(32, Qt::Uninitialized);
  QRandomGenerator::global()->fillRange(reinterpret_cast<quint32 *>(random.data()), random.size() / int(sizeof(quint32)));
  m_nonce = QString::fromLatin1(QCryptographicHash::hash(random, QCryptographicHash::Sha256).toHex());
}

bool PlayerHostServer::start()
{
  if (isListening()) return true;
  if (!listen(QHostAddress::LocalHost, 0)) return false;
  m_baseUrl = QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(serverPort()));
  emit baseUrlChanged();
  return true;
}

QUrl PlayerHostServer::baseUrl() const { return m_baseUrl; }
QString PlayerHostServer::nonce() const { return m_nonce; }

QUrl PlayerHostServer::playerUrl(const QString &channel) const
{
  QUrl url = m_baseUrl.resolved(QUrl(QStringLiteral("player")));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("channel"), channel.toLower());
  query.addQueryItem(QStringLiteral("nonce"), m_nonce);
  url.setQuery(query);
  return url;
}

void PlayerHostServer::incomingConnection(qintptr socketDescriptor)
{
  auto *socket = new QTcpSocket(this);
  if (!socket->setSocketDescriptor(socketDescriptor)) {
    socket->deleteLater();
    return;
  }
  auto *timeout = new QTimer(socket);
  timeout->setSingleShot(true);
  timeout->setInterval(5000);
  connect(timeout, &QTimer::timeout, socket, &QTcpSocket::disconnectFromHost);
  timeout->start();
  connect(socket, &QTcpSocket::readyRead, this, [this, socket, timeout]() {
    const QByteArray request = socket->readAll();
    timeout->stop();
    if (request.size() > 8192) {
      socket->write("HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\n\r\n");
      socket->disconnectFromHost();
      return;
    }
    const QList<QByteArray> lines = request.split('\n');
    if (lines.isEmpty()) {
      socket->disconnectFromHost();
      return;
    }
    const QList<QByteArray> first = lines.first().trimmed().split(' ');
    if (first.size() < 2 || (first.at(0) != "GET" && first.at(0) != "HEAD")) {
      socket->write("HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n");
      socket->disconnectFromHost();
      return;
    }
    const QUrl requestUrl(QString::fromLatin1(first.at(1)));
    QHash<QString, QString> query;
    const QUrlQuery parsedQuery(requestUrl);
    for (const auto &pair : parsedQuery.queryItems()) query.insert(pair.first, pair.second);
    const QByteArray body = responseForPath(requestUrl.path(), query);
    const bool ok = !body.isEmpty();
    QByteArray headers = ok ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 404 Not Found\r\n";
    headers += "Connection: close\r\n";
    headers += "Content-Type: text/html; charset=utf-8\r\n";
    headers += "Referrer-Policy: no-referrer\r\n";
    headers += "X-Content-Type-Options: nosniff\r\n";
    headers += "Permissions-Policy: camera=(), microphone=(), geolocation=(), midi=(), usb=(), serial=()\r\n";
    headers += "Content-Security-Policy: default-src 'none'; frame-src https://player.twitch.tv https://www.twitch.tv; img-src https: data:; style-src 'unsafe-inline'; script-src 'nonce-" + m_nonce.toLatin1() + "'; connect-src https://*.twitch.tv https://*.twitchcdn.net; base-uri 'none'; form-action 'none'\r\n";
    headers += "Content-Length: " + QByteArray::number(ok ? body.size() : 0) + "\r\n\r\n";
    socket->write(headers);
    if (first.at(0) == "GET" && ok) socket->write(body);
    socket->disconnectFromHost();
  });
  connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
}

QByteArray PlayerHostServer::responseForPath(const QString &path, const QHash<QString, QString> &query) const
{
  if (path != QLatin1String("/player") || query.value(QStringLiteral("nonce")) != m_nonce) return {};
  const QString channel = query.value(QStringLiteral("channel")).toLower();
  if (!channel.contains(QRegularExpression(QStringLiteral("^[a-z0-9_]{3,25}$")))) return {};
  return playerDocument(channel);
}

QByteArray PlayerHostServer::playerDocument(const QString &channel) const
{
  const QString html = QStringLiteral(R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Shudder Standard Twitch Player</title>
<style>html,body,#root{margin:0;width:100%;height:100%;overflow:hidden;background:#020103;color:#f8fafc;font-family:sans-serif}</style>
</head>
<body>
<iframe id="root" title="Twitch player" allow="autoplay; encrypted-media; fullscreen; picture-in-picture" allowfullscreen sandbox="allow-scripts allow-same-origin allow-presentation" src="https://player.twitch.tv/?channel=%1&parent=127.0.0.1&muted=false"></iframe>
<script nonce="%2">
window.addEventListener('keydown', event => { if (event.key === 'Escape') window.parent.postMessage({type:'shudder-escape'}, '*'); });
</script>
</body>
</html>)HTML").arg(channel, m_nonce);
  return html.toUtf8();
}
