#pragma once

#include "DigestAuth.h"
#include "Sdp.h"
#include "SipMessage.h"

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QHostInfo>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QUdpSocket>

#include <functional>
#include <map>
#include <memory>

namespace faxwerk {

struct SipAccount
{
    QString server = QStringLiteral("fritz.box"); // registrar / outbound proxy
    quint16 serverPort = 5060;
    QString domain;       // SIP domain, defaults to server
    QString user;         // SIP user name (FRITZ!Box: "Benutzername" of the IP phone)
    QString authUser;     // digest user name, defaults to user
    QString password;
    QString displayName;
    quint16 localPort = 0; // 0 = any free port
    int registerExpires = 600;
    QString userAgent;

    QString effectiveDomain() const { return domain.isEmpty() ? server : domain; }
    QString effectiveAuthUser() const { return authUser.isEmpty() ? user : authUser; }
};

// RFC 3261 timer values in milliseconds.
struct SipTimers
{
    int t1 = 500;
    int t2 = 4000;
    int timeoutFactor = 64; // Timer B/F = 64*T1 = 32 s
};

// A minimal SIP user agent over UDP, just enough to register as an IP phone
// and to place one audio call at a time.
class SipUserAgent : public QObject
{
    Q_OBJECT

public:
    enum class Failure {
        None,
        ResolveFailed,
        NetworkError,
        Timeout,
        AuthenticationFailed,
        Rejected,
    };
    Q_ENUM(Failure)

    enum class RegistrationState { Idle, Resolving, Registering, Registered, Unregistering, Failed };
    Q_ENUM(RegistrationState)

    enum class CallState { Idle, Calling, Proceeding, Connected, Terminating, Terminated };
    Q_ENUM(CallState)

    explicit SipUserAgent(const SipAccount &account, QObject *parent = nullptr);
    ~SipUserAgent() override;

    void setTimers(const SipTimers &timers) { m_timers = timers; }

    // Resolves the server, binds the socket and registers.
    void start();
    // Sends REGISTER with Expires: 0; emits unregistered() when done.
    void unregister();

    // Places a call to sip:<number>@<domain>, offering G.711 on localRtpPort.
    // Requires start() to have resolved the server.
    bool call(const QString &number, quint16 localRtpPort);
    // CANCEL while ringing, BYE when connected.
    void hangup();

    RegistrationState registrationState() const { return m_regState; }
    CallState callState() const { return m_callState; }
    QHostAddress localAddress() const { return m_localAddress; }
    quint16 localPort() const { return m_socket.localPort(); }
    QHostAddress serverAddress() const { return m_serverAddress; }

signals:
    void resolved(const QHostAddress &server, const QHostAddress &local);
    void registered(int expires);
    void registrationFailed(faxwerk::SipUserAgent::Failure failure, int statusCode, const QString &reason);
    void unregistered();

    void callProgress(int statusCode, const QString &reason);
    void callEstablished(const faxwerk::AudioEndpointInfo &remote);
    void remoteMediaChanged(const faxwerk::AudioEndpointInfo &remote);
    void t38Rejected();
    void callFailed(faxwerk::SipUserAgent::Failure failure, int statusCode, const QString &reason);
    void callEnded(bool byRemote, const QString &reason);

    // Every datagram sent or received (for SIP traces).
    void trace(bool outgoing, const QHostAddress &peer, quint16 port, const QByteArray &data);
    void logMessage(const QString &message);

private:
    struct Outgoing
    {
        SipMessage message;
        QHostAddress address;
        quint16 port = 0;
    };
    struct ClientTransaction;
    struct RequestContext;
    struct ServerInvite;

    // Transport
    void onHostResolved(const QHostInfo &info);
    void onReadyRead();
    void sendRaw(const QByteArray &data, const QHostAddress &address, quint16 port);
    QString viaValue(const QString &branch) const;
    QString contactUri() const;
    QString localUri() const;
    QString allowValue() const;

    // Client transactions (RFC 3261 section 17.1)
    void startClientTransaction(const SipMessage &request, const QHostAddress &address, quint16 port,
                                std::function<void(const SipMessage &)> onResponse,
                                std::function<void()> onTimeout);
    void retransmitClientTransaction(const QString &key);
    void timeoutClientTransaction(const QString &key);
    void handleResponse(const SipMessage &response, const QHostAddress &from, quint16 port);
    SipMessage buildNon2xxAck(const SipMessage &invite, const SipMessage &response) const;

    // Requests with digest authentication retries
    void dispatch(const std::shared_ptr<RequestContext> &ctx);
    std::optional<DigestChallenge> findChallenge(const SipMessage &response, bool proxy) const;

    // Registration
    void sendRegister(int expires);
    void handleRegisterResponse(const SipMessage &response, bool authFailed, int requestedExpires);

    // Call
    void handleInviteSuccess(const SipMessage &response);
    void sendAck2xx();
    void sendCancel();
    void sendBye();
    Outgoing buildInDialog(const QString &method, int cseq) const;
    std::pair<QHostAddress, quint16> nextHopFor(const QString &uri) const;
    void finishCall(bool byRemote, const QString &reason);
    void failCall(Failure failure, int code, const QString &reason);
    void resetCall();

    // Server side
    void handleRequest(const SipMessage &request, const QHostAddress &from, quint16 port);
    SipMessage makeResponse(const SipMessage &request, int code, const QString &reason = QString()) const;
    void sendResponse(const SipMessage &request, const SipMessage &response, const QHostAddress &to,
                      quint16 port);
    void sendInviteResponse(const SipMessage &request, const SipMessage &response,
                            const QHostAddress &to, quint16 port);
    bool isInDialog(const SipMessage &request) const;
    void handleSdpOffer(const SipMessage &request, const QHostAddress &from, quint16 port);
    QByteArray localSdp(const std::optional<SdpSession> &answerTo, int *payloadType);

    SipAccount m_account;
    SipTimers m_timers;
    QUdpSocket m_socket;
    DigestAuth m_digest;

    QHostAddress m_serverAddress;
    QHostAddress m_localAddress;
    bool m_resolved = false;
    int m_lookupId = -1;

    std::map<QString, std::unique_ptr<ClientTransaction>> m_transactions; // key: branch|method
    QHash<QString, QPair<QByteArray, QPair<QHostAddress, quint16>>> m_non2xxAcks; // INVITE branch -> ACK
    // Non-INVITE server transactions: request key -> (response, time sent)
    QHash<QString, QPair<QByteArray, qint64>> m_responseCache;
    std::map<QString, std::unique_ptr<ServerInvite>> m_serverInvites; // key: Call-ID|CSeq
    QElapsedTimer m_uptime;

    // Registration
    RegistrationState m_regState = RegistrationState::Idle;
    QString m_regCallId;
    QString m_regTag;
    int m_regCseq = 0;
    std::optional<DigestChallenge> m_regChallenge;
    bool m_regChallengeProxy = false;
    QTimer m_refreshTimer;

    // Call / dialog
    CallState m_callState = CallState::Idle;
    QString m_callId;
    QString m_localTag;
    QString m_remoteTag;
    QString m_remoteUri;  // To URI of our INVITE
    QString m_remoteTarget;
    QStringList m_routeSet;
    int m_localCseq = 0;
    int m_inviteCseq = 0;
    SipMessage m_lastInvite;
    bool m_inviteProvisional = false;
    bool m_cancelPending = false;
    bool m_cancelSent = false;
    bool m_dialogEstablished = false;
    bool m_callOutcomeReported = false;
    Outgoing m_ack;
    quint16 m_rtpPort = 0;
    quint64 m_sdpSessionId = 0;
    quint64 m_sdpVersion = 0;
    QByteArray m_lastSdpBody;
    int m_payloadType = kPayloadPcma;
};

} // namespace faxwerk

Q_DECLARE_METATYPE(faxwerk::AudioEndpointInfo)
