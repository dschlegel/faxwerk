#include "AppSettings.h"
#include "GitHubReleaseUpdater.h"
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
    void dayHeading();
    void formatDuration();
    void journalRoundtrip();
    void recentRecipients();
    void settingsRoundtrip();
    void settingsFromCommandLineConfig();
    void releaseVersionCompare_data();
    void releaseVersionCompare();
    void releaseParse();
    void releaseParseRejects_data();
    void releaseParseRejects();
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

void TestGuiLogic::dayHeading()
{
    const QDate today(2026, 10, 30); // a Friday
    QCOMPARE(gui::dayHeading(today, today), QStringLiteral("Heute"));
    QCOMPARE(gui::dayHeading(QDate(2026, 10, 29), today), QStringLiteral("Gestern"));
    QCOMPARE(gui::dayHeading(QDate(2026, 10, 26), today), QStringLiteral("Montag, 26. Oktober"));
    QCOMPARE(gui::dayHeading(QDate(2025, 12, 31), today), QStringLiteral("Mittwoch, 31. Dezember 2025"));
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

void TestGuiLogic::settingsRoundtrip()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("faxwerk.ini"));

    AppSettings settings;
    Location freiburg = AppSettings::newLocation(QStringLiteral("Freiburg"));
    freiburg.server = QStringLiteral("192.168.10.1");
    freiburg.user = QStringLiteral("faxwerk620");
    freiburg.password = QStringLiteral("eins");
    freiburg.ident = QStringLiteral("+49 761 1234567");
    freiburg.header = QStringLiteral("Praxis Freiburg");
    freiburg.phonebookUser = QStringLiteral("telefonbuch");
    freiburg.phonebookPassword = QStringLiteral("drei");
    Location offenburg = AppSettings::newLocation(QStringLiteral("Offenburg"));
    offenburg.server = QStringLiteral("192.168.20.1");
    offenburg.port = 5070;
    offenburg.user = QStringLiteral("faxwerk621");
    offenburg.password = QStringLiteral("zwei");
    settings.locations = {freiburg, offenburg};
    settings.autoSelect = false;
    settings.preferredId = offenburg.id;
    settings.maxRate = 14400;
    settings.ecm = false;
    {
        QSettings ini(path, QSettings::IniFormat);
        settings.save(ini);
    }

    QSettings ini(path, QSettings::IniFormat);
    const AppSettings loaded = AppSettings::load(ini);
    QCOMPARE(loaded.locations.size(), 2);
    QCOMPARE(loaded.locations.at(0).id, freiburg.id);
    QCOMPARE(loaded.locations.at(0).name, QStringLiteral("Freiburg"));
    QCOMPARE(loaded.locations.at(0).header, QStringLiteral("Praxis Freiburg"));
    QCOMPARE(loaded.locations.at(1).server, QStringLiteral("192.168.20.1"));
    QCOMPARE(loaded.locations.at(1).port, quint16(5070));
    QCOMPARE(loaded.locations.at(1).password, QStringLiteral("zwei"));
    QCOMPARE(loaded.locations.at(0).phonebookUser, QStringLiteral("telefonbuch"));
    QCOMPARE(loaded.locations.at(0).phonebookPassword, QStringLiteral("drei"));
    QVERIFY(loaded.locations.at(0).hasPhonebook());
    QVERIFY(!loaded.locations.at(1).hasPhonebook());
    QVERIFY(!loaded.autoSelect);
    QCOMPARE(loaded.preferredId, offenburg.id);
    QCOMPARE(loaded.maxRate, 14400);
    QVERIFY(!loaded.ecm);
    QVERIFY(loaded.isConfigured());

    // The command line tool reads the first location from [sip] and [fax].
    QCOMPARE(ini.value(QStringLiteral("sip/server")).toString(), QStringLiteral("192.168.10.1"));
    QCOMPARE(ini.value(QStringLiteral("sip/user")).toString(), QStringLiteral("faxwerk620"));
    QCOMPARE(ini.value(QStringLiteral("fax/ident")).toString(), QStringLiteral("+49 761 1234567"));
}

void TestGuiLogic::settingsFromCommandLineConfig()
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("faxwerk.ini"));
    {
        QSettings ini(path, QSettings::IniFormat);
        ini.setValue(QStringLiteral("sip/server"), QStringLiteral("fritz.box"));
        ini.setValue(QStringLiteral("sip/user"), QStringLiteral("faxwerk620"));
        ini.setValue(QStringLiteral("sip/password"), QStringLiteral("geheim"));
        ini.setValue(QStringLiteral("fax/header"), QStringLiteral("Example Ltd."));
    }
    QSettings ini(path, QSettings::IniFormat);
    const AppSettings loaded = AppSettings::load(ini);
    QCOMPARE(loaded.locations.size(), 1);
    QCOMPARE(loaded.locations.first().server, QStringLiteral("fritz.box"));
    QCOMPARE(loaded.locations.first().user, QStringLiteral("faxwerk620"));
    QCOMPARE(loaded.locations.first().header, QStringLiteral("Example Ltd."));
    QVERIFY(!loaded.locations.first().id.isEmpty());
    QVERIFY(loaded.autoSelect);
}

void TestGuiLogic::releaseVersionCompare_data()
{
    QTest::addColumn<QString>("candidate");
    QTest::addColumn<QString>("current");
    QTest::addColumn<bool>("newer");
    QTest::newRow("patch") << QStringLiteral("v0.1.1") << QStringLiteral("0.1.0") << true;
    QTest::newRow("same") << QStringLiteral("v0.1.0") << QStringLiteral("0.1.0") << false;
    QTest::newRow("older") << QStringLiteral("0.0.9") << QStringLiteral("0.1.0") << false;
    QTest::newRow("numeric, not text") << QStringLiteral("0.10.0") << QStringLiteral("0.9.0") << true;
    QTest::newRow("shorter") << QStringLiteral("1.0") << QStringLiteral("0.9.9") << true;
    QTest::newRow("trailing zero") << QStringLiteral("1.0.0") << QStringLiteral("1.0") << false;
    QTest::newRow("suffix ignored") << QStringLiteral("v0.2.0-rc1") << QStringLiteral("0.1.0") << true;
    QTest::newRow("no version") << QStringLiteral("latest") << QStringLiteral("0.1.0") << false;
    QTest::newRow("empty") << QString() << QStringLiteral("0.1.0") << false;
}

void TestGuiLogic::releaseVersionCompare()
{
    QFETCH(QString, candidate);
    QFETCH(QString, current);
    QFETCH(bool, newer);
    QCOMPARE(GitHubReleaseUpdater::isNewerVersion(candidate, current), newer);
}

static QByteArray releaseJson(const QString &tag, const QString &assets, bool prerelease = false)
{
    return QStringLiteral(R"({"tag_name": "%1", "html_url": "https://github.com/o/r/releases/tag/%1",
        "draft": false, "prerelease": %2, "assets": [%3]})")
        .arg(tag, prerelease ? QStringLiteral("true") : QStringLiteral("false"), assets)
        .toUtf8();
}

static QString assetJson(const QString &name, const QString &digest = QString())
{
    return QStringLiteral(R"({"name": "%1", "size": 1234, "digest": "%2",
        "browser_download_url": "https://github.com/o/r/releases/download/v0.2.0/%1"})")
        .arg(name, digest);
}

void TestGuiLogic::releaseParse()
{
    const QStringList suffixes = {QStringLiteral("-macos-arm64.pkg"), QStringLiteral("-macos-arm64.dmg")};
    const QString assets = assetJson(QStringLiteral("Faxwerk-0.2.0-macos-arm64.dmg")) + QLatin1Char(',')
        + assetJson(QStringLiteral("Faxwerk-0.2.0-windows-x64-setup.exe")) + QLatin1Char(',')
        + assetJson(QStringLiteral("Faxwerk-0.2.0-macos-arm64.pkg"), QStringLiteral("sha256:ABCDEF01"));
    QString error;
    const auto release = GitHubReleaseUpdater::parseRelease(releaseJson(QStringLiteral("v0.2.0"), assets), suffixes,
                                                            &error);
    QVERIFY2(release.has_value(), qPrintable(error));
    QCOMPARE(release->version, QStringLiteral("0.2.0"));
    // The .pkg is preferred even though the .dmg comes first.
    QCOMPARE(release->asset.name, QStringLiteral("Faxwerk-0.2.0-macos-arm64.pkg"));
    QCOMPARE(release->asset.size, qint64(1234));
    QCOMPARE(release->asset.sha256, QByteArray("abcdef01"));
    QCOMPARE(release->asset.url.host(), QStringLiteral("github.com"));

    const auto windows = GitHubReleaseUpdater::parseRelease(releaseJson(QStringLiteral("v0.2.0"), assets),
                                                            {QStringLiteral("-windows-x64-setup.exe")});
    QVERIFY(windows.has_value());
    QCOMPARE(windows->asset.name, QStringLiteral("Faxwerk-0.2.0-windows-x64-setup.exe"));
    QVERIFY(windows->asset.sha256.isEmpty());
}

void TestGuiLogic::releaseParseRejects_data()
{
    QTest::addColumn<QByteArray>("json");
    const QString pkg = assetJson(QStringLiteral("Faxwerk-0.2.0-macos-arm64.pkg"));
    QTest::newRow("not json") << QByteArray("<html>");
    QTest::newRow("no matching asset")
        << releaseJson(QStringLiteral("v0.2.0"), assetJson(QStringLiteral("Faxwerk-0.2.0-x86_64.AppImage")));
    QTest::newRow("no version") << releaseJson(QStringLiteral("nightly"), pkg);
    QTest::newRow("prerelease") << releaseJson(QStringLiteral("v0.2.0"), pkg, true);
    QTest::newRow("plain http") << releaseJson(QStringLiteral("v0.2.0"),
                                               QStringLiteral(R"({"name": "Faxwerk-0.2.0-macos-arm64.pkg",
                                                   "browser_download_url": "http://example.org/x.pkg"})"));
}

void TestGuiLogic::releaseParseRejects()
{
    QFETCH(QByteArray, json);
    QString error;
    QVERIFY(!GitHubReleaseUpdater::parseRelease(json, {QStringLiteral("-macos-arm64.pkg")}, &error).has_value());
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(TestGuiLogic)
#include "tst_guilogic.moc"
