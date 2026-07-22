#ifdef SHUDDER_WITH_LIBSECRET
#include <libsecret/secret.h>
#endif

#include "storage/SecretService.h"

#include "shudder_config.h"

#include <QPointer>
#include <QThread>

namespace {
#ifdef SHUDDER_WITH_LIBSECRET
const SecretSchema *schema()
{
  static const SecretSchema s = {
      SHUDDER_APP_ID ".Credentials",
      SECRET_SCHEMA_NONE,
      {{"kind", SECRET_SCHEMA_ATTRIBUTE_STRING}, {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}}};
  return &s;
}
#endif

bool storeSecretBlocking(const QString &kind, const QString &secret, QString *message)
{
#ifdef SHUDDER_WITH_LIBSECRET
  GError *error = nullptr;
  const QByteArray kindBytes = kind.toUtf8();
  const QByteArray secretBytes = secret.toUtf8();
  const gboolean ok = secret_password_store_sync(schema(), SECRET_COLLECTION_DEFAULT, SHUDDER_PRODUCT_NAME,
                                                secretBytes.constData(), nullptr, &error, "kind", kindBytes.constData(), nullptr);
  if (!ok) {
    if (message) *message = error ? QString::fromUtf8(error->message) : QObject::tr("Secret Service rejected the credential.");
    if (error) g_error_free(error);
    return false;
  }
  return true;
#else
  Q_UNUSED(kind)
  Q_UNUSED(secret)
  if (message) *message = QObject::tr("Secret Service support is not available in this build.");
  return false;
#endif
}
}

ShudderSecretStore::ShudderSecretStore(QObject *parent) : QObject(parent)
{
#ifdef SHUDDER_WITH_LIBSECRET
  m_available = true;
#else
  m_available = false;
#endif
}

bool ShudderSecretStore::available() const { return m_available; }

bool ShudderSecretStore::store(const QString &kind, const QString &secret)
{
  QString message;
  const bool ok = storeSecretBlocking(kind, secret, &message);
  if (!ok) emit error(message);
  return ok;
}

void ShudderSecretStore::storeAsync(const QString &kind, const QString &secret)
{
  QPointer<ShudderSecretStore> self(this);
  QThread *worker = QThread::create([self, kind, secret]() {
    QString message;
    const bool ok = storeSecretBlocking(kind, secret, &message);
    if (!ok && self) {
      QMetaObject::invokeMethod(self.data(), [self, message]() {
        if (self) emit self->error(message);
      }, Qt::QueuedConnection);
    }
  });
  worker->setObjectName(QStringLiteral("shudder-secret-store"));
  connect(worker, &QThread::finished, worker, &QObject::deleteLater);
  worker->start();
}

QString ShudderSecretStore::load(const QString &kind) const
{
#ifdef SHUDDER_WITH_LIBSECRET
  GError *error = nullptr;
  const QByteArray kindBytes = kind.toUtf8();
  gchar *secret = secret_password_lookup_sync(schema(), nullptr, &error, "kind", kindBytes.constData(), nullptr);
  if (error) {
    const QString message = QString::fromUtf8(error->message);
    g_error_free(error);
    emit this->error(message);
  }
  if (!secret) return {};
  const QString result = QString::fromUtf8(secret);
  secret_password_free(secret);
  return result;
#else
  Q_UNUSED(kind)
  emit error(tr("Secret Service support is not available in this build."));
  return {};
#endif
}

bool ShudderSecretStore::clear(const QString &kind)
{
#ifdef SHUDDER_WITH_LIBSECRET
  GError *error = nullptr;
  const QByteArray kindBytes = kind.toUtf8();
  const gboolean ok = secret_password_clear_sync(schema(), nullptr, &error, "kind", kindBytes.constData(), nullptr);
  if (!ok) {
    const QString message = error ? QString::fromUtf8(error->message) : tr("Secret Service did not clear the credential.");
    if (error) g_error_free(error);
    emit this->error(message);
    return false;
  }
  return true;
#else
  Q_UNUSED(kind)
  emit error(tr("Secret Service support is not available in this build."));
  return false;
#endif
}
