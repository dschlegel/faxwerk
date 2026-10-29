#include "MockFritzBox.h"

#include "sip/Sdp.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QNetworkDatagram>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QTimer>

namespace faxwerk::test {

namespace {

QByteArray md5Hex(const QString &text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5).toHex();
}

QHash<QString, QString> parseAuthorization(const QString &header)
{
    QHash<QString, QString> params;
    static const QRegularExpression re(QStringLiteral("(\\w+)\\s*=\\s*(\"([^\"]*)\"|[^,\\s]+)"));
    auto it = re.globalMatch(header);
    while (it.hasNext()) {
        const auto m = it.next();
        params.insert(m.captured(1).toLower(), m.captured(3).isNull() ? m.captured(2) : m.captured(3));
    }
    return params;
}

QString randomToken()
{
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray::number(QDateTime::currentMSecsSinceEpoch())
                                                            + QByteArray::number(QRandomGenerator::global()->generate64()),
                                                        QCryptographicHash::Md5)
                                   .toHex()
                                   .left(16));
}

} // namespace

MockFritzBox::MockFritzBox(const Options &options, QObject *parent)
    : QObject(parent)
    , m_options(options)
{
    connect(&m_socket, &QUdpSocket::readyRead, this, &MockFritzBox::onReadyRead);
}

MockFritzBox::~MockFritzBox()
{
    if (m_rtp)
        m_rtp->setAudioPort(nullptr);
}

bool MockFritzBox::listen()
{
    return m_socket.bind(QHostAddress::LocalHost, 0);
}

void MockFritzBox::error(const QString &message)
{
    qWarning("MockFritzBox: %s", qPrintable(message));
    m_errors.append(message);
}

void MockFritzBox::send(const QByteArray &data)
{
    m_socket.writeDatagram(data, m_peer, m_peerPort);
}

void MockFritzBox::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket.receiveDatagram();
        m_peer = datagram.senderAddress();
        m_peerPort = quint16(datagram.senderPort());
        QString parseError;
        const auto msg = SipMessage::parse(datagram.data(), &parseError);
        if (!msg) {
            error(QStringLiteral("unparsable datagram: %1").arg(parseError));
            continue;
        }
        if (msg->isRequest())
            handleRequest(*msg);
        else
            handleResponse(*msg);
    }
}

SipMessage MockFritzBox::makeResponse(const SipMessage &request, int code, const QString &toTag) const
{
    SipMessage response = SipMessage::response(code, QString());
    for (const QString &via : request.headerValues(QStringLiteral("Via"))) {
        // Fill in rport like a real registrar does.
        QString v = via;
        if (sip::hasParam(v, QStringLiteral("rport")) && sip::param(v, QStringLiteral("rport")).isEmpty())
            v.replace(QStringLiteral(";rport"), QStringLiteral(";rport=%1;received=%2").arg(m_peerPort).arg(m_peer.toString()));
        response.addHeader(QStringLiteral("Via"), v);
    }
    response.addHeader(QStringLiteral("From"), request.header(QStringLiteral("From")));
    SipNameAddr to = SipNameAddr::parse(request.header(QStringLiteral("To")));
    if (!toTag.isEmpty() && to.param(QStringLiteral("tag")).isEmpty())
        to.setParam(QStringLiteral("tag"), toTag);
    response.addHeader(QStringLiteral("To"), to.toString());
    response.addHeader(QStringLiteral("Call-ID"), request.callId());
    response.addHeader(QStringLiteral("CSeq"), request.header(QStringLiteral("CSeq")));
    response.addHeader(QStringLiteral("Server"), QStringLiteral("MockFritzBox"));
    return response;
}

bool MockFritzBox::verifyDigest(const QString &header, const QString &method, const QString &nonce, bool withQop)
{
    const auto p = parseAuthorization(header);
    if (!header.startsWith(QLatin1String("Digest "))) {
        error(QStringLiteral("%1: credentials are not Digest").arg(method));
        return false;
    }
    if (p.value(QStringLiteral("username")) != m_options.user || p.value(QStringLiteral("realm")) != m_options.realm
        || p.value(QStringLiteral("nonce")) != nonce || p.value(QStringLiteral("uri")).isEmpty()) {
        return false;
    }
    const QByteArray ha1 = md5Hex(QStringLiteral("%1:%2:%3").arg(m_options.user, m_options.realm, m_options.password));
    const QByteArray ha2 = md5Hex(QStringLiteral("%1:%2").arg(method, p.value(QStringLiteral("uri"))));
    QByteArray expected;
    if (withQop) {
        if (p.value(QStringLiteral("qop")) != QLatin1String("auth") || p.value(QStringLiteral("nc")).size() != 8
            || p.value(QStringLiteral("cnonce")).isEmpty()) {
            error(QStringLiteral("%1: qop/nc/cnonce missing").arg(method));
            return false;
        }
        expected = md5Hex(QStringLiteral("%1:%2:%3:%4:auth:%5")
                              .arg(QString::fromLatin1(ha1), nonce, p.value(QStringLiteral("nc")),
                                   p.value(QStringLiteral("cnonce")), QString::fromLatin1(ha2)));
    } else {
        if (p.contains(QStringLiteral("qop")))
            error(QStringLiteral("%1: qop sent although not offered").arg(method));
        expected = md5Hex(QStringLiteral("%1:%2:%3").arg(QString::fromLatin1(ha1), nonce, QString::fromLatin1(ha2)));
    }
    return p.value(QStringLiteral("response")).toLatin1() == expected;
}

void MockFritzBox::handleRequest(const SipMessage &request)
{
    const QString via = request.headerValues(QStringLiteral("Via")).value(0);
    if (!sip::hasParam(via, QStringLiteral("rport")))
        error(QStringLiteral("%1 without rport in Via").arg(request.method()));
    if (!request.topViaBranch().startsWith(QLatin1String("z9hG4bK")))
        error(QStringLiteral("%1 branch without magic cookie").arg(request.method()));

    const QString method = request.method();
    if (method == QLatin1String("REGISTER"))
        handleRegister(request);
    else if (method == QLatin1String("INVITE"))
        handleInvite(request);
    else if (method == QLatin1String("ACK"))
        handleAck(request);
    else if (method == QLatin1String("BYE")) {
        error(QStringLiteral("unexpected BYE from the client"));
        send(makeResponse(request, 200).serialize());
    }
    else
        send(makeResponse(request, 501).serialize());
}

void MockFritzBox::handleRegister(const SipMessage &request)
{
    const QString contact = SipNameAddr::parse(request.header(QStringLiteral("Contact"))).uri;
    const auto contactUri = SipUri::parse(contact);
    if (!contactUri || contactUri->host != QLatin1String("127.0.0.1") || contactUri->user != m_options.user)
        error(QStringLiteral("REGISTER with unexpected Contact %1").arg(contact));
    if (request.requestUri() != QStringLiteral("sip:127.0.0.1"))
        error(QStringLiteral("REGISTER Request-URI %1").arg(request.requestUri()));

    const QString credentials = request.header(QStringLiteral("Authorization"));
    if (credentials.isEmpty() || m_registerNonce.isEmpty()) {
        m_registerNonce = randomToken();
        ++m_registerChallenges;
        SipMessage response = makeResponse(request, 401, randomToken());
        response.addHeader(QStringLiteral("WWW-Authenticate"),
                           QStringLiteral("Digest realm=\"%1\", nonce=\"%2\", qop=\"auth\", algorithm=MD5")
                               .arg(m_options.realm, m_registerNonce));
        send(response.serialize());
        return;
    }
    if (!verifyDigest(credentials, QStringLiteral("REGISTER"), m_registerNonce, true)) {
        // Same nonce again: a correct client must give up instead of looping.
        ++m_registerChallenges;
        SipMessage response = makeResponse(request, 401, randomToken());
        response.addHeader(QStringLiteral("WWW-Authenticate"),
                           QStringLiteral("Digest realm=\"%1\", nonce=\"%2\", qop=\"auth\", algorithm=MD5")
                               .arg(m_options.realm, m_registerNonce));
        send(response.serialize());
        return;
    }

    const int expires = request.expires();
    SipMessage response = makeResponse(request, 200, randomToken());
    if (expires == 0) {
        m_unregistered = true;
    } else {
        m_registered = true;
        const int granted = qMin(expires, 300);
        response.addHeader(QStringLiteral("Contact"), QStringLiteral("<%1>;expires=%2").arg(contact).arg(granted));
        response.addHeader(QStringLiteral("Expires"), QString::number(granted));
    }
    send(response.serialize());
}

void MockFritzBox::handleInvite(const SipMessage &request)
{
    if (!request.toTag().isEmpty()) {
        error(QStringLiteral("unexpected re-INVITE from the client"));
        return;
    }
    if (!m_registered)
        error(QStringLiteral("INVITE before registration"));
    if (request.requestUri() != QStringLiteral("sip:0301234567@127.0.0.1"))
        error(QStringLiteral("INVITE Request-URI %1").arg(request.requestUri()));

    if (!m_invite.callId().isEmpty() && request.topViaBranch() == m_invite.topViaBranch())
        return; // retransmission of the authenticated INVITE
    if (!m_challengedInvite.callId().isEmpty() && request.topViaBranch() == m_challengedInvite.topViaBranch())
        return; // retransmission of the challenged INVITE

    const QString credentials = request.header(QStringLiteral("Proxy-Authorization"));
    if (credentials.isEmpty()) {
        if (!m_challengedInvite.callId().isEmpty())
            error(QStringLiteral("second INVITE without credentials"));
        m_challengedInvite = request;
        m_inviteNonce = randomToken();
        ++m_inviteChallenges;
        SipMessage response = makeResponse(request, 407, QStringLiteral("challenge-tag"));
        response.addHeader(QStringLiteral("Proxy-Authenticate"),
                           QStringLiteral("Digest realm=\"%1\", nonce=\"%2\"").arg(m_options.realm, m_inviteNonce));
        send(response.serialize());
        return;
    }

    // The re-sent INVITE: ACK first, same Call-ID and From tag, CSeq + 1, new branch.
    if (!m_ackFor407)
        error(QStringLiteral("authenticated INVITE before ACK for 407"));
    if (request.callId() != m_challengedInvite.callId())
        error(QStringLiteral("authenticated INVITE changed the Call-ID"));
    if (request.fromTag() != m_challengedInvite.fromTag())
        error(QStringLiteral("authenticated INVITE changed the From tag"));
    if (request.cseqNumber() != m_challengedInvite.cseqNumber() + 1)
        error(QStringLiteral("authenticated INVITE CSeq %1, expected %2")
                  .arg(request.cseqNumber())
                  .arg(m_challengedInvite.cseqNumber() + 1));
    if (request.topViaBranch() == m_challengedInvite.topViaBranch())
        error(QStringLiteral("authenticated INVITE reused the branch"));
    if (!verifyDigest(credentials, QStringLiteral("INVITE"), m_inviteNonce, false)) {
        error(QStringLiteral("INVITE digest verification failed"));
        send(makeResponse(request, 403, randomToken()).serialize());
        return;
    }

    m_invite = request;
    m_peerContact = SipNameAddr::parse(request.header(QStringLiteral("Contact"))).uri;
    m_toTag = randomToken();
    send(makeResponse(request, 100).serialize());

    if (m_options.rejectInviteWith > 0) {
        send(makeResponse(request, m_options.rejectInviteWith, m_toTag).serialize());
        return;
    }
    SipMessage ringing = makeResponse(request, 180, m_toTag);
    ringing.addHeader(QStringLiteral("Contact"), QStringLiteral("<sip:mock@127.0.0.1:%1>").arg(port()));
    send(ringing.serialize());
    QTimer::singleShot(50, this, &MockFritzBox::answerCall);
}

void MockFritzBox::answerCall()
{
    const auto offer = SdpSession::parse(m_invite.body());
    if (!offer) {
        error(QStringLiteral("INVITE without SDP"));
        return;
    }
    if (offer->offersT38() || m_invite.body().contains("telephone-event"))
        error(QStringLiteral("unexpected media in offer"));
    const auto remote = sdp::remoteAudio(*offer);
    if (!remote || remote->payloadType != kPayloadPcma) {
        error(QStringLiteral("offer does not prefer PCMA"));
        return;
    }

    m_rtp = std::make_unique<RtpSession>(m_options.rtp);
    if (!m_rtp->open(QHostAddress::LocalHost)) {
        error(QStringLiteral("cannot open RTP port"));
        return;
    }
    m_rtp->setRemote(remote->address, remote->port, remote->payloadType);

    FaxOptions faxOptions;
    faxOptions.stationId = QStringLiteral("+49 30 1234567");
    m_fax = std::make_unique<FaxEngine>(FaxEngine::Role::Receive, false, faxOptions);
    connect(m_fax.get(), &FaxEngine::finished, this, [this](const FaxResult &result) {
        m_faxFinished = true;
        m_faxResult = result;
        sendBye();
    });
    QString startError;
    if (!m_fax->start(m_options.rxTiffPath, &startError)) {
        error(startError);
        return;
    }

    const auto answer = sdp::makeAnswer(*offer, QHostAddress::LocalHost, m_rtp->localPort(), 42, 1);
    m_contactUri = QStringLiteral("sip:mock@127.0.0.1:%1").arg(port());
    SipMessage ok = makeResponse(m_invite, 200, m_toTag);
    ok.addHeader(QStringLiteral("Record-Route"), QStringLiteral("<sip:127.0.0.1:%1;lr>").arg(port()));
    ok.addHeader(QStringLiteral("Contact"), QStringLiteral("<%1>").arg(m_contactUri));
    ok.setBody(answer->toByteArray(), QStringLiteral("application/sdp"));
    const QByteArray wire = ok.serialize();
    send(wire);
    if (m_options.retransmitOk)
        QTimer::singleShot(30, this, [this, wire] { send(wire); });

    m_rtp->setAudioPort(m_fax.get());
    m_rtp->start();
}

void MockFritzBox::handleAck(const SipMessage &ack)
{
    if (!m_challengedInvite.callId().isEmpty() && ack.topViaBranch() == m_challengedInvite.topViaBranch()) {
        // ACK for the 407: same branch and Request-URI as the challenged INVITE.
        if (ack.requestUri() != m_challengedInvite.requestUri())
            error(QStringLiteral("ACK for 407 with Request-URI %1").arg(ack.requestUri()));
        if (ack.cseqNumber() != m_challengedInvite.cseqNumber() || ack.cseqMethod() != QLatin1String("ACK"))
            error(QStringLiteral("ACK for 407 with CSeq %1").arg(ack.header(QStringLiteral("CSeq"))));
        if (ack.toTag() != QLatin1String("challenge-tag"))
            error(QStringLiteral("ACK for 407 without the response's To tag"));
        m_ackFor407 = true;
        return;
    }
    if (m_invite.callId().isEmpty() || ack.callId() != m_invite.callId()) {
        error(QStringLiteral("ACK for an unknown transaction"));
        return;
    }
    if (ack.topViaBranch() == m_invite.topViaBranch()) {
        if (m_options.rejectInviteWith == 0)
            error(QStringLiteral("ACK for 2xx reused the INVITE branch"));
        return; // ACK for the rejection
    }

    // ACK for 2xx: new transaction to the Contact, routed via the Record-Route.
    if (ack.cseqNumber() != m_invite.cseqNumber())
        error(QStringLiteral("ACK for 2xx with CSeq %1").arg(ack.cseqNumber()));
    if (ack.requestUri() != m_contactUri)
        error(QStringLiteral("ACK for 2xx with Request-URI %1, expected %2").arg(ack.requestUri(), m_contactUri));
    const QString expectedRoute = QStringLiteral("<sip:127.0.0.1:%1;lr>").arg(port());
    if (ack.headerValues(QStringLiteral("Route")) != QStringList{expectedRoute})
        error(QStringLiteral("ACK for 2xx without route set"));
    if (ack.toTag() != m_toTag)
        error(QStringLiteral("ACK for 2xx with wrong To tag"));
    if (ack.header(QStringLiteral("Proxy-Authorization")).isEmpty())
        error(QStringLiteral("ACK for 2xx without the INVITE's credentials"));
    ++m_acksFor200;
    if (m_acksFor200 == 1 && m_options.sendT38ReInvite)
        QTimer::singleShot(300, this, &MockFritzBox::sendT38ReInvite);
}

void MockFritzBox::sendT38ReInvite()
{
    m_reInviteBranch = sip::generateBranch();
    SipMessage reinvite = SipMessage::request(QStringLiteral("INVITE"), m_peerContact);
    reinvite.addHeader(QStringLiteral("Via"), QStringLiteral("SIP/2.0/UDP 127.0.0.1:%1;branch=%2").arg(port()).arg(m_reInviteBranch));
    reinvite.addHeader(QStringLiteral("Max-Forwards"), QStringLiteral("70"));
    SipNameAddr from = SipNameAddr::parse(m_invite.header(QStringLiteral("To")));
    from.setParam(QStringLiteral("tag"), m_toTag);
    reinvite.addHeader(QStringLiteral("From"), from.toString());
    reinvite.addHeader(QStringLiteral("To"), m_invite.header(QStringLiteral("From")));
    reinvite.addHeader(QStringLiteral("Call-ID"), m_invite.callId());
    reinvite.addHeader(QStringLiteral("CSeq"), QStringLiteral("%1 INVITE").arg(++m_localCseq));
    reinvite.addHeader(QStringLiteral("Contact"), QStringLiteral("<%1>").arg(m_contactUri));
    reinvite.setBody("v=0\r\n"
                     "o=- 42 2 IN IP4 127.0.0.1\r\n"
                     "s=-\r\n"
                     "c=IN IP4 127.0.0.1\r\n"
                     "t=0 0\r\n"
                     "m=image 40100 udptl t38\r\n"
                     "a=T38FaxVersion:0\r\n"
                     "a=T38MaxBitRate:14400\r\n"
                     "a=T38FaxRateManagement:transferredTCF\r\n"
                     "a=T38FaxUdpEC:t38UDPRedundancy\r\n",
                     QStringLiteral("application/sdp"));
    m_reInvite = reinvite;
    send(reinvite.serialize());
}

void MockFritzBox::sendBye()
{
    if (m_rtp)
        m_rtp->stop();
    m_byeBranch = sip::generateBranch();
    SipMessage bye = SipMessage::request(QStringLiteral("BYE"), m_peerContact);
    bye.addHeader(QStringLiteral("Via"), QStringLiteral("SIP/2.0/UDP 127.0.0.1:%1;branch=%2").arg(port()).arg(m_byeBranch));
    bye.addHeader(QStringLiteral("Max-Forwards"), QStringLiteral("70"));
    SipNameAddr from = SipNameAddr::parse(m_invite.header(QStringLiteral("To")));
    from.setParam(QStringLiteral("tag"), m_toTag);
    bye.addHeader(QStringLiteral("From"), from.toString());
    bye.addHeader(QStringLiteral("To"), m_invite.header(QStringLiteral("From")));
    bye.addHeader(QStringLiteral("Call-ID"), m_invite.callId());
    bye.addHeader(QStringLiteral("CSeq"), QStringLiteral("%1 BYE").arg(++m_localCseq));
    send(bye.serialize());
}

void MockFritzBox::handleResponse(const SipMessage &response)
{
    const QString branch = response.topViaBranch();
    if (branch == m_reInviteBranch && response.cseqMethod() == QLatin1String("INVITE")) {
        if (response.statusCode() == 488) {
            m_t38Rejected = true;
            // ACK for a non-2xx: same branch and Request-URI.
            SipMessage ack = SipMessage::request(QStringLiteral("ACK"), m_reInvite.requestUri());
            ack.addHeader(QStringLiteral("Via"), m_reInvite.header(QStringLiteral("Via")));
            ack.addHeader(QStringLiteral("Max-Forwards"), QStringLiteral("70"));
            ack.addHeader(QStringLiteral("From"), m_reInvite.header(QStringLiteral("From")));
            ack.addHeader(QStringLiteral("To"), response.header(QStringLiteral("To")));
            ack.addHeader(QStringLiteral("Call-ID"), m_reInvite.callId());
            ack.addHeader(QStringLiteral("CSeq"), QStringLiteral("%1 ACK").arg(m_reInvite.cseqNumber()));
            send(ack.serialize());
        } else if (response.statusCode() >= 200) {
            error(QStringLiteral("T.38 re-INVITE answered with %1").arg(response.statusCode()));
        }
        return;
    }
    if (branch == m_byeBranch && response.cseqMethod() == QLatin1String("BYE")) {
        if (response.statusCode() != 200)
            error(QStringLiteral("BYE answered with %1").arg(response.statusCode()));
        if (!m_byeAnswered) {
            m_byeAnswered = true;
            emit callFinished();
        }
        return;
    }
    error(QStringLiteral("unexpected response %1 %2").arg(response.statusCode()).arg(response.cseqMethod()));
}

} // namespace faxwerk::test
