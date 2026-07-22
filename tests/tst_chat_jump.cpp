#include <QAbstractListModel>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickView>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTest>
#include <QtMath>

class FakePreferences final : public QObject {
  Q_OBJECT
public:
  Q_INVOKABLE QVariant get(const QString &key) const
  {
    if (key == QLatin1String("chatFontSize")) return m_chatFontSize;
    if (key == QLatin1String("chatEmoteSize")) return m_chatEmoteSize;
    if (key == QLatin1String("emotePickerWidth")) return 420;
    if (key == QLatin1String("emotePickerHeight")) return 520;
    return {};
  }

  void setChatFontSize(int size)
  {
    m_chatFontSize = size;
    emit valuesChanged();
  }

signals:
  void valuesChanged();

private:
  int m_chatFontSize = 15;
  int m_chatEmoteSize = 28;
};

class FakeAuthService final : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool signedIn READ signedIn CONSTANT)
public:
  bool signedIn() const { return true; }
};

class SyntheticChatModel final : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY channelChanged)
  Q_PROPERTY(QVariantList knownUsers READ knownUsers NOTIFY knownUsersChanged)
  Q_PROPERTY(QVariantList emotePickerEmotes READ emotePickerEmotes NOTIFY emotePickerEmotesChanged)
  Q_PROPERTY(QStringList preloadEmoteImageUrls READ preloadEmoteImageUrls NOTIFY emotePickerEmotesChanged)

public:
  enum Roles {
    IdRole = Qt::UserRole + 1,
    AuthorRole,
    DisplayNameRole,
    BodyRole,
    ColorRole,
    TimestampRole,
    ActionRole,
    NoticeRole,
    DeletedRole,
    ReplyParentIdRole,
    ReplyParentBodyRole,
    ReplyParentUserRole,
    BadgesRole,
    BadgeAssetsRole,
    MessagePartsRole,
    PlainTextRole,
  };

  struct Row {
    QString id;
    QString author = QStringLiteral("tester");
    QString displayName = QStringLiteral("Tester");
    QString body;
    bool action = false;
    bool notice = false;
    bool deleted = false;
    QString replyParentId;
    QString replyParentBody;
    QString replyParentUser;
    bool badge = false;
    QString badgeImageUrl;
    bool emote = false;
  };

  int rowCount(const QModelIndex &parent = QModelIndex()) const override
  {
    return parent.isValid() ? 0 : m_rows.size();
  }

  QVariant data(const QModelIndex &index, int role) const override
  {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};
    const Row &row = m_rows.at(index.row());
    switch (role) {
    case IdRole: return row.id;
    case AuthorRole: return row.author;
    case DisplayNameRole: return row.displayName;
    case BodyRole: return row.body;
    case ColorRole: return QStringLiteral("#f5f5f5");
    case TimestampRole: return QStringLiteral("12:34");
    case ActionRole: return row.action;
    case NoticeRole: return row.notice;
    case DeletedRole: return row.deleted;
    case ReplyParentIdRole: return row.replyParentId;
    case ReplyParentBodyRole: return row.replyParentBody;
    case ReplyParentUserRole: return row.replyParentUser;
    case BadgesRole: return row.badge ? QStringList{QStringLiteral("moderator/1")} : QStringList{};
    case BadgeAssetsRole: {
      QVariantMap badge;
      badge.insert(QStringLiteral("key"), QStringLiteral("moderator/1"));
      badge.insert(QStringLiteral("title"), QStringLiteral("Moderator"));
      badge.insert(QStringLiteral("imageUrl"), row.badgeImageUrl);
      return row.badge ? QVariantList{badge} : QVariantList{};
    }
    case MessagePartsRole: {
      QVariantList parts;
      QVariantMap text;
      text.insert(QStringLiteral("type"), QStringLiteral("text"));
      text.insert(QStringLiteral("text"), row.deleted ? QStringLiteral("<message deleted>") : row.body);
      parts.push_back(text);
      if (row.emote) {
        QVariantMap emote;
        emote.insert(QStringLiteral("type"), QStringLiteral("emote"));
        emote.insert(QStringLiteral("name"), QStringLiteral("Kappa"));
        emote.insert(QStringLiteral("provider"), QStringLiteral("Synthetic"));
        emote.insert(QStringLiteral("imageUrl"), QString());
        parts.push_back(emote);
      }
      return parts;
    }
    case PlainTextRole: return row.body;
    default: return {};
    }
  }

  QHash<int, QByteArray> roleNames() const override
  {
    return {{IdRole, "messageId"},
            {AuthorRole, "author"},
            {DisplayNameRole, "displayName"},
            {BodyRole, "body"},
            {ColorRole, "color"},
            {TimestampRole, "timestamp"},
            {ActionRole, "action"},
            {NoticeRole, "notice"},
            {DeletedRole, "deleted"},
            {ReplyParentIdRole, "replyParentId"},
            {ReplyParentBodyRole, "replyParentBody"},
            {ReplyParentUserRole, "replyParentUser"},
            {BadgesRole, "badges"},
            {BadgeAssetsRole, "badgeAssets"},
            {MessagePartsRole, "messageParts"},
            {PlainTextRole, "plainText"}};
  }

  QString channel() const { return m_channel; }

  void setChannel(const QString &channel)
  {
    if (m_channel == channel) return;
    beginResetModel();
    m_channel = channel;
    m_rows.clear();
    endResetModel();
    emit channelChanged();
  }

  QVariantList knownUsers() const
  {
    QVariantMap user;
    user.insert(QStringLiteral("login"), QStringLiteral("tester"));
    user.insert(QStringLiteral("displayName"), QStringLiteral("Tester"));
    return {user};
  }

  QVariantList emotePickerEmotes() const { return {}; }
  QStringList preloadEmoteImageUrls() const { return {}; }

  Q_INVOKABLE bool sendMessage(const QString &) { return true; }
  Q_INVOKABLE void refreshEmotePicker() {}

  void appendMessages(int count)
  {
    for (int i = 0; i < count; ++i) appendOne();
  }

  void appendOne()
  {
    const int next = ++m_nextId;
    Row row;
    row.id = QStringLiteral("msg-%1").arg(next);
    row.action = next % 11 == 0;
    row.notice = next % 29 == 0;
    row.badge = next % 3 == 0;
    row.emote = next % 5 == 0;
    if (next % 7 == 0) {
      row.replyParentId = QStringLiteral("msg-%1").arg(qMax(1, next - 3));
      row.replyParentUser = QStringLiteral("OtherUser");
      row.replyParentBody = QStringLiteral("Earlier message with enough text to elide");
    }
    row.body = QStringLiteral("Synthetic chat message %1 with wrapped text, URL https://example.invalid/%1, and variable length %2")
                   .arg(next)
                   .arg(QString(next % 17, QLatin1Char('x')));
    const int rowIndex = m_rows.size();
    beginInsertRows(QModelIndex(), rowIndex, rowIndex);
    m_rows.push_back(std::move(row));
    endInsertRows();
  }

  void appendBadgeMessage(const QString &imageUrl)
  {
    Row row;
    row.id = QStringLiteral("badge-msg-%1").arg(++m_nextId);
    row.badge = true;
    row.badgeImageUrl = imageUrl;
    row.body = QStringLiteral("Synthetic badge message");
    const int rowIndex = m_rows.size();
    beginInsertRows(QModelIndex(), rowIndex, rowIndex);
    m_rows.push_back(std::move(row));
    endInsertRows();
  }

  void trimTo(int limit)
  {
    const int overflow = m_rows.size() - limit;
    if (overflow <= 0) return;
    beginRemoveRows(QModelIndex(), 0, overflow - 1);
    m_rows.erase(m_rows.begin(), m_rows.begin() + overflow);
    endRemoveRows();
  }

  void markDeletedEvery(int step)
  {
    if (step <= 0) return;
    for (int i = 0; i < m_rows.size(); i += step) {
      m_rows[i].deleted = true;
      emit dataChanged(index(i), index(i), {DeletedRole, MessagePartsRole, PlainTextRole});
    }
  }

  void resetRows()
  {
    beginResetModel();
    m_rows.clear();
    endResetModel();
  }

signals:
  void channelChanged();
  void knownUsersChanged();
  void emotePickerEmotesChanged();

private:
  QVector<Row> m_rows;
  QString m_channel = QStringLiteral("alpha");
  int m_nextId = 0;
};

class ChatJumpTests final : public QObject {
  Q_OBJECT

private slots:
  void jumpHandlesEmptyOneInsertRemoveResetAndDestroy();
  void jumpStress500Cycles();
  void unresolvedBadgesDoNotRenderPlaceholders();

private:
  struct Harness {
    SyntheticChatModel model;
    FakePreferences preferences;
    FakeAuthService auth;
    QQuickView view;
    QStringList warnings;
  };

  std::unique_ptr<Harness> createHarness()
  {
    auto harness = std::make_unique<Harness>();
    harness->view.setResizeMode(QQuickView::SizeRootObjectToView);
    harness->view.setWidth(420);
    harness->view.setHeight(620);
    harness->view.engine()->rootContext()->setContextProperty(QStringLiteral("chatModel"), &harness->model);
    harness->view.engine()->rootContext()->setContextProperty(QStringLiteral("preferences"), &harness->preferences);
    harness->view.engine()->rootContext()->setContextProperty(QStringLiteral("authService"), &harness->auth);
    QObject::connect(harness->view.engine(), &QQmlEngine::warnings, &harness->view, [&warnings = harness->warnings](const QList<QQmlError> &items) {
      for (const QQmlError &warning : items) warnings.push_back(warning.toString());
    });
    harness->view.setSource(QUrl::fromLocalFile(QStringLiteral(SHUDDER_SOURCE_DIR "/qml/chat/ChatPanel.qml")));
    harness->view.show();
    return harness;
  }

  static QQuickItem *chatList(const Harness &harness)
  {
    return harness.view.rootObject()->findChild<QQuickItem *>(QStringLiteral("chatMessageList"));
  }

  static void jump(QQuickItem *list)
  {
    QVERIFY(QMetaObject::invokeMethod(list, "jumpToPresent"));
  }

  static void scrollAwayFromTail(QQuickItem *list, qreal fraction)
  {
    const qreal origin = list->property("originY").toReal();
    const qreal contentHeight = list->property("contentHeight").toReal();
    const qreal height = list->height();
    const qreal maxY = qMax(origin, origin + contentHeight - height);
    list->setProperty("followTail", false);
    list->setProperty("contentY", origin + (maxY - origin) * qBound<qreal>(0.0, fraction, 1.0));
  }

  static void verifyListState(const Harness &harness)
  {
    QQuickItem *list = chatList(harness);
    QVERIFY(list);
    const qreal contentY = list->property("contentY").toReal();
    const qreal contentHeight = list->property("contentHeight").toReal();
    const qreal originY = list->property("originY").toReal();
    QVERIFY(qIsFinite(contentY));
    QVERIFY(qIsFinite(contentHeight));
    QVERIFY(qIsFinite(originY));
    QCOMPARE(list->property("count").toInt(), harness.model.rowCount());
    QVERIFY2(harness.warnings.isEmpty(), qPrintable(harness.warnings.join(QLatin1Char('\n'))));
  }

  static void drainEvents(int turns = 8)
  {
    for (int i = 0; i < turns; ++i) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
    }
  }

  static void collectItemsByObjectName(QQuickItem *item, const QString &objectName, QList<QQuickItem *> *matches)
  {
    if (!item) return;
    if (item->objectName() == objectName) matches->push_back(item);
    for (QQuickItem *child : item->childItems()) collectItemsByObjectName(child, objectName, matches);
  }

  static QList<QQuickItem *> visualItemsByObjectName(QQuickItem *root, const QString &objectName)
  {
    QList<QQuickItem *> matches;
    collectItemsByObjectName(root, objectName, &matches);
    return matches;
  }

  static bool jumpSettled(QQuickItem *list)
  {
    drainEvents();
    if (list->property("pendingTailJump").toBool()) drainEvents(40);
    return !list->property("pendingTailJump").toBool();
  }
};

void ChatJumpTests::jumpHandlesEmptyOneInsertRemoveResetAndDestroy()
{
  auto harness = createHarness();
  QVERIFY(harness->view.status() == QQuickView::Ready);
  QVERIFY(harness->view.rootObject());
  QQuickItem *list = chatList(*harness);
  QVERIFY(list);

  jump(list);
  QVERIFY(jumpSettled(list));
  verifyListState(*harness);

  harness->model.appendMessages(1);
  jump(list);
  QVERIFY(jumpSettled(list));
  verifyListState(*harness);

  harness->model.appendMessages(80);
  QTRY_VERIFY_WITH_TIMEOUT(list->property("count").toInt() == harness->model.rowCount(), 1000);
  scrollAwayFromTail(list, 0.25);
  harness->model.appendMessages(20);
  harness->model.trimTo(40);
  harness->model.markDeletedEvery(4);
  jump(list);
  jump(list);
  QVERIFY(jumpSettled(list));
  verifyListState(*harness);

  harness->model.resetRows();
  jump(list);
  QVERIFY(jumpSettled(list));
  verifyListState(*harness);

  harness->model.appendMessages(30);
  harness->view.rootObject()->deleteLater();
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
  QCoreApplication::processEvents();
}

void ChatJumpTests::jumpStress500Cycles()
{
  auto harness = createHarness();
  QVERIFY(harness->view.status() == QQuickView::Ready);
  QVERIFY(harness->view.rootObject());
  QQuickItem *list = chatList(*harness);
  QVERIFY(list);

  harness->model.appendMessages(500);
  harness->model.trimTo(90);
  QTRY_VERIFY_WITH_TIMEOUT(list->property("count").toInt() == harness->model.rowCount(), 1000);

  for (int cycle = 0; cycle < 500; ++cycle) {
    scrollAwayFromTail(list, (cycle % 10) / 12.0);
    harness->model.appendMessages(cycle % 17 == 0 ? 30 : 3);
    if (cycle % 3 == 0) harness->model.trimTo(90);
    if (cycle % 5 == 0) harness->model.markDeletedEvery(9);
    if (cycle % 11 == 0) harness->preferences.setChatFontSize(14 + (cycle % 8));
    if (cycle % 13 == 0) harness->view.resize(360 + (cycle % 4) * 70, 460 + (cycle % 5) * 40);
    if (cycle % 29 == 0) {
      harness->model.setChannel(QStringLiteral("channel-%1").arg(cycle));
      harness->model.appendMessages(50);
      list = chatList(*harness);
      QVERIFY(list);
    }

    jump(list);
    jump(list);
    if (cycle % 7 == 0) jump(list);
    QVERIFY(jumpSettled(list));
    verifyListState(*harness);
  }

  QVERIFY(list->property("followTail").toBool());
  QCOMPARE(list->property("pendingMessageCount").toInt(), 0);
}

void ChatJumpTests::unresolvedBadgesDoNotRenderPlaceholders()
{
  auto harness = createHarness();
  QVERIFY(harness->view.status() == QQuickView::Ready);
  QVERIFY(harness->view.rootObject());
  QQuickItem *list = chatList(*harness);
  QVERIFY(list);

  harness->model.appendBadgeMessage(QString());
  jump(list);
  QVERIFY(jumpSettled(list));

  const QList<QQuickItem *> badgeCells = visualItemsByObjectName(harness->view.rootObject(), QStringLiteral("chatBadgeCell"));
  QVERIFY(!badgeCells.isEmpty());
  for (QQuickItem *badgeCell : badgeCells) {
    QVERIFY(!badgeCell->isVisible());
    QCOMPARE(badgeCell->width(), 0.0);
  }

  const QList<QQuickItem *> badgeLoaders = visualItemsByObjectName(harness->view.rootObject(), QStringLiteral("chatBadgeImageLoader"));
  for (QQuickItem *badgeLoader : badgeLoaders) QVERIFY(!badgeLoader->property("active").toBool());
  QVERIFY2(harness->warnings.isEmpty(), qPrintable(harness->warnings.join(QLatin1Char('\n'))));
}

int main(int argc, char **argv)
{
  qputenv("QML_DISABLE_DISK_CACHE", "1");
  if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  QQuickStyle::setStyle(QStringLiteral("Fusion"));
  ChatJumpTests tests;
  return QTest::qExec(&tests, argc, argv);
}

#include "tst_chat_jump.moc"
