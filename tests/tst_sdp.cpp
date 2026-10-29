#include "sip/Sdp.h"

#include <QtTest>

using namespace faxwerk;

class TestSdp : public QObject
{
    Q_OBJECT

private slots:
    void offer();
    void parsesAnswer();
    void answersOfferWithPreference();
    void detectsT38();
    void answersMixedOffer();
};

static QByteArray crlf(const char *text)
{
    QByteArray data(text);
    data.replace("\n", "\r\n");
    return data;
}

void TestSdp::offer()
{
    const SdpSession session = sdp::makeOffer(QHostAddress(QStringLiteral("10.0.0.2")), 40002, 1234, 1);
    const QByteArray text = session.toByteArray();
    QVERIFY(text.startsWith("v=0\r\n"));
    QVERIFY(text.contains("o=- 1234 1 IN IP4 10.0.0.2\r\n"));
    QVERIFY(text.contains("c=IN IP4 10.0.0.2\r\n"));
    QVERIFY(text.contains("m=audio 40002 RTP/AVP 8 0\r\n"));
    QVERIFY(text.contains("a=rtpmap:8 PCMA/8000\r\n"));
    QVERIFY(text.contains("a=rtpmap:0 PCMU/8000\r\n"));
    QVERIFY(text.contains("a=ptime:20\r\n"));
    QVERIFY(text.contains("a=sendrecv\r\n"));
    QVERIFY(!text.contains("telephone-event"));

    const auto parsed = SdpSession::parse(text);
    QVERIFY(parsed);
    QCOMPARE(parsed->media.first().formats, (QStringList{QStringLiteral("8"), QStringLiteral("0")}));
    QCOMPARE(parsed->media.first().ptime, 20);
}

void TestSdp::parsesAnswer()
{
    const auto session = SdpSession::parse(crlf("v=0\n"
                                                "o=- 3212 3212 IN IP4 10.0.0.1\n"
                                                "s=-\n"
                                                "c=IN IP4 10.0.0.1\n"
                                                "t=0 0\n"
                                                "m=audio 7078 RTP/AVP 8 101\n"
                                                "a=rtpmap:8 PCMA/8000\n"
                                                "a=rtpmap:101 telephone-event/8000\n"
                                                "a=sendrecv\n"));
    QVERIFY(session);
    QVERIFY(!session->offersT38());
    const auto remote = sdp::remoteAudio(*session);
    QVERIFY(remote);
    QCOMPARE(remote->address, QHostAddress(QStringLiteral("10.0.0.1")));
    QCOMPARE(remote->port, quint16(7078));
    QCOMPARE(remote->payloadType, int(kPayloadPcma));

    // Media level c= overrides the session level; PCMU only.
    const auto ulaw = SdpSession::parse(crlf("v=0\no=- 1 1 IN IP4 10.0.0.1\ns=-\nc=IN IP4 10.0.0.1\nt=0 0\n"
                                             "m=audio 5004 RTP/AVP 0\nc=IN IP4 10.0.0.9\n"));
    QVERIFY(ulaw);
    const auto ulawRemote = sdp::remoteAudio(*ulaw);
    QVERIFY(ulawRemote);
    QCOMPARE(ulawRemote->payloadType, int(kPayloadPcmu));
    QCOMPARE(ulawRemote->address, QHostAddress(QStringLiteral("10.0.0.9")));
}

void TestSdp::answersOfferWithPreference()
{
    const auto offer = SdpSession::parse(crlf("v=0\no=- 1 1 IN IP4 10.0.0.1\ns=-\nc=IN IP4 10.0.0.1\nt=0 0\n"
                                              "m=audio 6000 RTP/AVP 0 8 101\n"));
    QVERIFY(offer);
    int chosen = -1;
    const auto answer = sdp::makeAnswer(*offer, QHostAddress(QStringLiteral("10.0.0.2")), 40000, 7, 2, &chosen);
    QVERIFY(answer);
    QCOMPARE(chosen, int(kPayloadPcma)); // our preference wins over the offer order
    const QByteArray text = answer->toByteArray();
    QVERIFY(text.contains("m=audio 40000 RTP/AVP 8\r\n"));
    QVERIFY(!text.contains("PCMU"));

    const auto g722 = SdpSession::parse(crlf("v=0\no=- 1 1 IN IP4 10.0.0.1\ns=-\nc=IN IP4 10.0.0.1\nt=0 0\n"
                                             "m=audio 6000 RTP/AVP 9\n"));
    QVERIFY(g722);
    QVERIFY(!sdp::makeAnswer(*g722, QHostAddress(QStringLiteral("10.0.0.2")), 40000, 7, 2));
}

void TestSdp::detectsT38()
{
    const auto offer = SdpSession::parse(crlf("v=0\n"
                                              "o=- 2 3 IN IP4 10.0.0.1\n"
                                              "s=-\n"
                                              "c=IN IP4 10.0.0.1\n"
                                              "t=0 0\n"
                                              "m=image 7080 udptl t38\n"
                                              "a=T38FaxVersion:0\n"
                                              "a=T38MaxBitRate:14400\n"));
    QVERIFY(offer);
    QVERIFY(offer->offersT38());
    QVERIFY(!offer->activeAudio());
    QVERIFY(!sdp::remoteAudio(*offer));
    QVERIFY(!sdp::makeAnswer(*offer, QHostAddress(QStringLiteral("10.0.0.2")), 40000, 1, 1));
}

void TestSdp::answersMixedOffer()
{
    // Audio plus T.38 offered: accept audio, reject the image stream with port 0.
    const auto offer = SdpSession::parse(crlf("v=0\no=- 1 1 IN IP4 10.0.0.1\ns=-\nc=IN IP4 10.0.0.1\nt=0 0\n"
                                              "m=image 7080 udptl t38\n"
                                              "m=audio 7078 RTP/AVP 8 0\n"));
    QVERIFY(offer);
    const auto answer = sdp::makeAnswer(*offer, QHostAddress(QStringLiteral("10.0.0.2")), 40000, 1, 1);
    QVERIFY(answer);
    QCOMPARE(answer->media.size(), 2);
    QCOMPARE(answer->media.at(0).port, quint16(0));
    QCOMPARE(answer->media.at(1).port, quint16(40000));
    QVERIFY(answer->toByteArray().contains("m=image 0 udptl t38\r\n"));
}

QTEST_GUILESS_MAIN(TestSdp)
#include "tst_sdp.moc"
