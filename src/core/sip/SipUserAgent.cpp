#include "SipUserAgent.h"

#include <QDateTime>
#include <QNetworkDatagram>
#include <QRandomGenerator>

#include <algorithm>

namespace faxwerk {

namespace {

const QString kAllowedMethods = QStringLiteral("INVITE, ACK, BYE, CANCEL, OPTIONS, NOTIFY, UPDATE");
const QString kSdpType = QStringLiteral("application/sdp");

QString transactionKey(const QString &branch, const QString &method)
{
    return branch + QLatin1Char('|') + method;
}

bool isIpLiteral(const QString &host)
{
    return !QHostAddress(host).isNull();
}

} // namespace

struct SipUserAgent::ClientTransaction
{
    SipMessage request;
    QByteArray wire;
    QHostAddress address;
    quint16 port = 0;
    bool invite = false;
    bool proceeding = false;
    int interval = 500;
    QTimer retransmitTimer;
    QTimer timeoutTimer;
    std::function<void(const SipMessage &)> onResponse;
    std::function<void()> onTimeout;
};

// A logical request that may be re-sent with credentials after 401/407.
struct SipUserAgent::RequestContext
{
    std::function<Outgoing()> build; // a fresh request (new CSeq), without Via and credentials
    std::function<void(const SipMessage &)> onSent;
    std::function<void(const SipMessage &)> onProvisional;
    std::function<void(const SipMessage &, bool authFailed)> onFinal;
    std::function<void()> onTimeout;
    std::optional<DigestChallenge> challenge;
    bool proxyChallenge = false;
    QSet<QString> triedNonces;
    int authAttempts = 0;
};

// INVITE server transaction: final responses are retransmitted until ACK.
struct SipUserAgent::ServerInvite
{
    QString branch;
    QByteArray response;
    QHostAddress address;
    quint16 port = 0;
    int interval = 500;
    QTimer retransmitTimer;
    QTimer giveUpTimer;
};

SipUserAgent::SipUserAgent(const SipAccount &account, QObject *parent)
    : QObject(parent)
    , m_account(account)
    , m_digest(account.effectiveAuthUser(), account.password)
{
    qRegisterMetaType<faxwerk::AudioEndpointInfo>();
    qRegisterMetaType<faxwerk::SipUserAgent::Failure>();
    m_uptime.start();
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        if (m_regState == RegistrationState::Registered)
            sendRegister(m_account.registerExpires);
    });
    connect(&m_socket, &QUdpSocket::readyRead, this, &SipUserAgent::onReadyRead);
    m_sdpSessionId = QRandomGenerator::global()->bounded(1u << 30);
}

SipUserAgent::~SipUserAgent()
{
    if (m_lookupId >= 0)
        QHostInfo::abortHostLookup(m_lookupId);
}

// ---------------------------------------------------------------------------
// Transport

void SipUserAgent::start()
{
    if (m_regState != RegistrationState::Idle && m_regState != RegistrationState::Failed)
        return;
    m_regState = RegistrationState::Resolving;
    emit logMessage(QStringLiteral("Resolving %1").arg(m_account.server));
    m_lookupId = QHostInfo::lookupHost(m_account.server, this, &SipUserAgent::onHostResolved);
}

void SipUserAgent::onHostResolved(const QHostInfo &info)
{
    m_lookupId = -1;
    QHostAddress server;
    for (const QHostAddress &addr : info.addresses()) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol) {
            server = addr;
            break;
        }
    }
    if (server.isNull() && !info.addresses().isEmpty())
        server = info.addresses().first();
    if (info.error() != QHostInfo::NoError || server.isNull()) {
        m_regState = RegistrationState::Failed;
        emit registrationFailed(Failure::ResolveFailed, 0,
                                QStringLiteral("cannot resolve %1: %2").arg(m_account.server, info.errorString()));
        return;
    }
    m_serverAddress = server;

    // Find the local address used to reach the server with a connected probe
    // socket; it goes into Via, Contact and SDP.
    QUdpSocket probe;
    probe.connectToHost(server, m_account.serverPort);
    if (probe.state() != QAbstractSocket::ConnectedState)
        probe.waitForConnected(1000);
    m_localAddress = probe.localAddress();
    probe.close();
    if (m_localAddress.isNull()) {
        m_regState = RegistrationState::Failed;
        emit registrationFailed(Failure::NetworkError, 0,
                                QStringLiteral("no route to %1").arg(server.toString()));
        return;
    }

    const QHostAddress bindAddress = server.protocol() == QAbstractSocket::IPv6Protocol
                                         ? QHostAddress(QHostAddress::AnyIPv6)
                                         : QHostAddress(QHostAddress::AnyIPv4);
    if (m_socket.state() != QAbstractSocket::BoundState
        && !m_socket.bind(bindAddress, m_account.localPort)) {
        m_regState = RegistrationState::Failed;
        emit registrationFailed(Failure::NetworkError, 0,
                                QStringLiteral("cannot bind UDP port %1: %2")
                                    .arg(m_account.localPort)
                                    .arg(m_socket.errorString()));
        return;
    }
    m_resolved = true;
    emit resolved(m_serverAddress, m_localAddress);
    emit logMessage(QStringLiteral("Server %1:%2, local %3:%4")
                        .arg(m_serverAddress.toString())
                        .arg(m_account.serverPort)
                        .arg(m_localAddress.toString())
                        .arg(m_socket.localPort()));

    m_regCallId = sip::generateCallId(m_localAddress.toString());
    m_regTag = sip::generateTag();
    sendRegister(m_account.registerExpires);
}

void SipUserAgent::onReadyRead()
{
    while (m_socket.hasPendingDatagrams()) {
        const QNetworkDatagram datagram = m_socket.receiveDatagram();
        if (!datagram.isValid())
            continue;
        const QByteArray data = datagram.data();
        // Ignore keep-alives (CRLF) silently.
        if (data.trimmed().isEmpty())
            continue;
        QHostAddress sender = datagram.senderAddress();
        bool isV4 = false;
        const quint32 v4 = sender.toIPv4Address(&isV4);
        if (isV4)
            sender = QHostAddress(v4); // strip IPv4-mapped IPv6 form
        const quint16 senderPort = quint16(datagram.senderPort());
        emit trace(false, sender, senderPort, data);

        QString error;
        const auto msg = SipMessage::parse(data, &error);
        if (!msg) {
            emit logMessage(QStringLiteral("Ignoring malformed SIP message from %1: %2")
                                .arg(sender.toString(), error));
            continue;
        }
        if (msg->isResponse())
            handleResponse(*msg, sender, senderPort);
        else
            handleRequest(*msg, sender, senderPort);
    }
}

void SipUserAgent::sendRaw(const QByteArray &data, const QHostAddress &address, quint16 port)
{
    emit trace(true, address, port, data);
    if (m_socket.writeDatagram(data, address, port) < 0)
        emit logMessage(QStringLiteral("Sending to %1:%2 failed: %3")
                            .arg(address.toString())
                            .arg(port)
                            .arg(m_socket.errorString()));
}

QString SipUserAgent::viaValue(const QString &branch) const
{
    return QStringLiteral("SIP/2.0/UDP %1:%2;rport;branch=%3")
        .arg(m_localAddress.toString())
        .arg(m_socket.localPort())
        .arg(branch);
}

QString SipUserAgent::contactUri() const
{
    SipUri uri;
    uri.user = m_account.user;
    uri.host = m_localAddress.toString();
    uri.port = m_socket.localPort();
    return uri.toString();
}

QString SipUserAgent::localUri() const
{
    return QStringLiteral("sip:%1@%2").arg(m_account.user, m_account.effectiveDomain());
}

QString SipUserAgent::allowValue() const
{
    return kAllowedMethods;
}

// ---------------------------------------------------------------------------
// Client transactions

void SipUserAgent::startClientTransaction(const SipMessage &request, const QHostAddress &address,
                                          quint16 port,
                                          std::function<void(const SipMessage &)> onResponse,
                                          std::function<void()> onTimeout)
{
    const QString key = transactionKey(request.topViaBranch(), request.method());
    auto tx = std::make_unique<ClientTransaction>();
    tx->request = request;
    tx->wire = request.serialize();
    tx->address = address;
    tx->port = port;
    tx->invite = request.method() == QLatin1String("INVITE");
    tx->interval = m_timers.t1;
    tx->onResponse = std::move(onResponse);
    tx->onTimeout = std::move(onTimeout);

    // Timer A (INVITE) / E (non-INVITE): retransmissions.
    tx->retransmitTimer.setSingleShot(true);
    connect(&tx->retransmitTimer, &QTimer::timeout, this, [this, key] { retransmitClientTransaction(key); });
    tx->retransmitTimer.start(tx->interval);
    // Timer B / F: transaction timeout.
    tx->timeoutTimer.setSingleShot(true);
    connect(&tx->timeoutTimer, &QTimer::timeout, this, [this, key] { timeoutClientTransaction(key); });
    tx->timeoutTimer.start(m_timers.t1 * m_timers.timeoutFactor);

    const QByteArray wire = tx->wire;
    m_transactions[key] = std::move(tx);
    sendRaw(wire, address, port);
}

void SipUserAgent::retransmitClientTransaction(const QString &key)
{
    const auto it = m_transactions.find(key);
    if (it == m_transactions.end())
        return;
    ClientTransaction &tx = *it->second;
    sendRaw(tx.wire, tx.address, tx.port);
    if (tx.invite)
        tx.interval *= 2;                                  // Timer A doubles
    else if (tx.proceeding)
        tx.interval = m_timers.t2;                         // Timer E in Proceeding
    else
        tx.interval = std::min(tx.interval * 2, m_timers.t2);
    tx.retransmitTimer.start(tx.interval);
}

void SipUserAgent::timeoutClientTransaction(const QString &key)
{
    const auto it = m_transactions.find(key);
    if (it == m_transactions.end())
        return;
    const auto onTimeout = it->second->onTimeout;
    emit logMessage(QStringLiteral("Transaction timeout: %1").arg(it->second->request.method()));
    m_transactions.erase(it);
    if (onTimeout)
        onTimeout();
}

SipMessage SipUserAgent::buildNon2xxAck(const SipMessage &invite, const SipMessage &response) const
{
    // RFC 3261 17.1.1.3: same branch and Request-URI as the INVITE, To from the response.
    SipMessage ack = SipMessage::request(QStringLiteral("ACK"), invite.requestUri());
    ack.addHeader(QStringLiteral("Via"), invite.headerValues(QStringLiteral("Via")).value(0));
    ack.addHeader(QStringLiteral("Max-Forwards"), QStringLiteral("70"));
    ack.addHeader(QStringLiteral("From"), invite.header(QStringLiteral("From")));
    ack.addHeader(QStringLiteral("To"), response.header(QStringLiteral("To")));
    ack.addHeader(QStringLiteral("Call-ID"), invite.callId());
    ack.addHeader(QStringLiteral("CSeq"), QStringLiteral("%1 ACK").arg(invite.cseqNumber()));
    for (const QString &route : invite.headerValues(QStringLiteral("Route")))
        ack.addHeader(QStringLiteral("Route"), route);
    if (!m_account.userAgent.isEmpty())
        ack.addHeader(QStringLiteral("User-Agent"), m_account.userAgent);
    return ack;
}

void SipUserAgent::handleResponse(const SipMessage &response, const QHostAddress &, quint16)
{
    const QString branch = response.topViaBranch();
    const QString method = response.cseqMethod();
    const int code = response.statusCode();
    const auto it = m_transactions.find(transactionKey(branch, method));

    if (it == m_transactions.end()) {
        // Retransmitted final responses after the transaction is gone.
        if (method == QLatin1String("INVITE")) {
            if (code >= 200 && code < 300) {
                if (m_dialogEstablished && response.callId() == m_callId
                    && response.cseqNumber() == m_inviteCseq) {
                    emit logMessage(QStringLiteral("Retransmitted 200 OK, repeating ACK"));
                    sendRaw(m_ack.message.serialize(), m_ack.address, m_ack.port);
                }
            } else if (code >= 300) {
                const auto ack = m_non2xxAcks.constFind(branch);
                if (ack != m_non2xxAcks.constEnd())
                    sendRaw(ack->first, ack->second.first, ack->second.second);
            }
        }
        return;
    }

    ClientTransaction &tx = *it->second;
    if (code < 200) {
        tx.proceeding = true;
        if (tx.invite) {
            // Proceeding: no more retransmissions, Timer B no longer applies.
            tx.retransmitTimer.stop();
            tx.timeoutTimer.stop();
        }
        const auto onResponse = tx.onResponse;
        if (onResponse)
            onResponse(response);
        return;
    }

    // Final response: the transaction completes.
    const auto onResponse = tx.onResponse;
    if (tx.invite && code >= 300) {
        const SipMessage ack = buildNon2xxAck(tx.request, response);
        const QByteArray wire = ack.serialize();
        sendRaw(wire, tx.address, tx.port);
        // Keep the ACK for retransmitted responses (Timer D, 32 s).
        m_non2xxAcks.insert(branch, {wire, {tx.address, tx.port}});
        QTimer::singleShot(m_timers.t1 * m_timers.timeoutFactor, this,
                           [this, branch] { m_non2xxAcks.remove(branch); });
    }
    m_transactions.erase(it);
    if (onResponse)
        onResponse(response);
}

// ---------------------------------------------------------------------------
// Authentication

std::optional<DigestChallenge> SipUserAgent::findChallenge(const SipMessage &response, bool proxy) const
{
    const QString header = proxy ? QStringLiteral("Proxy-Authenticate") : QStringLiteral("WWW-Authenticate");
    for (const QString &value : response.headerValues(header)) {
        auto challenge = DigestChallenge::parse(value);
        if (challenge && DigestAuth::algorithmFromString(challenge->algorithm))
            return challenge;
    }
    return std::nullopt;
}

void SipUserAgent::dispatch(const std::shared_ptr<RequestContext> &ctx)
{
    Outgoing out = ctx->build();
    SipMessage &msg = out.message;
    msg.prependHeader(QStringLiteral("Via"), viaValue(sip::generateBranch()));
    if (ctx->challenge) {
        const QString credentials = m_digest.authorize(*ctx->challenge, msg.method(), msg.requestUri(), msg.body());
        if (!credentials.isEmpty()) {
            msg.setHeader(ctx->proxyChallenge ? QStringLiteral("Proxy-Authorization")
                                              : QStringLiteral("Authorization"),
                          credentials);
        }
    }
    if (ctx->onSent)
        ctx->onSent(msg);

    startClientTransaction(
        msg, out.address, out.port,
        [this, ctx](const SipMessage &response) {
            const int code = response.statusCode();
            if (code < 200) {
                if (ctx->onProvisional)
                    ctx->onProvisional(response);
                return;
            }
            if (code == 401 || code == 407) {
                const bool proxy = code == 407;
                const auto challenge = findChallenge(response, proxy);
                // Only one retry per nonce: the same nonce again means the
                // credentials were rejected.
                if (challenge && !ctx->triedNonces.contains(challenge->nonce) && ctx->authAttempts < 3) {
                    ctx->triedNonces.insert(challenge->nonce);
                    ctx->challenge = challenge;
                    ctx->proxyChallenge = proxy;
                    ++ctx->authAttempts;
                    emit logMessage(QStringLiteral("%1 challenged (%2), retrying with credentials")
                                        .arg(response.cseqMethod())
                                        .arg(code));
                    dispatch(ctx);
                    return;
                }
                if (ctx->onFinal)
                    ctx->onFinal(response, true);
                return;
            }
            if (ctx->onFinal)
                ctx->onFinal(response, false);
        },
        [ctx] {
            if (ctx->onTimeout)
                ctx->onTimeout();
        });
}

// ---------------------------------------------------------------------------
// Registration

void SipUserAgent::sendRegister(int expires)
{
    if (!m_resolved)
        return;
    m_regState = expires == 0 ? RegistrationState::Unregistering : RegistrationState::Registering;

    auto ctx = std::make_shared<RequestContext>();
    ctx->build = [this, expires] {
        const QString domain = m_account.effectiveDomain();
        SipMessage msg = SipMessage::request(QStringLiteral("REGISTER"), QStringLiteral("sip:%1").arg(domain));
        msg.addHeader(QStringLiteral("Max-Forwards"), QStringLiteral("70"));
        SipNameAddr from;
        from.displayName = m_account.displayName;
        from.uri = localUri();
        from.setParam(QStringLiteral("tag"), m_regTag);
        msg.addHeader(QStringLiteral("From"), from.toString());
        msg.addHeader(QStringLiteral("To"), QStringLiteral("<%1>").arg(localUri()));
        msg.addHeader(QStringLiteral("Call-ID"), m_regCallId);
        msg.addHeader(QStringLiteral("CSeq"), QStringLiteral("%1 REGISTER").arg(++m_regCseq));
        msg.addHeader(QStringLiteral("Contact"), QStringLiteral("<%1>").arg(contactUri()));
        msg.addHeader(QStringLiteral("Expires"), QString::number(expires));
        msg.addHeader(QStringLiteral("Allow"), allowValue());
        if (!m_account.userAgent.isEmpty())
            msg.addHeader(QStringLiteral("User-Agent"), m_account.userAgent);
        return Outgoing{msg, m_serverAddress, m_account.serverPort};
    };
    // Re-use the last challenge (with an incremented nonce count) for refreshes.
    if (m_regChallenge) {
        ctx->challenge = m_regChallenge;
        ctx->proxyChallenge = m_regChallengeProxy;
        ctx->triedNonces.insert(m_regChallenge->nonce);
    }
    ctx->onFinal = [this, ctx, expires](const SipMessage &response, bool authFailed) {
        if (ctx->challenge) {
            m_regChallenge = ctx->challenge;
            m_regChallengeProxy = ctx->proxyChallenge;
        }
        handleRegisterResponse(response, authFailed, expires);
    };
    ctx->onTimeout = [this, expires] {
        if (expires == 0) {
            m_regState = RegistrationState::Idle;
            emit unregistered();
            return;
        }
        m_regState = RegistrationState::Failed;
        emit registrationFailed(Failure::Timeout, 408,
                                QStringLiteral("no response from %1").arg(m_serverAddress.toString()));
    };
    dispatch(ctx);
}

void SipUserAgent::handleRegisterResponse(const SipMessage &response, bool authFailed, int requestedExpires)
{
    const int code = response.statusCode();
    if (requestedExpires == 0) {
        m_refreshTimer.stop();
        m_regState = RegistrationState::Idle;
        emit logMessage(QStringLiteral("Unregister: %1 %2").arg(code).arg(response.reasonPhrase()));
        emit unregistered();
        return;
    }
    if (code >= 200 && code < 300) {
        // Granted expiry: our Contact's expires parameter, else Expires, else requested.
        int expires = -1;
        for (const QString &contact : response.headerValues(QStringLiteral("Contact"))) {
            const SipNameAddr addr = SipNameAddr::parse(contact);
            const auto uri = SipUri::parse(addr.uri);
            if (uri && uri->user == m_account.user && uri->host == m_localAddress.toString()
                && (uri->port == 0 ? 5060 : uri->port) == m_socket.localPort()) {
                bool ok = false;
                const int value = addr.param(QStringLiteral("expires")).toInt(&ok);
                if (ok)
                    expires = value;
            }
        }
        if (expires < 0)
            expires = response.expires();
        if (expires <= 0)
            expires = requestedExpires;
        m_regState = RegistrationState::Registered;
        m_refreshTimer.start(std::max(1, expires / 2) * 1000);
        emit registered(expires);
        return;
    }

    m_regState = RegistrationState::Failed;
    m_refreshTimer.stop();
    const Failure failure = (authFailed || code == 403) ? Failure::AuthenticationFailed : Failure::Rejected;
    const QString reason = failure == Failure::AuthenticationFailed
                               ? QStringLiteral("wrong user name or password (%1 %2)").arg(code).arg(response.reasonPhrase())
                               : QStringLiteral("%1 %2").arg(code).arg(response.reasonPhrase());
    emit registrationFailed(failure, code, reason);
}

void SipUserAgent::unregister()
{
    m_refreshTimer.stop();
    if (!m_resolved || m_regState == RegistrationState::Idle || m_regState == RegistrationState::Failed
        || m_regState == RegistrationState::Resolving) {
        m_regState = RegistrationState::Idle;
        QMetaObject::invokeMethod(this, &SipUserAgent::unregistered, Qt::QueuedConnection);
        return;
    }
    if (m_regState == RegistrationState::Unregistering)
        return;
    sendRegister(0);
}

// ---------------------------------------------------------------------------
// Outgoing call

bool SipUserAgent::call(const QString &number, quint16 localRtpPort)
{
    if (!m_resolved || (m_callState != CallState::Idle && m_callState != CallState::Terminated))
        return false;

    resetCall();
    m_callState = CallState::Calling;
    m_callId = sip::generateCallId(m_localAddress.toString());
    m_localTag = sip::generateTag();
    m_remoteUri = QStringLiteral("sip:%1@%2").arg(number, m_account.effectiveDomain());
    m_rtpPort = localRtpPort;

    int payloadType = kPayloadPcma;
    const QByteArray offer = localSdp(std::nullopt, &payloadType);

    auto ctx = std::make_shared<RequestContext>();
    ctx->build = [this, offer] {
        SipMessage msg = SipMessage::request(QStringLiteral("INVITE"), m_remoteUri);
        msg.addHeader(QStringLiteral("Max-Forwards"), QStringLiteral("70"));
        SipNameAddr from;
        from.displayName = m_account.displayName;
        from.uri = localUri();
        from.setParam(QStringLiteral("tag"), m_localTag);
        msg.addHeader(QStringLiteral("From"), from.toString());
        msg.addHeader(QStringLiteral("To"), QStringLiteral("<%1>").arg(m_remoteUri));
        msg.addHeader(QStringLiteral("Call-ID"), m_callId);
        m_inviteCseq = ++m_localCseq;
        msg.addHeader(QStringLiteral("CSeq"), QStringLiteral("%1 INVITE").arg(m_inviteCseq));
        msg.addHeader(QStringLiteral("Contact"), QStringLiteral("<%1>").arg(contactUri()));
        msg.addHeader(QStringLiteral("Allow"), allowValue());
        if (!m_account.userAgent.isEmpty())
            msg.addHeader(QStringLiteral("User-Agent"), m_account.userAgent);
        msg.setBody(offer, kSdpType);
        return Outgoing{msg, m_serverAddress, m_account.serverPort};
    };
    ctx->onSent = [this](const SipMessage &msg) {
        m_lastInvite = msg;
        m_inviteProvisional = false;
    };
    ctx->onProvisional = [this](const SipMessage &response) {
        if (response.callId() != m_callId)
            return;
        m_inviteProvisional = true;
        if (m_callState == CallState::Calling)
            m_callState = CallState::Proceeding;
        emit callProgress(response.statusCode(), response.reasonPhrase());
        if (m_cancelPending && !m_cancelSent)
            sendCancel();
    };
    ctx->onFinal = [this](const SipMessage &response, bool authFailed) {
        if (response.callId() != m_callId)
            return;
        const int code = response.statusCode();
        if (code >= 200 && code < 300) {
            handleInviteSuccess(response);
            return;
        }
        m_callState = CallState::Terminated;
        if (authFailed)
            failCall(Failure::AuthenticationFailed, code,
                     QStringLiteral("wrong user name or password (%1 %2)").arg(code).arg(response.reasonPhrase()));
        else if (m_cancelPending)
            finishCall(false, QStringLiteral("call cancelled"));
        else
            failCall(Failure::Rejected, code, QStringLiteral("%1 %2").arg(code).arg(response.reasonPhrase()));
    };
    ctx->onTimeout = [this] {
        m_callState = CallState::Terminated;
        if (m_cancelPending)
            finishCall(false, QStringLiteral("call cancelled"));
        else
            failCall(Failure::Timeout, 408, QStringLiteral("no response to INVITE"));
    };
    dispatch(ctx);
    return true;
}

void SipUserAgent::handleInviteSuccess(const SipMessage &response)
{
    if (m_dialogEstablished) {
        // 200 OK from a second fork or a retransmission: acknowledge again.
        sendRaw(m_ack.message.serialize(), m_ack.address, m_ack.port);
        return;
    }
    m_dialogEstablished = true;
    m_remoteTag = response.toTag();
    const QString contact = SipNameAddr::parse(response.header(QStringLiteral("Contact"))).uri;
    m_remoteTarget = contact.isEmpty() ? m_remoteUri : contact;
    // The route set is the Record-Route list in reverse order (RFC 3261 12.1.2).
    m_routeSet = response.headerValues(QStringLiteral("Record-Route"));
    std::reverse(m_routeSet.begin(), m_routeSet.end());

    sendAck2xx();

    if (m_cancelPending) {
        // CANCEL lost the race against 200 OK: end the call properly.
        m_callState = CallState::Connected;
        sendBye();
        return;
    }

    const auto sdp = SdpSession::parse(response.body());
    const auto remote = sdp ? sdp::remoteAudio(*sdp) : std::nullopt;
    if (!remote) {
        m_callState = CallState::Connected;
        failCall(Failure::Rejected, 488, QStringLiteral("answer contains no usable G.711 audio"));
        sendBye();
        return;
    }
    m_payloadType = remote->payloadType;
    m_callState = CallState::Connected;
    emit callEstablished(*remote);
}

std::pair<QHostAddress, quint16> SipUserAgent::nextHopFor(const QString &uriText) const
{
    const auto uri = SipUri::parse(SipNameAddr::parse(uriText).uri);
    if (uri && isIpLiteral(uri->host))
        return {QHostAddress(uri->host), uri->port ? uri->port : quint16(5060)};
    // Host names: we only know how to reach the configured server.
    return {m_serverAddress, m_account.serverPort};
}

SipUserAgent::Outgoing SipUserAgent::buildInDialog(const QString &method, int cseq) const
{
    QString requestUri = m_remoteTarget;
    QStringList routes = m_routeSet;
    QString nextHop = requestUri;
    if (!routes.isEmpty()) {
        const auto first = SipUri::parse(SipNameAddr::parse(routes.first()).uri);
        if (first && !first->hasParam(QStringLiteral("lr"))) {
            // Strict routing (RFC 2543): the first route becomes the Request-URI.
            requestUri = SipNameAddr::parse(routes.takeFirst()).uri;
            routes.append(QStringLiteral("<%1>").arg(m_remoteTarget));
            nextHop = requestUri;
        } else {
            nextHop = routes.first();
        }
    }

    SipMessage msg = SipMessage::request(method, requestUri);
    msg.addHeader(QStringLiteral("Max-Forwards"), QStringLiteral("70"));
    for (const QString &route : routes)
        msg.addHeader(QStringLiteral("Route"), route);
    msg.addHeader(QStringLiteral("From"), m_lastInvite.header(QStringLiteral("From")));
    SipNameAddr to = SipNameAddr::parse(m_lastInvite.header(QStringLiteral("To")));
    if (!m_remoteTag.isEmpty())
        to.setParam(QStringLiteral("tag"), m_remoteTag);
    msg.addHeader(QStringLiteral("To"), to.toString());
    msg.addHeader(QStringLiteral("Call-ID"), m_callId);
    msg.addHeader(QStringLiteral("CSeq"), QStringLiteral("%1 %2").arg(cseq).arg(method));
    if (!m_account.userAgent.isEmpty())
        msg.addHeader(QStringLiteral("User-Agent"), m_account.userAgent);

    const auto hop = nextHopFor(nextHop);
    return Outgoing{msg, hop.first, hop.second};
}

void SipUserAgent::sendAck2xx()
{
    // ACK for 2xx is a new transaction to the remote target via the route set,
    // with the CSeq number of the INVITE and its credentials.
    m_ack = buildInDialog(QStringLiteral("ACK"), m_inviteCseq);
    m_ack.message.prependHeader(QStringLiteral("Via"), viaValue(sip::generateBranch()));
    for (const char *auth : {"Authorization", "Proxy-Authorization"}) {
        const QString value = m_lastInvite.header(QLatin1String(auth));
        if (!value.isEmpty())
            m_ack.message.addHeader(QLatin1String(auth), value);
    }
    sendRaw(m_ack.message.serialize(), m_ack.address, m_ack.port);
}

void SipUserAgent::sendCancel()
{
    m_cancelSent = true;
    // CANCEL mirrors the INVITE: same Request-URI, Via branch, From, To, Call-ID, Route.
    SipMessage cancel = SipMessage::request(QStringLiteral("CANCEL"), m_lastInvite.requestUri());
    cancel.addHeader(QStringLiteral("Via"), m_lastInvite.headerValues(QStringLiteral("Via")).value(0));
    cancel.addHeader(QStringLiteral("Max-Forwards"), QStringLiteral("70"));
    for (const QString &route : m_lastInvite.headerValues(QStringLiteral("Route")))
        cancel.addHeader(QStringLiteral("Route"), route);
    cancel.addHeader(QStringLiteral("From"), m_lastInvite.header(QStringLiteral("From")));
    cancel.addHeader(QStringLiteral("To"), m_lastInvite.header(QStringLiteral("To")));
    cancel.addHeader(QStringLiteral("Call-ID"), m_lastInvite.callId());
    cancel.addHeader(QStringLiteral("CSeq"), QStringLiteral("%1 CANCEL").arg(m_lastInvite.cseqNumber()));
    if (!m_account.userAgent.isEmpty())
        cancel.addHeader(QStringLiteral("User-Agent"), m_account.userAgent);
    // The 487 for the INVITE ends the call; the CANCEL's own response is irrelevant.
    startClientTransaction(cancel, m_serverAddress, m_account.serverPort, nullptr, nullptr);
}

void SipUserAgent::sendBye()
{
    m_callState = CallState::Terminating;
    auto ctx = std::make_shared<RequestContext>();
    ctx->build = [this] { return buildInDialog(QStringLiteral("BYE"), ++m_localCseq); };
    ctx->onFinal = [this](const SipMessage &response, bool) {
        finishCall(false, QStringLiteral("hung up (%1 %2)").arg(response.statusCode()).arg(response.reasonPhrase()));
    };
    ctx->onTimeout = [this] { finishCall(false, QStringLiteral("hung up (no response to BYE)")); };
    dispatch(ctx);
}

void SipUserAgent::hangup()
{
    switch (m_callState) {
    case CallState::Calling:
    case CallState::Proceeding:
        m_cancelPending = true;
        // A CANCEL may only be sent after a provisional response (RFC 3261 9.1).
        if (m_inviteProvisional && !m_cancelSent)
            sendCancel();
        break;
    case CallState::Connected:
        sendBye();
        break;
    default:
        break;
    }
}

void SipUserAgent::finishCall(bool byRemote, const QString &reason)
{
    m_callState = CallState::Terminated;
    if (m_callOutcomeReported)
        return;
    m_callOutcomeReported = true;
    emit callEnded(byRemote, reason);
}

void SipUserAgent::failCall(Failure failure, int code, const QString &reason)
{
    if (m_callOutcomeReported)
        return;
    m_callOutcomeReported = true;
    emit callFailed(failure, code, reason);
}

void SipUserAgent::resetCall()
{
    m_callId.clear();
    m_localTag.clear();
    m_remoteTag.clear();
    m_remoteTarget.clear();
    m_routeSet.clear();
    m_inviteProvisional = false;
    m_cancelPending = false;
    m_cancelSent = false;
    m_dialogEstablished = false;
    m_callOutcomeReported = false;
    m_lastSdpBody.clear();
}

// ---------------------------------------------------------------------------
// Incoming requests

SipMessage SipUserAgent::makeResponse(const SipMessage &request, int code, const QString &reason) const
{
    SipMessage response = SipMessage::response(code, reason);
    for (const QString &via : request.headerValues(QStringLiteral("Via")))
        response.addHeader(QStringLiteral("Via"), via);
    response.addHeader(QStringLiteral("From"), request.header(QStringLiteral("From")));
    SipNameAddr to = SipNameAddr::parse(request.header(QStringLiteral("To")));
    if (to.param(QStringLiteral("tag")).isEmpty() && code > 100) {
        const bool ourDialog = request.callId() == m_callId && !m_localTag.isEmpty();
        to.setParam(QStringLiteral("tag"), ourDialog ? m_localTag : sip::generateTag());
    }
    response.addHeader(QStringLiteral("To"), to.toString());
    response.addHeader(QStringLiteral("Call-ID"), request.callId());
    response.addHeader(QStringLiteral("CSeq"), request.header(QStringLiteral("CSeq")));
    if (!m_account.userAgent.isEmpty())
        response.addHeader(QStringLiteral("Server"), m_account.userAgent);
    return response;
}

void SipUserAgent::sendResponse(const SipMessage &request, const SipMessage &response,
                                const QHostAddress &to, quint16 port)
{
    // Responses go back to the source address and port (rport, RFC 3581).
    const QByteArray wire = response.serialize();
    sendRaw(wire, to, port);

    // Cache for retransmitted non-INVITE requests; drop entries older than 32 s.
    const qint64 now = m_uptime.elapsed();
    const qint64 lifetime = qint64(m_timers.t1) * m_timers.timeoutFactor;
    for (auto it = m_responseCache.begin(); it != m_responseCache.end();) {
        if (now - it->second > lifetime)
            it = m_responseCache.erase(it);
        else
            ++it;
    }
    m_responseCache.insert(transactionKey(request.topViaBranch(), request.method()), {wire, now});
}

void SipUserAgent::sendInviteResponse(const SipMessage &request, const SipMessage &response,
                                      const QHostAddress &to, quint16 port)
{
    const QString key = request.callId() + QLatin1Char('|') + QString::number(request.cseqNumber());
    auto server = std::make_unique<ServerInvite>();
    server->branch = request.topViaBranch();
    server->response = response.serialize();
    server->address = to;
    server->port = port;
    server->interval = m_timers.t1;

    // Final responses to INVITE are retransmitted (Timer G) until the ACK
    // arrives or Timer H (64*T1) expires.
    server->retransmitTimer.setSingleShot(true);
    connect(&server->retransmitTimer, &QTimer::timeout, this, [this, key] {
        const auto it = m_serverInvites.find(key);
        if (it == m_serverInvites.end())
            return;
        ServerInvite &s = *it->second;
        sendRaw(s.response, s.address, s.port);
        s.interval = std::min(s.interval * 2, m_timers.t2);
        s.retransmitTimer.start(s.interval);
    });
    server->giveUpTimer.setSingleShot(true);
    connect(&server->giveUpTimer, &QTimer::timeout, this, [this, key] {
        emit logMessage(QStringLiteral("No ACK received for INVITE response"));
        m_serverInvites.erase(key);
    });
    server->retransmitTimer.start(server->interval);
    server->giveUpTimer.start(m_timers.t1 * m_timers.timeoutFactor);

    const QByteArray wire = server->response;
    m_serverInvites[key] = std::move(server);
    sendRaw(wire, to, port);
}

bool SipUserAgent::isInDialog(const SipMessage &request) const
{
    return m_dialogEstablished && request.callId() == m_callId && request.toTag() == m_localTag
           && request.fromTag() == m_remoteTag;
}

QByteArray SipUserAgent::localSdp(const std::optional<SdpSession> &answerTo, int *payloadType)
{
    SdpSession session;
    if (answerTo) {
        const auto answer = sdp::makeAnswer(*answerTo, m_localAddress, m_rtpPort, m_sdpSessionId,
                                            m_sdpVersion, payloadType);
        if (!answer)
            return QByteArray();
        session = *answer;
    } else {
        session = sdp::makeOffer(m_localAddress, m_rtpPort, m_sdpSessionId, m_sdpVersion);
        if (payloadType)
            *payloadType = kPayloadPcma;
    }
    // The origin version only increases when the session description changes.
    QByteArray body = session.toByteArray();
    if (!m_lastSdpBody.isEmpty() && body != m_lastSdpBody) {
        session.sessionVersion = ++m_sdpVersion;
        m_lastSdpBody = body;
        body = session.toByteArray();
    } else {
        m_lastSdpBody = body;
    }
    return body;
}

void SipUserAgent::handleSdpOffer(const SipMessage &request, const QHostAddress &from, quint16 port)
{
    const bool isInvite = request.method() == QLatin1String("INVITE");
    auto respond = [&](const SipMessage &response) {
        if (isInvite)
            sendInviteResponse(request, response, from, port);
        else
            sendResponse(request, response, from, port);
    };

    std::optional<SdpSession> offer;
    if (!request.body().isEmpty()) {
        offer = SdpSession::parse(request.body());
        if (!offer) {
            respond(makeResponse(request, 400, QStringLiteral("Malformed SDP")));
            return;
        }
    }

    int payloadType = m_payloadType;
    const QByteArray body = localSdp(offer, &payloadType);
    if (body.isEmpty()) {
        // No usable audio: typically a switch to T.38. We stay on G.711.
        SipMessage response = makeResponse(request, 488);
        response.addHeader(QStringLiteral("Warning"),
                           QStringLiteral("304 %1 \"Media type not available\"").arg(m_localAddress.toString()));
        respond(response);
        if (offer && offer->offersT38()) {
            emit logMessage(QStringLiteral("Rejected T.38 re-INVITE, staying on G.711"));
            emit t38Rejected();
        }
        return;
    }

    SipMessage response = makeResponse(request, 200);
    response.addHeader(QStringLiteral("Contact"), QStringLiteral("<%1>").arg(contactUri()));
    response.addHeader(QStringLiteral("Allow"), allowValue());
    response.setBody(body, kSdpType);
    respond(response);

    // Target refresh (RFC 3261 12.2.2).
    const QString contact = SipNameAddr::parse(request.header(QStringLiteral("Contact"))).uri;
    if (!contact.isEmpty())
        m_remoteTarget = contact;
    if (offer) {
        if (const auto remote = sdp::remoteAudio(*offer)) {
            AudioEndpointInfo info = *remote;
            info.payloadType = payloadType;
            m_payloadType = payloadType;
            emit remoteMediaChanged(info);
        }
    }
}

void SipUserAgent::handleRequest(const SipMessage &request, const QHostAddress &from, quint16 port)
{
    const QString method = request.method();

    if (method == QLatin1String("ACK")) {
        const QString key = request.callId() + QLatin1Char('|') + QString::number(request.cseqNumber());
        if (m_serverInvites.erase(key) > 0 && isInDialog(request) && !request.body().isEmpty()) {
            // Answer to an offer we made in a 200 OK (re-INVITE without SDP).
            if (const auto sdp = SdpSession::parse(request.body())) {
                if (const auto remote = sdp::remoteAudio(*sdp)) {
                    m_payloadType = remote->payloadType;
                    emit remoteMediaChanged(*remote);
                }
            }
        }
        return;
    }

    if (method == QLatin1String("INVITE")) {
        const QString key = request.callId() + QLatin1Char('|') + QString::number(request.cseqNumber());
        const auto existing = m_serverInvites.find(key);
        if (existing != m_serverInvites.end()) {
            // Retransmitted INVITE: repeat our response.
            sendRaw(existing->second->response, from, port);
            return;
        }
        if (isInDialog(request) && m_callState == CallState::Connected) {
            handleSdpOffer(request, from, port);
            return;
        }
        // We are a fax sender only: reject incoming calls.
        sendInviteResponse(request, makeResponse(request, 486), from, port);
        return;
    }

    // Non-INVITE: answer retransmissions from the cache.
    const auto cached = m_responseCache.constFind(transactionKey(request.topViaBranch(), method));
    if (cached != m_responseCache.constEnd()) {
        sendRaw(cached->first, from, port);
        return;
    }

    if (method == QLatin1String("OPTIONS")) {
        SipMessage response = makeResponse(request, 200);
        response.addHeader(QStringLiteral("Allow"), allowValue());
        response.addHeader(QStringLiteral("Accept"), kSdpType);
        sendResponse(request, response, from, port);
    } else if (method == QLatin1String("BYE")) {
        if (!isInDialog(request)) {
            sendResponse(request, makeResponse(request, 481), from, port);
            return;
        }
        sendResponse(request, makeResponse(request, 200), from, port);
        finishCall(true, QStringLiteral("remote side hung up"));
    } else if (method == QLatin1String("UPDATE")) {
        if (!isInDialog(request)) {
            sendResponse(request, makeResponse(request, 481), from, port);
            return;
        }
        if (request.body().isEmpty()) {
            sendResponse(request, makeResponse(request, 200), from, port);
            return;
        }
        handleSdpOffer(request, from, port);
    } else if (method == QLatin1String("CANCEL")) {
        const QString key = request.callId() + QLatin1Char('|') + QString::number(request.cseqNumber());
        // We answer INVITEs immediately, so a CANCEL has nothing left to cancel.
        sendResponse(request, makeResponse(request, m_serverInvites.count(key) ? 200 : 481), from, port);
    } else if (method == QLatin1String("NOTIFY")) {
        sendResponse(request, makeResponse(request, 200), from, port);
    } else {
        SipMessage response = makeResponse(request, 501);
        response.addHeader(QStringLiteral("Allow"), allowValue());
        sendResponse(request, response, from, port);
    }
}

} // namespace faxwerk
