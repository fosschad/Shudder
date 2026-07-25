#include "playback/PlayerController.h"
#include "playback/StreamlinkResolver.h"
#include "web/PlayerHostServer.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class PlaybackProcessTests final : public QObject {
  Q_OBJECT

private slots:
  void cancelledQualityLookupDoesNotRetry();
  void replacementRejectsOldQualityResult();
  void failedStartReportsImmediatelyOnce();
  void failedStartStillRunsPendingReplacement();
  void crashedEnhancedResolveRetriesWithoutFalseFailure();
  void qualityTimeoutIsTerminal();
  void controllerDoesNotStickWaitingForQualities();
  void automaticQualityDoesNotLeakAcrossChannels();

private:
  static QString writeScript(const QTemporaryDir &directory, const QByteArray &body)
  {
    const QString path = directory.filePath(QStringLiteral("streamlink-test"));
    QFile script(path);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) return {};
    script.write("#!/bin/sh\nset -eu\n");
    script.write(body);
    script.close();
    if (!script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)) return {};
    return path;
  }

  static QStringList logLines(const QString &path)
  {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  }
};

void PlaybackProcessTests::cancelledQualityLookupDoesNotRetry()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString log = directory.filePath(QStringLiteral("calls.log"));
  const QString script = writeScript(directory, QStringLiteral("printf '%s\\n' \"$*\" >> '%1'\nsleep 2\nprintf '{\"streams\":{\"720p\":{}}}\\n'\n").arg(log).toUtf8());
  QVERIFY(!script.isEmpty());
  qputenv("SHUDDER_STREAMLINK_PATH", script.toUtf8());

  StreamlinkResolver resolver;
  QSignalSpy resolved(&resolver, &StreamlinkResolver::qualitiesResolved);
  QSignalSpy failed(&resolver, &StreamlinkResolver::qualitiesFailed);
  resolver.requestQualities(QStringLiteral("alpha"));
  QTRY_COMPARE_WITH_TIMEOUT(logLines(log).size(), 1, 1000);
  resolver.cancel();
  QTest::qWait(250);
  QCOMPARE(logLines(log).size(), 1);
  QCOMPARE(resolved.count(), 0);
  QCOMPARE(failed.count(), 0);
}

void PlaybackProcessTests::replacementRejectsOldQualityResult()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString log = directory.filePath(QStringLiteral("calls.log"));
  const QString script = writeScript(directory, QStringLiteral("printf '%s\\n' \"$*\" >> '%1'\ncase \"$*\" in *alpha*) sleep 2 ;; *) sleep 0.05 ;; esac\nprintf '{\"streams\":{\"720p\":{}}}\\n'\n").arg(log).toUtf8());
  QVERIFY(!script.isEmpty());
  qputenv("SHUDDER_STREAMLINK_PATH", script.toUtf8());

  StreamlinkResolver resolver;
  QSignalSpy resolved(&resolver, &StreamlinkResolver::qualitiesResolved);
  QSignalSpy failed(&resolver, &StreamlinkResolver::qualitiesFailed);
  resolver.requestQualities(QStringLiteral("alpha"));
  QTRY_COMPARE_WITH_TIMEOUT(logLines(log).size(), 1, 1000);
  resolver.requestQualities(QStringLiteral("beta"));
  QTRY_COMPARE_WITH_TIMEOUT(resolved.count(), 1, 2000);
  QCOMPARE(resolved.first().at(0).toString(), QStringLiteral("beta"));
  QCOMPARE(logLines(log).size(), 2);
  QCOMPARE(failed.count(), 0);
}

void PlaybackProcessTests::failedStartReportsImmediatelyOnce()
{
  qputenv("SHUDDER_STREAMLINK_PATH", QByteArrayLiteral("/definitely/not/a/streamlink-executable"));
  StreamlinkResolver resolver;
  QSignalSpy failed(&resolver, &StreamlinkResolver::qualitiesFailed);
  resolver.requestQualities(QStringLiteral("alpha"));
  QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
  QTest::qWait(100);
  QCOMPARE(failed.count(), 1);
}

void PlaybackProcessTests::failedStartStillRunsPendingReplacement()
{
  qputenv("SHUDDER_STREAMLINK_PATH", QByteArrayLiteral("/definitely/not/a/streamlink-executable"));
  StreamlinkResolver resolver;
  QSignalSpy failed(&resolver, &StreamlinkResolver::qualitiesFailed);
  resolver.requestQualities(QStringLiteral("alpha"));
  resolver.requestQualities(QStringLiteral("beta"));
  QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
  QCOMPARE(failed.first().at(0).toString(), QStringLiteral("beta"));
}

void PlaybackProcessTests::crashedEnhancedResolveRetriesWithoutFalseFailure()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString script = writeScript(directory, QByteArrayLiteral("case \"$*\" in *--twitch-supported-codecs*) kill -SEGV $$ ;; *) printf 'https://example.invalid/live.m3u8\\n' ;; esac\n"));
  QVERIFY(!script.isEmpty());
  qputenv("SHUDDER_STREAMLINK_PATH", script.toUtf8());

  StreamlinkResolver resolver;
  QSignalSpy resolved(&resolver, &StreamlinkResolver::resolved);
  QSignalSpy failed(&resolver, &StreamlinkResolver::failed);
  resolver.resolve(QStringLiteral("alpha"));
  QTRY_COMPARE_WITH_TIMEOUT(resolved.count(), 1, 2000);
  QCOMPARE(failed.count(), 0);
}

void PlaybackProcessTests::qualityTimeoutIsTerminal()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString log = directory.filePath(QStringLiteral("calls.log"));
  const QString script = writeScript(directory, QStringLiteral("printf '%s\\n' \"$*\" >> '%1'\nsleep 2\n").arg(log).toUtf8());
  QVERIFY(!script.isEmpty());
  qputenv("SHUDDER_STREAMLINK_PATH", script.toUtf8());

  StreamlinkResolver resolver;
  resolver.m_qualityTimeout.setInterval(500);
  QSignalSpy failed(&resolver, &StreamlinkResolver::qualitiesFailed);
  resolver.requestQualities(QStringLiteral("alpha"));
  QTRY_COMPARE_WITH_TIMEOUT(logLines(log).size(), 1, 1000);
  resolver.m_qualityTimeout.start(50);
  QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
  QTest::qWait(200);
  QCOMPARE(failed.count(), 1);
  QCOMPARE(logLines(log).size(), 1);
}

void PlaybackProcessTests::controllerDoesNotStickWaitingForQualities()
{
  qputenv("SHUDDER_STREAMLINK_PATH", QByteArrayLiteral("/definitely/not/a/streamlink-executable"));
  PlayerHostServer host;
  PlayerController controller(&host);
  controller.playChannel({{QStringLiteral("login"), QStringLiteral("alpha")}});
  QTRY_VERIFY_WITH_TIMEOUT(!controller.status().startsWith(QStringLiteral("Opening")), 1000);
  QVERIFY(controller.status().contains(QStringLiteral("failed to start"), Qt::CaseInsensitive));
  QVERIFY(!controller.m_waitingForQualities);
  QVERIFY(controller.nativeSource().isEmpty());
  controller.stop();
  QVERIFY(!controller.m_waitingForQualities);
}

void PlaybackProcessTests::automaticQualityDoesNotLeakAcrossChannels()
{
  PlayerHostServer host;
  PlayerController controller(&host);
  controller.m_autoQuality = true;
  controller.m_quality = QStringLiteral("1080p60");
  controller.m_qualityOptions.clear();
  QCOMPARE(controller.effectiveQualityForResolve(), QStringLiteral("best"));
  controller.m_qualityOptions = {QStringLiteral("720p60"), QStringLiteral("480p")};
  QCOMPARE(controller.effectiveQualityForResolve(), QStringLiteral("720p60"));
}

QTEST_MAIN(PlaybackProcessTests)
#include "tst_playback_process.moc"
