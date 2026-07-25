#ifdef SHUDDER_WITH_LIBSECRET
#include <libsecret/secret.h>
#endif

#include "storage/SecretService.h"

#include "shudder_config.h"

#include <QPointer>

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

QString loadSecretBlocking(const QString &kind, QString *message)
{
#ifdef SHUDDER_WITH_LIBSECRET
  GError *error = nullptr;
  const QByteArray kindBytes = kind.toUtf8();
  gchar *secret = secret_password_lookup_sync(schema(), nullptr, &error, "kind", kindBytes.constData(), nullptr);
  if (error) {
    if (message) *message = QString::fromUtf8(error->message);
    g_error_free(error);
  }
  if (!secret) return {};
  const QString result = QString::fromUtf8(secret);
  secret_password_free(secret);
  return result;
#else
  Q_UNUSED(kind)
  if (message) *message = QObject::tr("Secret Service support is not available in this build.");
  return {};
#endif
}

bool clearSecretBlocking(const QString &kind, QString *message)
{
#ifdef SHUDDER_WITH_LIBSECRET
  GError *error = nullptr;
  const QByteArray kindBytes = kind.toUtf8();
  const gboolean ok = secret_password_clear_sync(schema(), nullptr, &error, "kind", kindBytes.constData(), nullptr);
  if (!ok && message) *message = error ? QString::fromUtf8(error->message) : QObject::tr("Secret Service did not clear the credential.");
  if (error) g_error_free(error);
  return ok;
#else
  Q_UNUSED(kind)
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
  m_storeFunction = storeSecretBlocking;
  m_loadFunction = loadSecretBlocking;
  m_clearFunction = clearSecretBlocking;
  m_workerContext = new QObject;
  m_workerContext->moveToThread(&m_workerThread);
  connect(&m_workerThread, &QThread::finished, m_workerContext, &QObject::deleteLater);
  m_workerThread.setObjectName(QStringLiteral("shudder-secret-store"));
  m_workerThread.start();
}

ShudderSecretStore::~ShudderSecretStore()
{
  if (!m_workerThread.isRunning()) return;
  QMetaObject::invokeMethod(m_workerContext, []() {}, Qt::BlockingQueuedConnection);
  m_workerThread.quit();
  m_workerThread.wait();
  m_workerContext = nullptr;
}

bool ShudderSecretStore::available() const { return m_available; }

bool ShudderSecretStore::store(const QString &kind, const QString &secret)
{
  QString message;
  bool ok = false;
  const StoreFunction operation = m_storeFunction;
  QMetaObject::invokeMethod(m_workerContext, [&]() { ok = operation(kind, secret, &message); }, Qt::BlockingQueuedConnection);
  if (!ok) emit error(message);
  return ok;
}

void ShudderSecretStore::storeAsync(const QString &kind, const QString &secret)
{
  QPointer<ShudderSecretStore> self(this);
  const StoreFunction operation = m_storeFunction;
  QMetaObject::invokeMethod(m_workerContext, [self, operation, kind, secret]() {
    QString message;
    const bool ok = operation(kind, secret, &message);
    if (!ok && self) {
      QMetaObject::invokeMethod(self.data(), [self, message]() {
        if (self) emit self->error(message);
      }, Qt::QueuedConnection);
    }
  }, Qt::QueuedConnection);
}

QString ShudderSecretStore::load(const QString &kind) const
{
  QString message;
  QString result;
  const LoadFunction operation = m_loadFunction;
  QMetaObject::invokeMethod(m_workerContext, [&]() { result = operation(kind, &message); }, Qt::BlockingQueuedConnection);
  if (!message.isEmpty()) emit error(message);
  return result;
}

bool ShudderSecretStore::clear(const QString &kind)
{
  QString message;
  bool ok = false;
  const ClearFunction operation = m_clearFunction;
  QMetaObject::invokeMethod(m_workerContext, [&]() { ok = operation(kind, &message); }, Qt::BlockingQueuedConnection);
  if (!ok) emit error(message);
  return ok;
}
