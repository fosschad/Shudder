#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

#include <memory>

class StandardPlayerTests final : public QObject {
  Q_OBJECT

private slots:
  void resizeAndLifecycleStress();
};

void StandardPlayerTests::resizeAndLifecycleStress()
{
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"QML(
    import QtQuick
    import QtQuick.Window
    import QtWebEngine
    Window {
      id: root
      width: 640
      height: 360
      visible: true
      property int pageStatus: -1
      WebEngineView {
        id: web
        objectName: "standardPlayerWebView"
        anchors.fill: parent
        url: "data:text/html,<html><body style='background:%23101010;color:white'>Shudder Standard Player</body></html>"
        onLoadingChanged: function(request) { root.pageStatus = request.status }
      }
    }
  )QML", QUrl(QStringLiteral("inmemory:/StandardPlayerStress.qml")));
  QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 10000);
  QVERIFY2(component.status() == QQmlComponent::Ready, qPrintable(component.errorString()));
  std::unique_ptr<QObject> root(component.create());
  QVERIFY2(root, qPrintable(component.errorString()));
  auto *window = qobject_cast<QQuickWindow *>(root.get());
  QVERIFY(window);
  QObject *webView = window->findChild<QObject *>(QStringLiteral("standardPlayerWebView"));
  QVERIFY(webView);
  QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 5000);
  QTRY_COMPARE_WITH_TIMEOUT(window->property("pageStatus").toInt(), 2, 10000);
  const QUrl source = webView->property("url").toUrl();

  for (int cycle = 0; cycle < 120; ++cycle) {
    window->resize(480 + (cycle * 37) % 520, 270 + (cycle * 29) % 360);
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QCOMPARE(webView->property("url").toUrl(), source);
  }

  window->showMaximized();
  QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::Maximized, 5000);
  window->showNormal();
  QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::Windowed, 5000);
  window->showFullScreen();
  QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::FullScreen, 5000);
  window->showNormal();
  QTRY_COMPARE_WITH_TIMEOUT(window->visibility(), QWindow::Windowed, 5000);
  window->hide();
  window->show();
  QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 5000);
  QCOMPARE(webView->property("url").toUrl(), source);
  window->close();
  root.reset();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents();
}

int main(int argc, char **argv)
{
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QtWebEngineQuick::initialize();
  QGuiApplication application(argc, argv);
  StandardPlayerTests tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "tst_standard_player.moc"
