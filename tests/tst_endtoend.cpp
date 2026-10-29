#include "MockFritzBox.h"
#include "TestDocuments.h"

#include "FaxJob.h"
#include "document/FaxTiff.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace faxwerk;
using namespace faxwerk::test;

// Full pipeline against MockFritzBox on 127.0.0.1: PDF -> TIFF -> REGISTER
// (401) -> INVITE (407) -> RTP/T.30 at an accelerated packet interval ->
// BYE from the mock -> unregister.
class TestEndToEnd : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void sendFax_data();
    void sendFax();
    void wrongPassword();
    void busy();

private:
    FaxJobConfig makeConfig(const MockFritzBox &mock, const QString &password) const;
    FaxOutcome run(FaxJob &job) const;

    QTemporaryDir m_dir;
    QString m_pdf;
    RtpSession::Options m_fastRtp;
};

void TestEndToEnd::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_pdf = m_dir.filePath(QStringLiteral("letter.pdf"));
    QVERIFY(makeTestPdf(m_pdf, 3));
    // 10x faster than real time; both sides use the same pacing.
    m_fastRtp.packetInterval = std::chrono::milliseconds(2);
}

FaxJobConfig TestEndToEnd::makeConfig(const MockFritzBox &mock, const QString &password) const
{
    FaxJobConfig config;
    config.sip.account.server = QStringLiteral("127.0.0.1");
    config.sip.account.serverPort = mock.port();
    config.sip.account.user = QStringLiteral("faxwerk");
    config.sip.account.password = password;
    config.sip.account.userAgent = QStringLiteral("Faxwerk-Test");
    config.sip.rtp = m_fastRtp;
    config.sip.hangupDelayMs = 3000; // the mock hangs up first
    config.destination = QStringLiteral("0301234567");
    config.pdfFiles = {m_pdf};
    config.fax.stationId = QStringLiteral("+49 30 7654321");
    config.fax.headerInfo = QStringLiteral("Faxwerk end-to-end test");
    config.timeoutSeconds = 150;
    return config;
}

FaxOutcome TestEndToEnd::run(FaxJob &job) const
{
    QSignalSpy finished(&job, &FaxJob::finished);
    QObject::connect(&job, &FaxJob::statusChanged, [](const QString &status) { qInfo("status: %s", qPrintable(status)); });
    job.start();
    if (!finished.wait(160000))
        qWarning("FaxJob did not finish");
    return job.outcome();
}

void TestEndToEnd::sendFax_data()
{
    QTest::addColumn<bool>("t38ReInvite");
    QTest::newRow("g711") << false;
    QTest::newRow("t38-reinvite-rejected") << true;
}

void TestEndToEnd::sendFax()
{
    QFETCH(bool, t38ReInvite);

    MockFritzBox::Options mockOptions;
    mockOptions.rtp = m_fastRtp;
    mockOptions.rxTiffPath = m_dir.filePath(QStringLiteral("received-%1.tif").arg(QLatin1String(QTest::currentDataTag())));
    mockOptions.sendT38ReInvite = t38ReInvite;
    MockFritzBox mock(mockOptions);
    QVERIFY(mock.listen());

    FaxJob job(makeConfig(mock, mockOptions.password));
    QStringList trace;
    QObject::connect(&job, &FaxJob::protocolTrace, [&trace](bool outgoing, const QString &, const QByteArray &data) {
        trace.append((outgoing ? QStringLiteral(">> ") : QStringLiteral("<< ")) + QString::fromUtf8(data.left(data.indexOf('\r'))));
    });
    const FaxOutcome outcome = run(job);
    if (outcome.status != FaxStatus::Success)
        qInfo("SIP trace:\n%s", qPrintable(trace.join(QLatin1Char('\n'))));

    QVERIFY2(outcome.status == FaxStatus::Success,
             qPrintable(QStringLiteral("%1: %2").arg(faxStatusName(outcome.status), outcome.message)));
    QCOMPARE(outcome.fax.pagesTransferred, 3);
    QVERIFY2(mock.errors().isEmpty(), qPrintable(mock.errors().join(QLatin1Char('\n'))));
    QCOMPARE(mock.registerChallenges(), 1);
    QCOMPARE(mock.inviteChallenges(), 1);
    QVERIFY(mock.ackForChallengeSeen());
    // The mock sends its 200 OK twice; every copy must be acknowledged.
    QCOMPARE(mock.acksFor200(), 2);
    QVERIFY(mock.faxFinished());
    QVERIFY2(mock.faxResult().success, qPrintable(mock.faxResult().completionText));
    QCOMPARE(mock.faxResult().pagesTransferred, 3);
    QVERIFY(mock.byeAnswered());
    QVERIFY(mock.unregistered());
    QCOMPARE(mock.t38Rejected(), t38ReInvite);

    FaxTiffInfo info;
    QString error;
    QVERIFY2(readFaxTiffInfo(mockOptions.rxTiffPath, &info, &error), qPrintable(error));
    QCOMPARE(info.pages, 3);
    for (int i = 0; i < info.pages; ++i)
        QCOMPARE(info.widths.at(i), 1728);
}

void TestEndToEnd::wrongPassword()
{
    MockFritzBox::Options mockOptions;
    mockOptions.rtp = m_fastRtp;
    MockFritzBox mock(mockOptions);
    QVERIFY(mock.listen());

    FaxJob job(makeConfig(mock, QStringLiteral("wrong")));
    const FaxOutcome outcome = run(job);
    QCOMPARE(outcome.status, FaxStatus::AuthenticationFailed);
    // One retry with credentials, then give up when the same nonce comes back.
    QCOMPARE(mock.registerChallenges(), 2);
    QCOMPARE(mock.inviteChallenges(), 0);
    QVERIFY(!mock.registered());
}

void TestEndToEnd::busy()
{
    MockFritzBox::Options mockOptions;
    mockOptions.rtp = m_fastRtp;
    mockOptions.rejectInviteWith = 486;
    MockFritzBox mock(mockOptions);
    QVERIFY(mock.listen());

    FaxJob job(makeConfig(mock, mockOptions.password));
    const FaxOutcome outcome = run(job);
    QCOMPARE(outcome.status, FaxStatus::CallFailed);
    QCOMPARE(outcome.sipStatus, 486);
    QVERIFY2(mock.errors().isEmpty(), qPrintable(mock.errors().join(QLatin1Char('\n'))));
    QVERIFY(mock.unregistered());
}

QTEST_MAIN(TestEndToEnd)
#include "tst_endtoend.moc"
