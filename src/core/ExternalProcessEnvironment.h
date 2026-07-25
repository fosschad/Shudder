#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace ExternalProcessEnvironment {

[[nodiscard]] QProcessEnvironment forHostTool(const QProcessEnvironment &source = QProcessEnvironment::systemEnvironment());
[[nodiscard]] QString resolveHostExecutable(const QString &executable, const QProcessEnvironment &environment);

}
