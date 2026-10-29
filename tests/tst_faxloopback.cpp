#include "TestDocuments.h"

#include "document/FaxTiff.h"
#include "fax/FaxEngine.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <array>

using namespace faxwerk;

// Two FaxEngines exchange audio directly (no network, no real-time pacing).
class TestFaxLoopback : public QObject
{
    Q_OBJECT

private slots:
    void transfer_data();
    void transfer();
};

void TestFaxLoopback::transfer_data()
{
    QTest::addColumn<bool>("ecm");
    QTest::addColumn<int>("maxRate");
    QTest::newRow("ecm-9600") << true << 9600;
    QTest::newRow("no-ecm-9600") << false << 9600;
    QTest::newRow("ecm-14400") << true << 14400;
}

void TestFaxLoopback::transfer()
{
    QFETCH(bool, ecm);
    QFETCH(int, maxRate);

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString txFile = dir.filePath(QStringLiteral("tx.tif"));
    const QString rxFile = dir.filePath(QStringLiteral("rx.tif"));
    QString error;
    QVERIFY2(writeFaxTiff(txFile, test::makeTestFaxPages(3), QStringLiteral("test"), &error), qPrintable(error));

    FaxOptions senderOptions;
    senderOptions.stationId = QStringLiteral("+49 30 1111111");
    senderOptions.headerInfo = QStringLiteral("Loopback test");
    senderOptions.ecm = ecm;
    senderOptions.maxBitRate = maxRate;
    FaxOptions receiverOptions = senderOptions;
    receiverOptions.stationId = QStringLiteral("+49 30 2222222");

    FaxEngine sender(FaxEngine::Role::Send, true, senderOptions);
    FaxEngine receiver(FaxEngine::Role::Receive, false, receiverOptions);
    QSignalSpy senderDone(&sender, &FaxEngine::finished);
    QSignalSpy receiverDone(&receiver, &FaxEngine::finished);
    QSignalSpy pages(&sender, &FaxEngine::pageTransferred);
    QSignalSpy negotiated(&sender, &FaxEngine::negotiated);
    QVERIFY(sender.start(txFile, &error));
    QVERIFY(receiver.start(rxFile, &error));

    std::array<std::int16_t, 160> a {};
    std::array<std::int16_t, 160> b {};
    // At most 10 minutes of audio.
    int block = 0;
    for (; block < 10 * 60 * 50; ++block) {
        sender.produceAudio(a.data(), int(a.size()));
        receiver.produceAudio(b.data(), int(b.size()));
        receiver.receiveAudio(a.data(), int(a.size()));
        sender.receiveAudio(b.data(), int(b.size()));
        if (sender.isFinished() && receiver.isFinished())
            break;
    }
    QVERIFY(sender.isFinished());
    QVERIFY(receiver.isFinished());
    qInfo("simulated %.1f s of audio", block / 50.0);
    QTRY_COMPARE(senderDone.count(), 1);
    QTRY_COMPARE(receiverDone.count(), 1);

    const FaxResult tx = sender.result();
    const FaxResult rx = receiver.result();
    QVERIFY2(tx.success, qPrintable(tx.completionText));
    QVERIFY2(rx.success, qPrintable(rx.completionText));
    QCOMPARE(tx.pagesTransferred, 3);
    QCOMPARE(rx.pagesTransferred, 3);
    QCOMPARE(tx.ecm, ecm);
    QCOMPARE(tx.bitRate, maxRate);
    QCOMPARE(rx.remoteIdent, QStringLiteral("+49 30 1111111"));
    QCOMPARE(tx.remoteIdent, QStringLiteral("+49 30 2222222"));
    QCOMPARE(pages.count(), 3);
    QVERIFY(negotiated.count() >= 1);

    FaxTiffInfo info;
    QVERIFY(readFaxTiffInfo(rxFile, &info, &error));
    QCOMPARE(info.pages, 3);
    QCOMPARE(info.widths.value(0), 1728);
}

QTEST_GUILESS_MAIN(TestFaxLoopback)
#include "tst_faxloopback.moc"
