#include "twitch/TwitchDirectoryModel.h"

#include <QSignalSpy>
#include <QTest>

class DirectoryModelTests final : public QObject {
  Q_OBJECT

private slots:
  void credentialChangesInvalidatePendingStateAndCache();
  void rowResetNotifiesCountAndDerivedItems();
};

void DirectoryModelTests::credentialChangesInvalidatePendingStateAndCache()
{
  TwitchDirectoryModel model;
  model.setAccessToken(QStringLiteral("token-a"));
  TwitchDirectoryModel::Item item;
  item.id = QStringLiteral("stream-a");
  item.login = QStringLiteral("alpha");
  model.m_items.push_back(item);
  model.m_pageCache.insert(QStringLiteral("/streams?first=24"), {model.m_items, QStringLiteral("cursor")});
  model.m_pendingPath = QStringLiteral("/streams?first=24");
  model.m_after = QStringLiteral("cursor");
  model.m_busy = true;
  model.m_hasMore = true;
  const int requestGeneration = model.m_requestGeneration;

  QSignalSpy busyChanged(&model, &TwitchDirectoryModel::busyChanged);
  QSignalSpy hasMoreChanged(&model, &TwitchDirectoryModel::hasMoreChanged);
  model.setAccessToken(QStringLiteral("token-b"));

  QVERIFY(model.m_requestGeneration > requestGeneration);
  QVERIFY(!model.busy());
  QVERIFY(model.m_pendingPath.isEmpty());
  QVERIFY(model.m_after.isEmpty());
  QVERIFY(model.m_pageCache.isEmpty());
  QVERIFY(!model.hasMore());
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(busyChanged.count(), 1);
  QCOMPARE(hasMoreChanged.count(), 1);

  model.m_pendingPath = QStringLiteral("/streams?first=24");
  model.m_busy = true;
  model.m_pageCache.insert(QStringLiteral("/streams?first=24"), {model.m_items, {}});
  model.setClientId(QStringLiteral("replacement-client"));
  QVERIFY(!model.busy());
  QVERIFY(model.m_pendingPath.isEmpty());
  QVERIFY(model.m_pageCache.isEmpty());
}

void DirectoryModelTests::rowResetNotifiesCountAndDerivedItems()
{
  TwitchDirectoryModel model;
  TwitchDirectoryModel::Item category;
  category.kind = QStringLiteral("category");
  category.id = QStringLiteral("1");
  category.categoryId = category.id;
  category.category = QStringLiteral("Category");
  model.m_items.push_back(category);

  QSignalSpy countChanged(&model, &TwitchDirectoryModel::countChanged);
  QSignalSpy itemsChanged(&model, &TwitchDirectoryModel::itemsChanged);
  model.resetRowsForRequest();

  QCOMPARE(model.rowCount(), 0);
  QCOMPARE(model.count(), 0);
  QCOMPARE(countChanged.count(), 1);
  QCOMPARE(itemsChanged.count(), 1);
  QVERIFY(model.searchCategoryItems().isEmpty());
  QVERIFY(model.searchChannelItems().isEmpty());
}

QTEST_MAIN(DirectoryModelTests)
#include "tst_directory_model.moc"
