#include "core/ExternalProcessEnvironment.h"
#include "playback/PlayerController.h"
#include "playback/StreamlinkResolver.h"
#include "web/PlayerHostServer.h"

#include <QFile>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
class ScopedEnvironment final {
public:
  void set(const QByteArray &name, const QByteArray &value)
  {
    Entry entry{name, qEnvironmentVariableIsSet(name.constData()), qgetenv(name.constData())};
    m_entries.push_back(entry);
    qputenv(name.constData(), value);
  }

  ~ScopedEnvironment()
  {
    for (auto it = m_entries.crbegin(); it != m_entries.crend(); ++it) {
      if (it->wasSet) qputenv(it->name.constData(), it->value);
      else qunsetenv(it->name.constData());
    }
  }

private:
  struct Entry {
    QByteArray name;
    bool wasSet;
    QByteArray value;
  };
  QList<Entry> m_entries;
};
}

class PlaybackProcessTests final : public QObject {
  Q_OBJECT

private slots:
  void hostToolEnvironmentSanitizesAppImagePaths();
  void hostToolEnvironmentPreservesHostValues();
  void streamlinkChildReceivesSanitizedEnvironment();
  void streamlinkErrorsAreClassified();
  void runtimeTracebackIsNotDisplayed();
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

void PlaybackProcessTests::hostToolEnvironmentSanitizesAppImagePaths()
{
  QProcessEnvironment source;
  source.insert(QStringLiteral("APPDIR"), QStringLiteral("/tmp/.mount_ShudderABC"));
  source.insert(QStringLiteral("APPIMAGE"), QStringLiteral("/home/user/Shudder.AppImage"));
  source.insert(QStringLiteral("ARGV0"), QStringLiteral("/home/user/Shudder.AppImage"));
  source.insert(QStringLiteral("PATH"), QStringLiteral(":/tmp/.mount_ShudderABC/usr/bin:/usr/bin:/usr/bin:/opt/tools:"));
  source.insert(QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("/tmp/.mount_ShudderABC/usr/lib"));
  source.insert(QStringLiteral("SHUDDER_APPIMAGE_HOST_LD_LIBRARY_PATH"), QStringLiteral(":/opt/host/lib:/opt/host/lib:/usr/local/lib:"));
  source.insert(QStringLiteral("LD_PRELOAD"), QStringLiteral("/opt/host/lib/keep.so /tmp/.mount_ShudderABC/usr/lib/libcrypto.so.3:/opt/host/lib/keep.so"));
  source.insert(QStringLiteral("PYTHONHOME"), QStringLiteral("/tmp/.mount_ShudderABC/usr"));
  source.insert(QStringLiteral("PYTHONPATH"), QStringLiteral("/tmp/.mount_ShudderABC/usr/python:/opt/host/python"));
  source.insert(QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral("/tmp/.mount_ShudderABC/usr/plugins"));
  source.insert(QStringLiteral("QML2_IMPORT_PATH"), QStringLiteral("/tmp/.mount_ShudderABC/usr/qml:/opt/host/qml"));
  source.insert(QStringLiteral("QML_IMPORT_PATH"), QStringLiteral("/tmp/.mount_ShudderABC/usr/qml"));
  source.insert(QStringLiteral("QTWEBENGINEPROCESS_PATH"), QStringLiteral("/tmp/.mount_ShudderABC/usr/libexec/QtWebEngineProcess"));

  const QProcessEnvironment sanitized = ExternalProcessEnvironment::forHostTool(source);
  QCOMPARE(sanitized.value(QStringLiteral("PATH")), QStringLiteral("/usr/bin:/opt/tools"));
  QCOMPARE(sanitized.value(QStringLiteral("LD_LIBRARY_PATH")), QStringLiteral("/opt/host/lib:/usr/local/lib"));
  QCOMPARE(sanitized.value(QStringLiteral("LD_PRELOAD")), QStringLiteral("/opt/host/lib/keep.so"));
  QVERIFY(!sanitized.contains(QStringLiteral("PYTHONHOME")));
  QCOMPARE(sanitized.value(QStringLiteral("PYTHONPATH")), QStringLiteral("/opt/host/python"));
  QVERIFY(!sanitized.contains(QStringLiteral("QT_PLUGIN_PATH")));
  QCOMPARE(sanitized.value(QStringLiteral("QML2_IMPORT_PATH")), QStringLiteral("/opt/host/qml"));
  QVERIFY(!sanitized.contains(QStringLiteral("QML_IMPORT_PATH")));
  QVERIFY(!sanitized.contains(QStringLiteral("QTWEBENGINEPROCESS_PATH")));
  QVERIFY(!sanitized.contains(QStringLiteral("APPDIR")));
  QVERIFY(!sanitized.contains(QStringLiteral("APPIMAGE")));
  QVERIFY(!sanitized.contains(QStringLiteral("ARGV0")));
  QVERIFY(!sanitized.contains(QStringLiteral("SHUDDER_APPIMAGE_HOST_LD_LIBRARY_PATH")));
}

void PlaybackProcessTests::hostToolEnvironmentPreservesHostValues()
{
  QProcessEnvironment source;
  source.insert(QStringLiteral("LD_LIBRARY_PATH"), QStringLiteral("/tmp/.mount_Unexpected/usr/lib:/opt/host/lib::/opt/host/lib"));
  source.insert(QStringLiteral("PYTHONHOME"), QStringLiteral("/opt/host/python"));
  source.insert(QStringLiteral("PYTHONPATH"), QStringLiteral("/opt/host/python/modules"));
  source.insert(QStringLiteral("QT_PLUGIN_PATH"), QStringLiteral("/opt/host/qt/plugins"));
  source.insert(QStringLiteral("HTTPS_PROXY"), QStringLiteral("http://proxy.invalid:8080"));

  const QProcessEnvironment sanitized = ExternalProcessEnvironment::forHostTool(source);
  QCOMPARE(sanitized.value(QStringLiteral("LD_LIBRARY_PATH")), QStringLiteral("/tmp/.mount_Unexpected/usr/lib:/opt/host/lib"));
  QCOMPARE(sanitized.value(QStringLiteral("PYTHONHOME")), QStringLiteral("/opt/host/python"));
  QCOMPARE(sanitized.value(QStringLiteral("PYTHONPATH")), QStringLiteral("/opt/host/python/modules"));
  QCOMPARE(sanitized.value(QStringLiteral("QT_PLUGIN_PATH")), QStringLiteral("/opt/host/qt/plugins"));
  QCOMPARE(sanitized.value(QStringLiteral("HTTPS_PROXY")), QStringLiteral("http://proxy.invalid:8080"));
  QCOMPARE(source.value(QStringLiteral("LD_LIBRARY_PATH")), QStringLiteral("/tmp/.mount_Unexpected/usr/lib:/opt/host/lib::/opt/host/lib"));
}

void PlaybackProcessTests::streamlinkChildReceivesSanitizedEnvironment()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString appDir = directory.filePath(QStringLiteral("appdir"));
  const QString log = directory.filePath(QStringLiteral("environment.log"));
  const QString script = writeScript(directory, QStringLiteral(
      "printf 'PATH=%s\\n' \"${PATH-unset}\" > '%1'\n"
      "printf 'LD_LIBRARY_PATH=%s\\n' \"${LD_LIBRARY_PATH-unset}\" >> '%1'\n"
      "printf 'LD_PRELOAD=%s\\n' \"${LD_PRELOAD-unset}\" >> '%1'\n"
      "printf 'PYTHONHOME=%s\\n' \"${PYTHONHOME-unset}\" >> '%1'\n"
      "printf 'PYTHONPATH=%s\\n' \"${PYTHONPATH-unset}\" >> '%1'\n"
      "printf 'QT_PLUGIN_PATH=%s\\n' \"${QT_PLUGIN_PATH-unset}\" >> '%1'\n"
      "printf 'QML2_IMPORT_PATH=%s\\n' \"${QML2_IMPORT_PATH-unset}\" >> '%1'\n"
      "printf 'APPDIR=%s\\n' \"${APPDIR-unset}\" >> '%1'\n"
      "printf 'https://example.invalid/live.m3u8\\n'\n").arg(log).toUtf8());
  QVERIFY(!script.isEmpty());

  ScopedEnvironment environment;
  environment.set("SHUDDER_STREAMLINK_PATH", script.toUtf8());
  environment.set("APPDIR", appDir.toUtf8());
  environment.set("APPIMAGE", QByteArrayLiteral("/home/user/Shudder.AppImage"));
  environment.set("ARGV0", QByteArrayLiteral("/home/user/Shudder.AppImage"));
  environment.set("PATH", (appDir + QStringLiteral("/usr/bin:/usr/bin")).toUtf8());
  environment.set("LD_LIBRARY_PATH", (appDir + QStringLiteral("/usr/lib:/opt/host/lib")).toUtf8());
  environment.set("LD_PRELOAD", (appDir + QStringLiteral("/usr/lib/libcrypto.so.3")).toUtf8());
  environment.set("PYTHONHOME", (appDir + QStringLiteral("/usr")).toUtf8());
  environment.set("PYTHONPATH", (appDir + QStringLiteral("/usr/python")).toUtf8());
  environment.set("QT_PLUGIN_PATH", (appDir + QStringLiteral("/usr/plugins")).toUtf8());
  environment.set("QML2_IMPORT_PATH", (appDir + QStringLiteral("/usr/qml")).toUtf8());

  StreamlinkResolver resolver;
  QCOMPARE(resolver.streamlinkPath(), script);
  QSignalSpy resolved(&resolver, &StreamlinkResolver::resolved);
  resolver.resolve(QStringLiteral("alpha"));
  QTRY_COMPARE_WITH_TIMEOUT(resolved.count(), 1, 2000);
  const QStringList lines = logLines(log);
  QVERIFY(lines.contains(QStringLiteral("PATH=/usr/bin")));
  QVERIFY(lines.contains(QStringLiteral("LD_LIBRARY_PATH=/opt/host/lib")));
  QVERIFY(lines.contains(QStringLiteral("LD_PRELOAD=unset")));
  QVERIFY(lines.contains(QStringLiteral("PYTHONHOME=unset")));
  QVERIFY(lines.contains(QStringLiteral("PYTHONPATH=unset")));
  QVERIFY(lines.contains(QStringLiteral("QT_PLUGIN_PATH=unset")));
  QVERIFY(lines.contains(QStringLiteral("QML2_IMPORT_PATH=unset")));
  QVERIFY(lines.contains(QStringLiteral("APPDIR=unset")));
  QCOMPARE(qgetenv("LD_LIBRARY_PATH"), (appDir + QStringLiteral("/usr/lib:/opt/host/lib")).toUtf8());
}

void PlaybackProcessTests::streamlinkErrorsAreClassified()
{
  QCOMPARE(StreamlinkResolver::processFailureMessage(QStringLiteral("HTTP 401 Unauthorized"), false),
           QStringLiteral("Streamlink authentication failed. Reconnect Twitch and try again."));
  QCOMPARE(StreamlinkResolver::processFailureMessage(QStringLiteral("No playable streams found on this URL"), false),
           QStringLiteral("No playable stream was found. The channel may be offline or unsupported."));
  QCOMPARE(StreamlinkResolver::processFailureMessage(QStringLiteral("ImportError: libcrypto.so.3 version `OPENSSL_3.3.0' not found"), false),
           QStringLiteral("Streamlink could not start because its host runtime libraries are incompatible."));
  QCOMPARE(StreamlinkResolver::processFailureMessage(QStringLiteral("unexpected failure"), true),
           QStringLiteral("Streamlink could not list stream qualities."));
}

void PlaybackProcessTests::runtimeTracebackIsNotDisplayed()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString script = writeScript(directory, QByteArrayLiteral(
      "printf \"ImportError: /tmp/.mount_Shudder/usr/lib/libcrypto.so.3: version 'OPENSSL_3.3.0' not found\\n\" >&2\nexit 1\n"));
  QVERIFY(!script.isEmpty());
  ScopedEnvironment environment;
  environment.set("SHUDDER_STREAMLINK_PATH", script.toUtf8());

  QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("Streamlink resolve error: ImportError:.*OPENSSL_3\\.3\\.0.*")));
  StreamlinkResolver resolver;
  QSignalSpy failed(&resolver, &StreamlinkResolver::failed);
  resolver.resolve(QStringLiteral("alpha"));
  QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 2000);
  const QString message = failed.first().at(1).toString();
  QCOMPARE(message, QStringLiteral("Streamlink could not start because its host runtime libraries are incompatible."));
  QVERIFY(!message.contains(QStringLiteral("Traceback")));
  QVERIFY(!message.contains(QStringLiteral("libcrypto.so.3")));
}

void PlaybackProcessTests::cancelledQualityLookupDoesNotRetry()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString log = directory.filePath(QStringLiteral("calls.log"));
  const QString script = writeScript(directory, QStringLiteral("printf '%s\\n' \"$*\" >> '%1'\nsleep 2\nprintf '{\"streams\":{\"720p\":{}}}\\n'\n").arg(log).toUtf8());
  QVERIFY(!script.isEmpty());
  ScopedEnvironment environment;
  environment.set("SHUDDER_STREAMLINK_PATH", script.toUtf8());

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
  ScopedEnvironment environment;
  environment.set("SHUDDER_STREAMLINK_PATH", script.toUtf8());

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
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString script = writeScript(directory, QByteArrayLiteral("exit 0\n"));
  ScopedEnvironment environment;
  environment.set("SHUDDER_STREAMLINK_PATH", script.toUtf8());
  StreamlinkResolver resolver;
  QVERIFY(QFile::remove(script));
  QSignalSpy failed(&resolver, &StreamlinkResolver::qualitiesFailed);
  resolver.requestQualities(QStringLiteral("alpha"));
  QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 1000);
  QTest::qWait(100);
  QCOMPARE(failed.count(), 1);
}

void PlaybackProcessTests::failedStartStillRunsPendingReplacement()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString script = writeScript(directory, QByteArrayLiteral("exit 0\n"));
  ScopedEnvironment environment;
  environment.set("SHUDDER_STREAMLINK_PATH", script.toUtf8());
  StreamlinkResolver resolver;
  QVERIFY(QFile::remove(script));
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
  ScopedEnvironment environment;
  environment.set("SHUDDER_STREAMLINK_PATH", script.toUtf8());

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
  ScopedEnvironment environment;
  environment.set("SHUDDER_STREAMLINK_PATH", script.toUtf8());

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
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const QString script = writeScript(directory, QByteArrayLiteral("exit 0\n"));
  ScopedEnvironment environment;
  environment.set("SHUDDER_STREAMLINK_PATH", script.toUtf8());
  PlayerHostServer host;
  PlayerController controller(&host);
  QVERIFY(QFile::remove(script));
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
