#include "playback/MpvVideoItem.h"

#include <QGuiApplication>
#include <QPainter>
#include <QQuickPaintedItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>

#include <clocale>
#include <cstdlib>

namespace {
class RenderSentinel final : public QQuickPaintedItem {
public:
  using QQuickPaintedItem::QQuickPaintedItem;
  void paint(QPainter *painter) override { painter->fillRect(boundingRect(), QColor(24, 220, 96)); }
};
}

class MpvVideoItemTests final : public QObject {
  Q_OBJECT

private slots:
  void resizeAndLifecycleStress();
};

void MpvVideoItemTests::resizeAndLifecycleStress()
{
  QQuickWindow window;
  window.resize(640, 360);
  auto *item = new MpvVideoItem(window.contentItem());
  item->setSize(QSizeF(window.size()));
  item->setSource(QStringLiteral("av://lavfi:testsrc=size=320x180:rate=30"));
  auto *sentinel = new RenderSentinel(window.contentItem());
  sentinel->setSize(QSizeF(32, 32));
  sentinel->setZ(10);
  window.show();

  QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 5000);
  QTRY_VERIFY_WITH_TIMEOUT(item->renderReady(), 5000);
  QTRY_VERIFY_WITH_TIMEOUT(item->renderedFrameCount() > 2, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(item->videoWidth(), 320, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(item->videoHeight(), 180, 5000);
  QCOMPARE(item->renderContextGeneration(), 1);
  QCOMPARE(item->sourceCommandCount(), quint64(1));
  const quint64 initialFrames = item->renderedFrameCount();

  for (int cycle = 0; cycle < 120; ++cycle) {
    const QSize size(480 + (cycle * 37) % 520, 270 + (cycle * 29) % 360);
    window.resize(size);
    item->setSize(QSizeF(size));
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QCOMPARE(item->source(), QStringLiteral("av://lavfi:testsrc=size=320x180:rate=30"));
    QVERIFY(item->renderReady());
    QCOMPARE(item->renderContextGeneration(), 1);
  }

  QTRY_VERIFY_WITH_TIMEOUT(item->renderedFrameCount() > initialFrames, 5000);
  const QSize target = item->renderTargetSize();
  QVERIFY(target.width() > 0);
  QVERIFY(target.height() > 0);
  const qreal dpr = window.effectiveDevicePixelRatio();
  QVERIFY(qAbs(target.width() - qRound(item->width() * dpr)) <= 1);
  QVERIFY(qAbs(target.height() - qRound(item->height() * dpr)) <= 1);
  const QImage frame = window.grabWindow();
  QVERIFY(!frame.isNull());
  const QColor sentinelColor = frame.pixelColor(qRound(16 * dpr), qRound(16 * dpr));
  QVERIFY(sentinelColor.green() > 180);
  QVERIFY(sentinelColor.red() < 80);

  window.showMaximized();
  QTRY_COMPARE_WITH_TIMEOUT(window.visibility(), QWindow::Maximized, 5000);
  window.showNormal();
  QTRY_COMPARE_WITH_TIMEOUT(window.visibility(), QWindow::Windowed, 5000);
  window.showFullScreen();
  QTRY_COMPARE_WITH_TIMEOUT(window.visibility(), QWindow::FullScreen, 5000);
  window.showNormal();
  QTRY_COMPARE_WITH_TIMEOUT(window.visibility(), QWindow::Windowed, 5000);

  const quint64 sourceChanges = item->sourceCommandCount();
  item->setSource(QStringLiteral("av://lavfi:testsrc=size=160x90:rate=24"));
  item->setSource(QStringLiteral("av://lavfi:testsrc=size=640x360:rate=24"));
  item->setSource(QStringLiteral("av://lavfi:testsrc=size=256x144:rate=24"));
  QTRY_VERIFY_WITH_TIMEOUT(item->sourceCommandCount() > sourceChanges, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(item->videoWidth(), 256, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(item->videoHeight(), 144, 5000);

  const int contextGeneration = item->renderContextGeneration();
  const quint64 sourceCommands = item->sourceCommandCount();
  const quint64 framesBeforeRelease = item->renderedFrameCount();
  window.setPersistentGraphics(false);
  window.setPersistentSceneGraph(false);
  window.hide();
  window.releaseResources();
  window.destroy();
  QCoreApplication::processEvents();
  window.show();
  QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(), 5000);
  QTRY_VERIFY_WITH_TIMEOUT(item->renderReady(), 5000);
  if (item->renderContextGeneration() > contextGeneration)
    QTRY_VERIFY_WITH_TIMEOUT(item->sourceCommandCount() > sourceCommands, 5000);
  else {
    QCOMPARE(item->renderContextGeneration(), contextGeneration);
    QCOMPARE(item->sourceCommandCount(), sourceCommands);
  }
  QTRY_VERIFY_WITH_TIMEOUT(item->renderedFrameCount() > framesBeforeRelease, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(item->videoWidth(), 256, 5000);
  QTRY_COMPARE_WITH_TIMEOUT(item->videoHeight(), 144, 5000);
  delete sentinel;
  delete item;
  window.close();
  QCoreApplication::processEvents();
}

int main(int argc, char **argv)
{
  setenv("LC_NUMERIC", "C", 1);
  setlocale(LC_NUMERIC, "C");
  QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
  QGuiApplication application(argc, argv);
  MpvVideoItemTests tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "tst_mpv_video_item.moc"
