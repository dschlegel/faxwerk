#include "sip/DigestAuth.h"
#include "sip/SipMessage.h"

#include <QCryptographicHash>
#include <QtTest>

using namespace faxwerk;

class TestDigestAuth : public QObject
{
    Q_OBJECT

private slots:
    void rfc2617Vector();
    void authorizeRfc2617Challenge();
    void legacyWithoutQop();
    void md5Sess();
    void sha256Rfc7616();
    void parsesChallenge();
    void rejectsUnsupported();
};

namespace {

QByteArray md5(const QByteArray &data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex();
}

// Extracts a parameter value from an Authorization header value.
QString authParam(const QString &header, const QString &name)
{
    for (const QString &part : sip::splitList(header.mid(header.indexOf(QLatin1Char(' ')) + 1))) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (part.left(eq).trimmed() == name)
            return sip::unquote(part.mid(eq + 1));
    }
    return QString();
}

const QString kRfcChallenge = QStringLiteral(
    "Digest realm=\"testrealm@host.com\", qop=\"auth,auth-int\", "
    "nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\", opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"");

} // namespace

void TestDigestAuth::rfc2617Vector()
{
    const QByteArray response = DigestAuth::computeResponse(
        DigestAuth::Algorithm::Md5, QStringLiteral("Mufasa"), QStringLiteral("testrealm@host.com"),
        QStringLiteral("Circle Of Life"), QStringLiteral("GET"), QStringLiteral("/dir/index.html"),
        QStringLiteral("dcd98b7102dd2f0e8b11d0f600bfb0c093"), QStringLiteral("00000001"),
        QStringLiteral("0a4f113b"), QStringLiteral("auth"));
    QCOMPARE(response, QByteArray("6629fae49393a05397450978507c4ef1"));
}

void TestDigestAuth::authorizeRfc2617Challenge()
{
    const auto challenge = DigestChallenge::parse(kRfcChallenge);
    QVERIFY(challenge);
    DigestAuth auth(QStringLiteral("Mufasa"), QStringLiteral("Circle Of Life"));

    const QString header = auth.authorize(*challenge, QStringLiteral("GET"), QStringLiteral("/dir/index.html"),
                                          QByteArray(), QStringLiteral("0a4f113b"));
    QVERIFY(header.startsWith(QStringLiteral("Digest ")));
    QCOMPARE(authParam(header, QStringLiteral("response")), QStringLiteral("6629fae49393a05397450978507c4ef1"));
    QCOMPARE(authParam(header, QStringLiteral("username")), QStringLiteral("Mufasa"));
    QCOMPARE(authParam(header, QStringLiteral("qop")), QStringLiteral("auth"));
    QCOMPARE(authParam(header, QStringLiteral("nc")), QStringLiteral("00000001"));
    QCOMPARE(authParam(header, QStringLiteral("cnonce")), QStringLiteral("0a4f113b"));
    QCOMPARE(authParam(header, QStringLiteral("opaque")), QStringLiteral("5ccc069c403ebaf9f0171e9517f40e41"));
    QCOMPARE(authParam(header, QStringLiteral("uri")), QStringLiteral("/dir/index.html"));

    // The nonce count increases per use of the same nonce ...
    const QString second = auth.authorize(*challenge, QStringLiteral("GET"), QStringLiteral("/dir/index.html"),
                                          QByteArray(), QStringLiteral("0a4f113b"));
    QCOMPARE(authParam(second, QStringLiteral("nc")), QStringLiteral("00000002"));
    QVERIFY(authParam(second, QStringLiteral("response")) != authParam(header, QStringLiteral("response")));
    QCOMPARE(auth.nonceCount(challenge->nonce), 2);

    // ... and starts again for a new nonce.
    DigestChallenge other = *challenge;
    other.nonce = QStringLiteral("fresh");
    const QString third = auth.authorize(other, QStringLiteral("GET"), QStringLiteral("/"));
    QCOMPARE(authParam(third, QStringLiteral("nc")), QStringLiteral("00000001"));
    QVERIFY(!authParam(third, QStringLiteral("cnonce")).isEmpty());
}

void TestDigestAuth::legacyWithoutQop()
{
    // FRITZ!Box style challenge without qop (RFC 2069 compatibility).
    const auto challenge = DigestChallenge::parse(QStringLiteral("Digest realm=\"fritz.box\", nonce=\"5F3A1B2C\""));
    QVERIFY(challenge);
    QVERIFY(challenge->qop.isEmpty());
    DigestAuth auth(QStringLiteral("620"), QStringLiteral("geheim"));
    const QString header = auth.authorize(*challenge, QStringLiteral("REGISTER"), QStringLiteral("sip:fritz.box"));

    const QByteArray ha1 = md5("620:fritz.box:geheim");
    const QByteArray ha2 = md5("REGISTER:sip:fritz.box");
    QCOMPARE(authParam(header, QStringLiteral("response")), QString::fromLatin1(md5(ha1 + ":5F3A1B2C:" + ha2)));
    QVERIFY(authParam(header, QStringLiteral("nc")).isEmpty());
    QVERIFY(authParam(header, QStringLiteral("qop")).isEmpty());
    QVERIFY(authParam(header, QStringLiteral("cnonce")).isEmpty());
    QVERIFY(!header.contains(QStringLiteral("algorithm")));
}

void TestDigestAuth::md5Sess()
{
    const auto challenge = DigestChallenge::parse(
        QStringLiteral("Digest realm=\"r\", nonce=\"n\", qop=\"auth\", algorithm=MD5-sess"));
    QVERIFY(challenge);
    DigestAuth auth(QStringLiteral("u"), QStringLiteral("p"));
    const QString header = auth.authorize(*challenge, QStringLiteral("INVITE"), QStringLiteral("sip:1@r"),
                                          QByteArray(), QStringLiteral("cn"));
    const QByteArray ha1 = md5(md5("u:r:p") + ":n:cn");
    const QByteArray ha2 = md5("INVITE:sip:1@r");
    const QByteArray expected = md5(ha1 + ":n:00000001:cn:auth:" + ha2);
    QCOMPARE(authParam(header, QStringLiteral("response")), QString::fromLatin1(expected));
    QCOMPARE(authParam(header, QStringLiteral("algorithm")), QStringLiteral("MD5-sess"));
}

void TestDigestAuth::sha256Rfc7616()
{
    // RFC 7616 section 3.9.1
    const QString nonce = QStringLiteral("7ypf/xlj9XXwfDPEoM4URrv/xwf94BcCAzFZH4GiTo0v");
    const QString cnonce = QStringLiteral("f2/wE4q74E6zIJEtWaHKaf5wv/H5QzzpXusqGemxURZJ");
    const QString realm = QStringLiteral("http-auth@example.org");
    QCOMPARE(DigestAuth::computeResponse(DigestAuth::Algorithm::Sha256, QStringLiteral("Mufasa"), realm,
                                         QStringLiteral("Circle of Life"), QStringLiteral("GET"),
                                         QStringLiteral("/dir/index.html"), nonce, QStringLiteral("00000001"),
                                         cnonce, QStringLiteral("auth")),
             QByteArray("753927fa0e85d155564e2e272a28d1802ca10daf4496794697cf8db5856cb6c1"));
    QCOMPARE(DigestAuth::computeResponse(DigestAuth::Algorithm::Md5, QStringLiteral("Mufasa"), realm,
                                         QStringLiteral("Circle of Life"), QStringLiteral("GET"),
                                         QStringLiteral("/dir/index.html"), nonce, QStringLiteral("00000001"),
                                         cnonce, QStringLiteral("auth")),
             QByteArray("8ca523f5e9506fed4657c9700eebdbec"));
}

void TestDigestAuth::parsesChallenge()
{
    const auto c = DigestChallenge::parse(QStringLiteral(
        "digest realm=\"fritz.box\",nonce=\"AB,CD\", opaque=\"op\", stale=TRUE, algorithm=SHA-256, qop=\"auth\""));
    QVERIFY(c);
    QCOMPARE(c->realm, QStringLiteral("fritz.box"));
    QCOMPARE(c->nonce, QStringLiteral("AB,CD"));
    QCOMPARE(c->opaque, QStringLiteral("op"));
    QVERIFY(c->stale);
    QCOMPARE(c->algorithm, QStringLiteral("SHA-256"));
    QCOMPARE(c->qop, QStringList{QStringLiteral("auth")});

    QVERIFY(!DigestChallenge::parse(QStringLiteral("Basic realm=\"x\"")));
    QVERIFY(!DigestChallenge::parse(QStringLiteral("Digest realm=\"x\"")));
}

void TestDigestAuth::rejectsUnsupported()
{
    DigestAuth auth(QStringLiteral("u"), QStringLiteral("p"));
    auto challenge = DigestChallenge::parse(QStringLiteral("Digest realm=\"r\", nonce=\"n\", algorithm=SHA-512-256"));
    QVERIFY(challenge);
    QVERIFY(auth.authorize(*challenge, QStringLiteral("REGISTER"), QStringLiteral("sip:r")).isEmpty());
    challenge = DigestChallenge::parse(QStringLiteral("Digest realm=\"r\", nonce=\"n\", qop=\"auth-conf\""));
    QVERIFY(challenge);
    QVERIFY(auth.authorize(*challenge, QStringLiteral("REGISTER"), QStringLiteral("sip:r")).isEmpty());
}

QTEST_GUILESS_MAIN(TestDigestAuth)
#include "tst_digestauth.moc"
