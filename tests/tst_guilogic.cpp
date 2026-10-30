#include "Journal.h"
#include "Messages.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace faxwerk;
using namespace faxwerk::gui;

// Non-visual parts of the desktop app: outcome texts, number cleanup, journal.
class TestGuiLogic : public QObject
{
    Q_OBJECT

private slots:
    void normalizeNumber_data();
    void normalizeNumber();
    void outcomeSeverity_data();
    void outcomeSeverity();
    void shortTime();
    void formatDuration();
    void journalRoundtrip();
    void recentRecipients();
};

void TestGuiLogic::normalizeNumber_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("spaces") << QStringLiteral("0761 4567 890") << QStringLiteral("07614567890");
    QTest::newRow("slash and dash") << QStringLiteral("0761 / 45-67 890") << QStringLiteral("07614567890");
    QTest::newRow("international") << QStringLiteral("+49 (761) 4567890") << QStringLiteral("+497614567890");
    QTest::newRow("plus only leading") << QStringLiteral("0761+123") << QStringLiteral("0761123");
    QTest::newRow("empty") << QString() << QString();
}

void TestGuiLogic::normalizeNumber()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(gui::normalizeNumber(input), expected);
}

void TestGuiLogic::outcomeSeverity_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<int>("sipStatus");
    QTest::addColumn<int>("pagesTransferred");
    QTest::addColumn<int>("severity");
    QTest::addColumn<QString>("title");
    QTest::newRow("success") << int(FaxStatus::Success) << 0 << 3 << int(Severity::Ok) << QStringLiteral("Zugestellt");
    QTest::newRow("busy") << int(FaxStatus::CallFailed) << 486 << 0 << int(Severity::Warning) << QStringLiteral("Besetzt");
    QTest::newRow("not found") << int(FaxStatus::CallFailed) << 404 << 0 << int(Severity::Failure)
                               << QStringLiteral("Nummer nicht gefunden");
    QTest::newRow("no fax") << int(FaxStatus::FaxFailed) << 0 << 0 << int(Severity::Failure)
                            << QStringLiteral("Kein Faxgerät erreicht");
    QTest::newRow("partial") << int(FaxStatus::FaxFailed) << 0 << 1 << int(Severity::Failure)
                             << QStringLiteral("Unvollständig");
    QTest::newRow("auth") << int(FaxStatus::AuthenticationFailed) << 401 << 0 << int(Severity::Failure)
                          << QStringLiteral("Anmeldung abgelehnt");
    QTest::newRow("aborted") << int(FaxStatus::Aborted) << 0 << 0 << int(Severity::Warning)
                             << QStringLiteral("Abgebrochen");
}

void TestGuiLogic::outcomeSeverity()
{
    QFETCH(int, status);
    QFETCH(int, sipStatus);
    QFETCH(int, pagesTransferred);
    QFETCH(int, severity);
    QFETCH(QString, title);
    const OutcomeText text = describeOutcome(FaxStatus(status), sipStatus, pagesTransferred, 3);
    QCOMPARE(int(text.severity), severity);
    QCOMPARE(text.title, title);
    QVERIFY(!text.detail.isEmpty());
}

void TestGuiLogic::shortTime()
{
    const QDateTime now(QDate(2026, 10, 30), QTime(15, 0));
    QCOMPARE(gui::shortTime(QDateTime(QDate(2026, 10, 30), QTime(14, 21)), now), QStringLiteral("14:21"));
    QCOMPARE(gui::shortTime(QDateTime(QDate(2026, 10, 29), QTime(9, 5)), now), QStringLiteral("gestern"));
    QCOMPARE(gui::shortTime(QDateTime(QDate(2026, 10, 26), QTime(9, 5)), now), QStringLiteral("26.10."));
}

void TestGuiLogic::formatDuration()
{
    QCOMPARE(gui::formatDuration(42000), QStringLiteral("42 s"));
    QCOMPARE(gui::formatDuration(112000), QStringLiteral("1:52 min"));
    QCOMPARE(gui::formatDuration(60000), QStringLiteral("1:00 min"));
}

void TestGuiLogic::journalRoundtrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("sub/journal.json"));

    JournalEntry entry;
    entry.time = QDateTime(QDate(2026, 10, 30), QTime(14, 21, 5));
    entry.destination = QStringLiteral("0761 4567 890");
    entry.recipientName = QStringLiteral("Praxis Dr. Berger, HNO");
    entry.documents = {QStringLiteral("/tmp/a.pdf"), QStringLiteral("/tmp/b.pdf")};
    entry.pages = 3;
    entry.status = FaxStatus::CallFailed;
    entry.sipStatus = 486;
    entry.message = QStringLiteral("call failed: destination busy (Busy Here)");
    entry.pagesTransferred = 0;
    entry.bitRate = 9600;
    entry.ecm = true;
    entry.remoteIdent = QStringLiteral("+49 761 4567890");
    entry.durationMs = 112000;

    {
        Journal journal(path);
        QSignalSpy changed(&journal, &Journal::changed);
        journal.add(entry);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(journal.entries().size(), 1);
        QVERIFY(!journal.entries().first().id.isEmpty());
    }

    Journal loaded(path);
    QString error;
    QVERIFY2(loaded.load(&error), qPrintable(error));
    QCOMPARE(loaded.entries().size(), 1);
    const JournalEntry &e = loaded.entries().first();
    QCOMPARE(e.time, entry.time);
    QCOMPARE(e.destination, entry.destination);
    QCOMPARE(e.recipientName, entry.recipientName);
    QCOMPARE(e.documents, entry.documents);
    QCOMPARE(e.pages, 3);
    QCOMPARE(e.status, FaxStatus::CallFailed);
    QCOMPARE(e.sipStatus, 486);
    QCOMPARE(e.message, entry.message);
    QCOMPARE(e.bitRate, 9600);
    QVERIFY(e.ecm);
    QCOMPARE(e.remoteIdent, entry.remoteIdent);
    QCOMPARE(e.durationMs, qint64(112000));

    loaded.remove(e.id);
    QVERIFY(loaded.entries().isEmpty());
    Journal reloaded(path);
    QVERIFY(reloaded.load());
    QVERIFY(reloaded.entries().isEmpty());
}

void TestGuiLogic::recentRecipients()
{
    QTemporaryDir dir;
    Journal journal(dir.filePath(QStringLiteral("journal.json")));
    auto add = [&journal](const QString &number, const QString &name) {
        JournalEntry entry;
        entry.destination = number;
        entry.recipientName = name;
        journal.add(entry);
    };
    add(QStringLiteral("111"), QStringLiteral("A"));
    add(QStringLiteral("222"), QString());
    add(QStringLiteral("111"), QStringLiteral("A neu"));
    add(QStringLiteral("333"), QStringLiteral("C"));

    const QList<Recipient> recent = journal.recentRecipients(2);
    QCOMPARE(recent.size(), 2);
    QCOMPARE(recent.at(0).number, QStringLiteral("333"));
    QCOMPARE(recent.at(1).number, QStringLiteral("111"));
    QCOMPARE(recent.at(1).name, QStringLiteral("A neu"));
    QCOMPARE(journal.recentRecipients(10).size(), 3);
}

QTEST_GUILESS_MAIN(TestGuiLogic)
#include "tst_guilogic.moc"
