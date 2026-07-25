#include "core/ExternalProcessEnvironment.h"

#include <QCoreApplication>
#include <QDir>
#include <QProcess>

#include <cstdio>

int main(int argc, char *argv[])
{
  QCoreApplication app(argc, argv);
  if (argc < 2) {
    std::fprintf(stderr, "Usage: shudder-host-tool-probe <executable> [arguments...]\n");
    return 2;
  }

  const QString appDir = qEnvironmentVariable("APPDIR");
  const bool verifyLibraries = qEnvironmentVariableIntValue("SHUDDER_VERIFY_HOST_LIBRARIES") == 1;
  QProcessEnvironment environment = ExternalProcessEnvironment::forHostTool();
  if (verifyLibraries) environment.insert(QStringLiteral("LD_DEBUG"), QStringLiteral("libs"));
  environment.remove(QStringLiteral("SHUDDER_VERIFY_HOST_LIBRARIES"));

  const QString executable = ExternalProcessEnvironment::resolveHostExecutable(QString::fromLocal8Bit(argv[1]), environment);
  if (executable.isEmpty()) {
    std::fprintf(stderr, "Host tool is unavailable or resolves inside the AppImage.\n");
    return 127;
  }

  QStringList arguments;
  for (int i = 2; i < argc; ++i) arguments.push_back(QString::fromLocal8Bit(argv[i]));
  QProcess process;
  process.setProcessEnvironment(environment);
  process.start(executable, arguments);
  if (!process.waitForStarted(10000)) {
    std::fprintf(stderr, "Host tool failed to start: %s\n", qPrintable(process.errorString()));
    return 126;
  }
  if (!process.waitForFinished(60000)) {
    process.kill();
    process.waitForFinished(5000);
    std::fprintf(stderr, "Host tool timed out.\n");
    return 124;
  }

  const QByteArray out = process.readAllStandardOutput();
  const QByteArray err = process.readAllStandardError();
  if (!out.isEmpty()) std::fwrite(out.constData(), 1, size_t(out.size()), stdout);
  if (!err.isEmpty()) std::fwrite(err.constData(), 1, size_t(err.size()), stderr);
  if (verifyLibraries) {
    const QByteArray bundledCrypto = QDir(appDir).filePath(QStringLiteral("usr/lib/libcrypto.so.3")).toLocal8Bit();
    if (!appDir.isEmpty() && err.contains(bundledCrypto)) {
      std::fprintf(stderr, "Host tool loaded libcrypto.so.3 from APPDIR.\n");
      return 125;
    }
    if (!err.contains("libcrypto.so.3")) {
      std::fprintf(stderr, "Host tool libcrypto.so.3 load was not observable.\n");
      return 125;
    }
  }
  return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : 125;
}
