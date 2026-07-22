#include <QFile>
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>
#include <QTimer>

class TabTransitionTests final : public QObject {
  Q_OBJECT

private slots:
  void persistentPagesKeepImmediateSurfacesAcrossSwitches();

private:
  static void collectItems(QQuickItem *item, const QString &objectName, QList<QQuickItem *> *matches)
  {
    if (!item) return;
    if (item->objectName() == objectName) matches->push_back(item);
    for (QQuickItem *child : item->childItems()) collectItems(child, objectName, matches);
  }

  static QList<QQuickItem *> itemsByName(QQuickItem *root, const QString &objectName)
  {
    QList<QQuickItem *> matches;
    collectItems(root, objectName, &matches);
    return matches;
  }

  static void drainEvents(int turns = 8)
  {
    for (int i = 0; i < turns; ++i) QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
  }
};

void TabTransitionTests::persistentPagesKeepImmediateSurfacesAcrossSwitches()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const QString qmlPath = dir.filePath(QStringLiteral("tab_harness.qml"));
  QFile file(qmlPath);
  QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
  QTextStream out(&file);
  const QString componentsImport = QUrl::fromLocalFile(QStringLiteral(SHUDDER_SOURCE_DIR "/qml/components")).toString();
  QTcpServer slowServer;
  QVERIFY(slowServer.listen(QHostAddress::LocalHost));
  const QByteArray png = QByteArray::fromBase64("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+/p9sAAAAASUVORK5CYII=");
  QObject::connect(&slowServer, &QTcpServer::newConnection, &slowServer, [&slowServer, png]() {
    while (QTcpSocket *socket = slowServer.nextPendingConnection()) {
      QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, png]() {
        socket->readAll();
        QTimer::singleShot(180, socket, [socket, png]() {
          socket->write("HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nCache-Control: max-age=3600\r\nContent-Length: ");
          socket->write(QByteArray::number(png.size()));
          socket->write("\r\n\r\n");
          socket->write(png);
          socket->disconnectFromHost();
        });
      });
      QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
  });
  const QString slowUrl = QStringLiteral("http://127.0.0.1:%1/slow.png").arg(slowServer.serverPort());
  out << R"QML(
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ")QML" << componentsImport << R"QML(" as Components

Item {
    id: root
    width: 900
    height: 620
    property string section: "home"
    property int homeConstructed: 0
    property int browseConstructed: 0
    Components.Theme { id: theme }
    Rectangle { anchors.fill: parent; color: theme.app }
    ListModel {
        id: homeModel
        ListElement { kind: "stream"; title: "Home A"; displayName: "Streamer A"; category: "Category"; categoryId: "1"; thumbnail: ")QML" << slowUrl << R"QML("; viewerCount: 10; uptime: "1m"; language: "en"; tags: "" }
        ListElement { kind: "stream"; title: "Home B"; displayName: "Streamer B"; category: "Category"; categoryId: "2"; thumbnail: "http://127.0.0.1:1/home-b.jpg"; viewerCount: 20; uptime: "2m"; language: "en"; tags: "" }
        ListElement { kind: "stream"; title: "Home C"; displayName: "Streamer C"; category: "Category"; categoryId: "3"; thumbnail: "http://127.0.0.1:1/home-c.jpg"; viewerCount: 30; uptime: "3m"; language: "en"; tags: "" }
    }
    ListModel {
        id: browseModel
        ListElement { kind: "category"; title: "Browse A"; displayName: ""; category: "Browse A"; categoryId: "10"; thumbnail: ")QML" << slowUrl << R"QML("; viewerCount: 100; uptime: ""; language: ""; tags: "" }
        ListElement { kind: "category"; title: "Browse B"; displayName: ""; category: "Browse B"; categoryId: "11"; thumbnail: "http://127.0.0.1:1/browse-b.jpg"; viewerCount: 200; uptime: ""; language: ""; tags: "" }
        ListElement { kind: "category"; title: "Browse C"; displayName: ""; category: "Browse C"; categoryId: "12"; thumbnail: "http://127.0.0.1:1/browse-c.jpg"; viewerCount: 300; uptime: ""; language: ""; tags: "" }
    }
    GridView {
        id: homePage
        objectName: "homePage"
        anchors.fill: parent
        visible: root.section === "home"
        model: homeModel
        cellWidth: 290
        cellHeight: 286
        reuseItems: true
        cacheBuffer: 320
        Component.onCompleted: ++root.homeConstructed
        delegate: Components.StreamCard { width: 278; height: 274; cardHoverEnabled: false }
    }
    GridView {
        id: browsePage
        objectName: "browsePage"
        anchors.fill: parent
        visible: root.section === "browse"
        model: browseModel
        cellWidth: 154
        cellHeight: 260
        reuseItems: true
        cacheBuffer: 320
        Component.onCompleted: ++root.browseConstructed
        delegate: Components.StreamCard { width: 142; height: 250; cardHoverEnabled: false }
    }
}
)QML";
  file.close();

  QQuickView view;
  QStringList warnings;
  QObject::connect(view.engine(), &QQmlEngine::warnings, &view, [&warnings](const QList<QQmlError> &items) {
    for (const QQmlError &warning : items) warnings.push_back(warning.toString());
  });
  view.setResizeMode(QQuickView::SizeRootObjectToView);
  view.setSource(QUrl::fromLocalFile(qmlPath));
  view.show();
  QVERIFY(view.status() == QQuickView::Ready);
  QVERIFY(view.rootObject());
  drainEvents(20);

  QCOMPARE(view.rootObject()->property("homeConstructed").toInt(), 1);
  QCOMPARE(view.rootObject()->property("browseConstructed").toInt(), 1);
  QVERIFY(!itemsByName(view.rootObject(), QStringLiteral("thumbnailSurface")).isEmpty());

  int maxCards = 0;
  for (int i = 0; i < 500; ++i) {
    view.rootObject()->setProperty("section", (i % 2) == 0 ? QStringLiteral("browse") : QStringLiteral("home"));
    drainEvents(2);
    maxCards = qMax(maxCards, itemsByName(view.rootObject(), QStringLiteral("streamCard")).size());
    QVERIFY(!itemsByName(view.rootObject(), QStringLiteral("thumbnailSurface")).isEmpty());
    QCOMPARE(view.rootObject()->property("homeConstructed").toInt(), 1);
    QCOMPARE(view.rootObject()->property("browseConstructed").toInt(), 1);
  }

  QVERIFY2(maxCards <= 18, qPrintable(QStringLiteral("unexpected delegate growth: %1").arg(maxCards)));
  for (const QString &warning : std::as_const(warnings)) {
    QVERIFY2(warning.contains(QStringLiteral("QQuickImage: Connection refused")), qPrintable(warning));
  }
}

int main(int argc, char **argv)
{
  qputenv("QML_DISABLE_DISK_CACHE", "1");
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  QQuickStyle::setStyle(QStringLiteral("Fusion"));
  TabTransitionTests tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "tst_tab_transitions.moc"
