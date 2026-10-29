#include "sip/SipMessage.h"

#include <QtTest>

using namespace faxwerk;

class TestSipMessage : public QObject
{
    Q_OBJECT

private slots:
    void parsesRequestWithCompactForms();
    void parsesResponse();
    void unfoldsHeaders();
    void splitsListHeaders();
    void honoursContentLength();
    void rejectsInvalidMessages();
    void toleratesBareLineFeeds();
    void roundtrip();
    void canonicalNames();
    void nameAddr();
    void sipUri();
    void params();
};

static QByteArray crlf(const char *text)
{
    QByteArray data(text);
    data.replace("\n", "\r\n");
    return data;
}

void TestSipMessage::parsesRequestWithCompactForms()
{
    const QByteArray data = crlf("INVITE sip:bob@biloxi.com SIP/2.0\n"
                                 "v: SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bK776asdhds\n"
                                 "Max-Forwards: 70\n"
                                 "t: Bob <sip:bob@biloxi.com>\n"
                                 "f: Alice <sip:alice@atlanta.com>;tag=1928301774\n"
                                 "i: a84b4c76e66710@pc33.atlanta.com\n"
                                 "CSeq: 314159 INVITE\n"
                                 "m: <sip:alice@pc33.atlanta.com>\n"
                                 "c: application/sdp\n"
                                 "k: timer\n"
                                 "s: Hello\n"
                                 "l: 4\n"
                                 "\n"
                                 "v=0\n");
    QString error;
    const auto msg = SipMessage::parse(data, &error);
    QVERIFY2(msg, qPrintable(error));
    QVERIFY(msg->isRequest());
    QCOMPARE(msg->method(), QStringLiteral("INVITE"));
    QCOMPARE(msg->requestUri(), QStringLiteral("sip:bob@biloxi.com"));
    QCOMPARE(msg->header(QStringLiteral("Via")), QStringLiteral("SIP/2.0/UDP pc33.atlanta.com;branch=z9hG4bK776asdhds"));
    QCOMPARE(msg->header(QStringLiteral("via")), msg->header(QStringLiteral("v")));
    QCOMPARE(msg->header(QStringLiteral("TO")), QStringLiteral("Bob <sip:bob@biloxi.com>"));
    QCOMPARE(msg->callId(), QStringLiteral("a84b4c76e66710@pc33.atlanta.com"));
    QCOMPARE(msg->cseqNumber(), 314159);
    QCOMPARE(msg->cseqMethod(), QStringLiteral("INVITE"));
    QCOMPARE(msg->fromTag(), QStringLiteral("1928301774"));
    QVERIFY(msg->toTag().isEmpty());
    QCOMPARE(msg->topViaBranch(), QStringLiteral("z9hG4bK776asdhds"));
    QCOMPARE(msg->header(QStringLiteral("Contact")), QStringLiteral("<sip:alice@pc33.atlanta.com>"));
    QCOMPARE(msg->header(QStringLiteral("Content-Type")), QStringLiteral("application/sdp"));
    QCOMPARE(msg->header(QStringLiteral("Supported")), QStringLiteral("timer"));
    QCOMPARE(msg->header(QStringLiteral("Subject")), QStringLiteral("Hello"));
    QCOMPARE(msg->body(), QByteArray("v=0\r"));
}

void TestSipMessage::parsesResponse()
{
    const auto msg = SipMessage::parse(crlf("SIP/2.0 401 Unauthorized\n"
                                            "Via: SIP/2.0/UDP 192.168.178.20:5060;rport=5060;branch=z9hG4bKabc\n"
                                            "From: <sip:620@fritz.box>;tag=aa\n"
                                            "To: <sip:620@fritz.box>;tag=bb\n"
                                            "Call-ID: 1@x\n"
                                            "CSeq: 1 REGISTER\n"
                                            "WWW-Authenticate: Digest realm=\"fritz.box\", nonce=\"ABC\"\n"
                                            "Content-Length: 0\n\n"));
    QVERIFY(msg);
    QVERIFY(msg->isResponse());
    QCOMPARE(msg->statusCode(), 401);
    QCOMPARE(msg->reasonPhrase(), QStringLiteral("Unauthorized"));
    QCOMPARE(msg->toTag(), QStringLiteral("bb"));
    QCOMPARE(msg->cseqMethod(), QStringLiteral("REGISTER"));
    // Authenticate headers are not split at commas.
    QCOMPARE(msg->headerValues(QStringLiteral("www-authenticate")).size(), 1);
    QVERIFY(msg->body().isEmpty());
}

void TestSipMessage::unfoldsHeaders()
{
    const auto msg = SipMessage::parse(crlf("OPTIONS sip:a@b SIP/2.0\n"
                                            "Via: SIP/2.0/UDP h;branch=z9hG4bK1\n"
                                            "From: <sip:x@y>;tag=1\n"
                                            "To: <sip:a@b>\n"
                                            "Call-ID: c\n"
                                            "CSeq: 1 OPTIONS\n"
                                            "Subject: I know you're there,\n"
                                            "     pick up the phone\n"
                                            "\t and talk to me!\n"
                                            "\n"));
    QVERIFY(msg);
    QCOMPARE(msg->header(QStringLiteral("Subject")),
             QStringLiteral("I know you're there, pick up the phone and talk to me!"));
}

void TestSipMessage::splitsListHeaders()
{
    const auto msg = SipMessage::parse(crlf("SIP/2.0 200 OK\n"
                                            "Via: SIP/2.0/UDP a;branch=z9hG4bK1, SIP/2.0/UDP b;branch=z9hG4bK2\n"
                                            "v: SIP/2.0/UDP c;branch=z9hG4bK3\n"
                                            "Record-Route: <sip:p1;lr>, \"Proxy, Two\" <sip:p2;lr>\n"
                                            "From: <sip:x@y>;tag=1\n"
                                            "To: <sip:a@b>;tag=2\n"
                                            "Call-ID: c\n"
                                            "CSeq: 1 INVITE\n"
                                            "\n"));
    QVERIFY(msg);
    const QStringList vias = msg->headerValues(QStringLiteral("Via"));
    QCOMPARE(vias.size(), 3);
    QCOMPARE(msg->topViaBranch(), QStringLiteral("z9hG4bK1"));
    QCOMPARE(sip::param(vias.at(2), QStringLiteral("branch")), QStringLiteral("z9hG4bK3"));
    const QStringList routes = msg->headerValues(QStringLiteral("record-route"));
    QCOMPARE(routes.size(), 2);
    QCOMPARE(routes.at(1), QStringLiteral("\"Proxy, Two\" <sip:p2;lr>"));
}

void TestSipMessage::honoursContentLength()
{
    const QByteArray head = crlf("MESSAGE sip:a@b SIP/2.0\n"
                                 "Via: SIP/2.0/UDP h;branch=z9hG4bK1\n"
                                 "From: <sip:x@y>;tag=1\n"
                                 "To: <sip:a@b>\n"
                                 "Call-ID: c\n"
                                 "CSeq: 1 MESSAGE\n");
    auto longer = SipMessage::parse(head + "Content-Length: 5\r\n\r\nhello world");
    QVERIFY(longer);
    QCOMPARE(longer->body(), QByteArray("hello"));

    QString error;
    QVERIFY(!SipMessage::parse(head + "Content-Length: 50\r\n\r\nhello", &error));
    QVERIFY(error.contains(QStringLiteral("truncated")));

    auto noLength = SipMessage::parse(head + "\r\nabc");
    QVERIFY(noLength);
    QCOMPARE(noLength->body(), QByteArray("abc"));
}

void TestSipMessage::rejectsInvalidMessages()
{
    QVERIFY(!SipMessage::parse(QByteArray()));
    QVERIFY(!SipMessage::parse("\r\n\r\n"));
    QVERIFY(!SipMessage::parse("HELLO\r\n\r\n"));
    QVERIFY(!SipMessage::parse("SIP/2.0 99 Too Small\r\nVia: x\r\n\r\n"));
    // Missing Call-ID
    QVERIFY(!SipMessage::parse(crlf("OPTIONS sip:a@b SIP/2.0\nVia: SIP/2.0/UDP h\nFrom: <sip:x@y>\nTo: <sip:a@b>\nCSeq: 1 OPTIONS\n\n")));
}

void TestSipMessage::toleratesBareLineFeeds()
{
    const auto msg = SipMessage::parse("BYE sip:a@b SIP/2.0\nVia: SIP/2.0/UDP h;branch=z9hG4bK9\nFrom: <sip:x@y>;tag=1\n"
                                       "To: <sip:a@b>;tag=2\nCall-ID: c\nCSeq: 2 BYE\nContent-Length: 0\n\n");
    QVERIFY(msg);
    QCOMPARE(msg->method(), QStringLiteral("BYE"));
    QCOMPARE(msg->topViaBranch(), QStringLiteral("z9hG4bK9"));
}

void TestSipMessage::roundtrip()
{
    SipMessage msg = SipMessage::request(QStringLiteral("INVITE"), QStringLiteral("sip:0301234567@fritz.box"));
    msg.addHeader(QStringLiteral("v"), QStringLiteral("SIP/2.0/UDP 10.0.0.2:5062;rport;branch=z9hG4bKx"));
    msg.addHeader(QStringLiteral("from"), QStringLiteral("\"Fax\" <sip:620@fritz.box>;tag=abc"));
    msg.addHeader(QStringLiteral("to"), QStringLiteral("<sip:0301234567@fritz.box>"));
    msg.addHeader(QStringLiteral("call-id"), QStringLiteral("id@10.0.0.2"));
    msg.addHeader(QStringLiteral("cseq"), QStringLiteral("2 INVITE"));
    msg.addHeader(QStringLiteral("Content-Length"), QStringLiteral("999")); // recomputed
    msg.setBody("v=0\r\n", QStringLiteral("application/sdp"));
    msg.prependHeader(QStringLiteral("Via"), QStringLiteral("SIP/2.0/UDP proxy;branch=z9hG4bKtop"));

    const QByteArray wire = msg.serialize();
    QVERIFY(wire.startsWith("INVITE sip:0301234567@fritz.box SIP/2.0\r\n"));
    QVERIFY(wire.contains("\r\nCall-ID: id@10.0.0.2\r\n"));
    QVERIFY(wire.contains("\r\nCSeq: 2 INVITE\r\n"));
    QVERIFY(wire.contains("\r\nContent-Length: 5\r\n\r\nv=0\r\n"));
    QVERIFY(!wire.contains("999"));
    QVERIFY(wire.indexOf("branch=z9hG4bKtop") < wire.indexOf("branch=z9hG4bKx"));

    const auto parsed = SipMessage::parse(wire);
    QVERIFY(parsed);
    QCOMPARE(parsed->method(), msg.method());
    QCOMPARE(parsed->requestUri(), msg.requestUri());
    QCOMPARE(parsed->body(), msg.body());
    QCOMPARE(parsed->topViaBranch(), QStringLiteral("z9hG4bKtop"));
    QCOMPARE(parsed->headerValues(QStringLiteral("Via")).size(), 2);
    QCOMPARE(parsed->fromTag(), QStringLiteral("abc"));
    QCOMPARE(parsed->serialize(), wire);

    SipMessage response = SipMessage::response(486, QString());
    QCOMPARE(response.reasonPhrase(), QStringLiteral("Busy Here"));
}

void TestSipMessage::canonicalNames()
{
    QCOMPARE(SipMessage::canonicalHeaderName(QStringLiteral("www-authenticate")), QStringLiteral("WWW-Authenticate"));
    QCOMPARE(SipMessage::canonicalHeaderName(QStringLiteral("CSEQ")), QStringLiteral("CSeq"));
    QCOMPARE(SipMessage::canonicalHeaderName(QStringLiteral("call-id")), QStringLiteral("Call-ID"));
    QCOMPARE(SipMessage::canonicalHeaderName(QStringLiteral("record-route")), QStringLiteral("Record-Route"));
    QCOMPARE(SipMessage::canonicalHeaderName(QStringLiteral("K")), QStringLiteral("Supported"));
    QCOMPARE(SipMessage::canonicalHeaderName(QStringLiteral("l")), QStringLiteral("Content-Length"));
    QCOMPARE(SipMessage::canonicalHeaderName(QStringLiteral("x-custom-thing")), QStringLiteral("X-Custom-Thing"));
}

void TestSipMessage::nameAddr()
{
    SipNameAddr a = SipNameAddr::parse(QStringLiteral("\"Doe, John\" <sip:john@example.com;lr>;tag=xyz;foo"));
    QCOMPARE(a.displayName, QStringLiteral("Doe, John"));
    QCOMPARE(a.uri, QStringLiteral("sip:john@example.com;lr"));
    QCOMPARE(a.param(QStringLiteral("tag")), QStringLiteral("xyz"));
    a.setParam(QStringLiteral("tag"), QStringLiteral("new"));
    QCOMPARE(a.toString(), QStringLiteral("\"Doe, John\" <sip:john@example.com;lr>;tag=new;foo"));

    SipNameAddr plain = SipNameAddr::parse(QStringLiteral("sip:alice@atlanta.com;tag=88"));
    QCOMPARE(plain.uri, QStringLiteral("sip:alice@atlanta.com"));
    QCOMPARE(plain.param(QStringLiteral("tag")), QStringLiteral("88"));

    SipNameAddr noTag = SipNameAddr::parse(QStringLiteral("<sip:bob@biloxi.com>"));
    noTag.setParam(QStringLiteral("tag"), QStringLiteral("t1"));
    QCOMPARE(noTag.toString(), QStringLiteral("<sip:bob@biloxi.com>;tag=t1"));
}

void TestSipMessage::sipUri()
{
    const auto uri = SipUri::parse(QStringLiteral("sip:**610@192.168.178.1:5070;transport=udp;lr"));
    QVERIFY(uri);
    QCOMPARE(uri->user, QStringLiteral("**610"));
    QCOMPARE(uri->host, QStringLiteral("192.168.178.1"));
    QCOMPARE(uri->port, quint16(5070));
    QVERIFY(uri->hasParam(QStringLiteral("lr")));
    QCOMPARE(uri->toString(), QStringLiteral("sip:**610@192.168.178.1:5070;transport=udp;lr"));

    const auto v6 = SipUri::parse(QStringLiteral("<sip:[fd00::1]:5060>"));
    QVERIFY(v6);
    QCOMPARE(v6->host, QStringLiteral("fd00::1"));
    QCOMPARE(v6->port, quint16(5060));
    QCOMPARE(v6->toString(), QStringLiteral("sip:[fd00::1]:5060"));

    QVERIFY(!SipUri::parse(QStringLiteral("tel:+4930123")));
    QVERIFY(!SipUri::parse(QStringLiteral("sip:")));
}

void TestSipMessage::params()
{
    const QString via = QStringLiteral("SIP/2.0/UDP 10.0.0.1:5060;rport=5062;received=1.2.3.4;branch=z9hG4bKq");
    QCOMPARE(sip::param(via, QStringLiteral("rport")), QStringLiteral("5062"));
    QCOMPARE(sip::param(via, QStringLiteral("BRANCH")), QStringLiteral("z9hG4bKq"));
    QVERIFY(sip::hasParam(QStringLiteral("SIP/2.0/UDP h;rport;branch=z"), QStringLiteral("rport")));
    QVERIFY(!sip::hasParam(via, QStringLiteral("maddr")));
    QVERIFY(sip::generateBranch().startsWith(QStringLiteral("z9hG4bK")));
    QVERIFY(sip::generateTag() != sip::generateTag());
    QCOMPARE(sip::unquote(QStringLiteral("\"a \\\"b\\\" c\"")), QStringLiteral("a \"b\" c"));
    QCOMPARE(sip::quote(QStringLiteral("a \"b\"")), QStringLiteral("\"a \\\"b\\\"\""));
}

QTEST_GUILESS_MAIN(TestSipMessage)
#include "tst_sipmessage.moc"
