#include "rtp/G711.h"
#include "rtp/RtpSession.h"

#include <QUdpSocket>
#include <QtTest>

#include <vector>

using namespace faxwerk;

namespace {

class RecordingPort : public AudioPort
{
public:
    void receiveAudio(const std::int16_t *samples, int count) override
    {
        received.insert(received.end(), samples, samples + count);
    }
    void receiveGap(int count) override { gaps.push_back(count); }
    void produceAudio(std::int16_t *samples, int count) override
    {
        for (int i = 0; i < count; ++i)
            samples[i] = std::int16_t(1000);
        ++produced;
    }

    std::vector<std::int16_t> received;
    std::vector<int> gaps;
    int produced = 0;
};

QByteArray rtpPacket(quint16 seq, quint32 ssrc, int payloadType = 8, int samples = 160)
{
    QByteArray p(12 + samples, char(g711::linearToAlaw(500)));
    p[0] = char(0x80);
    p[1] = char(payloadType);
    p[2] = char(seq >> 8);
    p[3] = char(seq);
    for (int i = 0; i < 4; ++i) {
        p[4 + i] = 0;
        p[8 + i] = char(ssrc >> (24 - 8 * i));
    }
    return p;
}

} // namespace

class TestRtpSession : public QObject
{
    Q_OBJECT

private slots:
    void pacedTransmission();
    void receiveHandling();
};

void TestRtpSession::pacedTransmission()
{
    RtpSession::Options options;
    options.packetInterval = std::chrono::milliseconds(2);
    RtpSession sender(options);
    RtpSession receiver(options);
    RecordingPort tx;
    RecordingPort rx;
    QVERIFY(sender.open(QHostAddress::LocalHost));
    QVERIFY(receiver.open(QHostAddress::LocalHost));
    QCOMPARE(sender.localPort() % 2, 0);
    sender.setRemote(QHostAddress::LocalHost, receiver.localPort(), 8);
    sender.setAudioPort(&tx);
    receiver.setAudioPort(&rx);

    QElapsedTimer clock;
    clock.start();
    sender.start();
    QTRY_VERIFY_WITH_TIMEOUT(sender.statistics().packetsSent >= 200, 5000);
    sender.stop();
    const qint64 elapsed = clock.elapsed();
    // 200 packets at 2 ms are due after ~398 ms; drift-free pacing keeps us close.
    QVERIFY2(elapsed >= 390 && elapsed < 1500, qPrintable(QString::number(elapsed)));

    QTRY_COMPARE_WITH_TIMEOUT(receiver.statistics().packetsReceived, sender.statistics().packetsSent, 2000);
    QCOMPARE(rx.received.size(), size_t(160 * sender.statistics().packetsSent));
    QVERIFY(std::abs(rx.received.front() - 1000) < 64);
    QVERIFY(rx.gaps.empty());
}

void TestRtpSession::receiveHandling()
{
    RtpSession session;
    RecordingPort rx;
    QVERIFY(session.open(QHostAddress::LocalHost));
    session.setAudioPort(&rx);
    QUdpSocket peer;
    auto send = [&](const QByteArray &packet) {
        peer.writeDatagram(packet, QHostAddress::LocalHost, session.localPort());
    };

    send(rtpPacket(100, 0x1111));
    send(rtpPacket(101, 0x1111));
    send(rtpPacket(104, 0x1111));          // 102, 103 lost
    send(rtpPacket(103, 0x1111));          // late: dropped
    send(rtpPacket(105, 0x1111, 101));     // telephone-event: dropped
    send(rtpPacket(7, 0x2222));            // new SSRC: resync, no gap
    send(rtpPacket(8, 0x2222));
    send(QByteArray("junk"));

    QTRY_COMPARE_WITH_TIMEOUT(session.statistics().packetsReceived, quint64(5), 2000);
    QTRY_COMPARE_WITH_TIMEOUT(session.statistics().packetsDropped, quint64(3), 2000);
    QCOMPARE(session.statistics().packetsLost, quint64(2));
    QCOMPARE(session.statistics().ssrcChanges, 1);
    QCOMPARE(rx.gaps, std::vector<int>{320});
    QCOMPARE(rx.received.size(), size_t(5 * 160));
}

QTEST_GUILESS_MAIN(TestRtpSession)
#include "tst_rtpsession.moc"
