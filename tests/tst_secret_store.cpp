#include "storage/SecretService.h"

#include <QHash>
#include <QMutex>
#include <QTest>

class SecretStoreTests final : public QObject {
  Q_OBJECT

private slots:
  void operationsAreSerializedInSubmissionOrder();
};

void SecretStoreTests::operationsAreSerializedInSubmissionOrder()
{
  ShudderSecretStore store;
  QMutex mutex;
  QHash<QString, QString> values;
  QStringList operations;
  store.m_storeFunction = [&](const QString &kind, const QString &secret, QString *) {
    QMutexLocker locker(&mutex);
    operations.push_back(QStringLiteral("store:%1").arg(secret));
    values.insert(kind, secret);
    return true;
  };
  store.m_loadFunction = [&](const QString &kind, QString *) {
    QMutexLocker locker(&mutex);
    operations.push_back(QStringLiteral("load"));
    return values.value(kind);
  };
  store.m_clearFunction = [&](const QString &kind, QString *) {
    QMutexLocker locker(&mutex);
    operations.push_back(QStringLiteral("clear"));
    values.remove(kind);
    return true;
  };

  store.storeAsync(QStringLiteral("oauth"), QStringLiteral("old"));
  store.storeAsync(QStringLiteral("oauth"), QStringLiteral("new"));
  QCOMPARE(store.load(QStringLiteral("oauth")), QStringLiteral("new"));
  QCOMPARE(operations, QStringList({QStringLiteral("store:old"), QStringLiteral("store:new"), QStringLiteral("load")}));

  store.storeAsync(QStringLiteral("oauth"), QStringLiteral("resurrect"));
  QVERIFY(store.clear(QStringLiteral("oauth")));
  QVERIFY(store.load(QStringLiteral("oauth")).isEmpty());
  QCOMPARE(operations.mid(3), QStringList({QStringLiteral("store:resurrect"), QStringLiteral("clear"), QStringLiteral("load")}));
}

QTEST_MAIN(SecretStoreTests)
#include "tst_secret_store.moc"
