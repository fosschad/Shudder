#include "core/UrlUtils.h"

#include <QRegularExpression>
#include <QStringList>

namespace UrlUtils {

bool isSafeHttpUrl(const QUrl &url)
{
  if (!url.isValid()) return false;
  const QString scheme = url.scheme().toLower();
  return (scheme == QLatin1String("https") || scheme == QLatin1String("http")) && !url.host().isEmpty();
}

bool isTwitchHttpsOrigin(const QUrl &url)
{
  if (!url.isValid() || url.scheme().toLower() != QLatin1String("https")) return false;
  const QString host = url.host().toLower();
  return host == QLatin1String("twitch.tv") || host == QLatin1String("www.twitch.tv") ||
         host == QLatin1String("player.twitch.tv") || host.endsWith(QLatin1String(".twitch.tv")) ||
         host == QLatin1String("twitchcdn.net") || host.endsWith(QLatin1String(".twitchcdn.net"));
}

bool isAllowedTwitchActionUrl(const QUrl &url)
{
  if (!isTwitchHttpsOrigin(url)) return false;
  const QString host = url.host().toLower();
  if (host == QLatin1String("player.twitch.tv")) return true;
  return host == QLatin1String("www.twitch.tv") || host == QLatin1String("twitch.tv") ||
         host.endsWith(QLatin1String(".twitch.tv"));
}

QString redactedForLog(QString value)
{
  static const QRegularExpression oauth(R"((Authorization(?:=|%3D)OAuth(?:\s|%20)+)[A-Za-z0-9._~-]+)",
                                        QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression bearer(R"((Bearer\s+)[A-Za-z0-9._~-]+)",
                                         QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression query(R"(([?&](?:sig|token|signature|oauth_token|access_token)=)[^&\s]+)",
                                        QRegularExpression::CaseInsensitiveOption);
  value.replace(oauth, QStringLiteral("\\1[REDACTED]"));
  value.replace(bearer, QStringLiteral("\\1[REDACTED]"));
  value.replace(query, QStringLiteral("\\1[REDACTED]"));
  return value;
}

} // namespace UrlUtils
