#pragma once

#include <QUrl>

namespace UrlUtils {
[[nodiscard]] bool isSafeHttpUrl(const QUrl &url);
[[nodiscard]] bool isTwitchHttpsOrigin(const QUrl &url);
[[nodiscard]] bool isAllowedTwitchActionUrl(const QUrl &url);
[[nodiscard]] QString redactedForLog(QString value);
}
