#include "MockFritzBox.h"

#include "LocationProbe.h"

#include <QSignalSpy>
#include <QtTest>

using namespace faxwerk;
using namespace faxwerk::gui;
using namespace faxwerk::test;

// LocationProbe against MockFritzBox instances on 127.0.0.1: it must pick the
// first reachable location in list order and sign off again.
class TestLocationProbe : public QObject
{
    Q_OBJECT

private slots:
    void skipsUnreachableLocation();
    void prefersListOrder();
    void wrongPasswordIsNotReachable();
    void reportsNoneWhenNothingAnswers();

private:
    static Location location(const QString &name, quint16 port, const QString &password = QStringLiteral("s3cret"));
    static QString probe(const QList<Location> &locations, int timeoutMs = 5000);
    static quint16 unusedPort();
};

Location TestLocationProbe::location(const QString &name, quint16 port, const QString &password)
{
    Location l = AppSettings::newLocation(name);
    l.server = QStringLiteral("127.0.0.1");
    l.port = port;
    l.user = QStringLiteral("faxwerk");
    l.password = password;
    return l;
}

QString TestLocationProbe::probe(const QList<Location> &locations, int timeoutMs)
{
    AppSettings settings;
    settings.locations = locations;
    LocationProbe probe;
    QSignalSpy finished(&probe, &LocationProbe::finished);
    probe.start(settings, locations, timeoutMs);
    if (!finished.wait(timeoutMs + 5000))
        return QStringLiteral("<no result>");
    return finished.first().first().toString();
}

quint16 TestLocationProbe::unusedPort()
{
    QUdpSocket socket;
    socket.bind(QHostAddress::LocalHost, 0);
    return socket.localPort(); // closed again when the socket goes away
}

void TestLocationProbe::skipsUnreachableLocation()
{
    MockFritzBox box({});
    QVERIFY(box.listen());
    const Location away = location(QStringLiteral("Freiburg"), unusedPort());
    const Location here = location(QStringLiteral("Offenburg"), box.port());

    QCOMPARE(probe({away, here}, 3000), here.id);
    QVERIFY(box.registered());
    QTRY_VERIFY_WITH_TIMEOUT(box.unregistered(), 5000);
    QVERIFY2(box.errors().isEmpty(), qPrintable(box.errors().join(QLatin1Char('\n'))));
}

void TestLocationProbe::prefersListOrder()
{
    MockFritzBox first({});
    MockFritzBox second({});
    QVERIFY(first.listen());
    QVERIFY(second.listen());
    const Location a = location(QStringLiteral("A"), first.port());
    const Location b = location(QStringLiteral("B"), second.port());

    QCOMPARE(probe({a, b}), a.id);
    QCOMPARE(probe({b, a}), b.id);
    QTRY_VERIFY_WITH_TIMEOUT(first.unregistered() && second.unregistered(), 5000);
}

void TestLocationProbe::wrongPasswordIsNotReachable()
{
    MockFritzBox box({});
    QVERIFY(box.listen());
    QCOMPARE(probe({location(QStringLiteral("A"), box.port(), QStringLiteral("wrong"))}), QString());
}

void TestLocationProbe::reportsNoneWhenNothingAnswers()
{
    QElapsedTimer timer;
    timer.start();
    QCOMPARE(probe({location(QStringLiteral("A"), unusedPort())}, 2000), QString());
    QVERIFY(timer.elapsed() < 6000);
}

QTEST_GUILESS_MAIN(TestLocationProbe)
#include "tst_locationprobe.moc"
