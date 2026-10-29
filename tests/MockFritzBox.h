#pragma once

#include "fax/FaxEngine.h"
#include "rtp/RtpSession.h"
#include "sip/SipMessage.h"

#include <QHostAddress>
#include <QObject>
#include <QStringList>
#include <QUdpSocket>

#include <memory>

namespace faxwerk::test {

// A tiny SIP registrar + user agent server on 127.0.0.1 that behaves like a
// router answering a call with a fax machine:
//   REGISTER -> 401 (digest with qop=auth) -> 200
//   INVITE   -> 407 (digest without qop)  -> ACK -> INVITE -> 100/180/200 + SDP
//   RTP with a receiving FaxEngine; after T.30 phase E it hangs up with BYE.
// Digest responses are verified independently with QCryptographicHash, and
// every protocol violation is recorded in errors().
class MockFritzBox : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString user = QStringLiteral("faxwerk");
        QString password = QStringLiteral("s3cret");
        QString realm = QStringLiteral("fritz.box");
        RtpSession::Options rtp;
        QString rxTiffPath;
        bool retransmitOk = true;   // send the 200 OK twice; both must be ACKed
        bool sendT38ReInvite = false;
        int rejectInviteWith = 0;   // e.g. 486; 0 = answer the call
    };

    explicit MockFritzBox(const Options &options, QObject *parent = nullptr);
    ~MockFritzBox() override;

    bool listen();
    quint16 port() const { return m_socket.localPort(); }

    QStringList errors() const { return m_errors; }
    int registerChallenges() const { return m_registerChallenges; }
    int inviteChallenges() const { return m_inviteChallenges; }
    bool registered() const { return m_registered; }
    bool unregistered() const { return m_unregistered; }
    bool ackForChallengeSeen() const { return m_ackFor407; }
    int acksFor200() const { return m_acksFor200; }
    bool t38Rejected() const { return m_t38Rejected; }
    bool byeAnswered() const { return m_byeAnswered; }
    bool faxFinished() const { return m_faxFinished; }
    FaxResult faxResult() const { return m_faxResult; }

signals:
    void callFinished();

private:
    void onReadyRead();
    void handleRequest(const SipMessage &request);
    void handleResponse(const SipMessage &response);
    void handleRegister(const SipMessage &request);
    void handleInvite(const SipMessage &request);
    void handleAck(const SipMessage &request);
    void answerCall();
    void sendT38ReInvite();
    void sendBye();
    void send(const QByteArray &data);
    SipMessage makeResponse(const SipMessage &request, int code, const QString &toTag = QString()) const;
    bool verifyDigest(const QString &header, const QString &method, const QString &nonce, bool withQop);
    void error(const QString &message);

    Options m_options;
    QUdpSocket m_socket;
    QHostAddress m_peer;
    quint16 m_peerPort = 0;
    QStringList m_errors;

    QString m_registerNonce;
    QString m_inviteNonce;
    int m_registerChallenges = 0;
    int m_inviteChallenges = 0;
    bool m_registered = false;
    bool m_unregistered = false;

    // Dialog state
    SipMessage m_challengedInvite;
    SipMessage m_invite;
    QString m_toTag;
    QString m_contactUri;
    QString m_peerContact;
    bool m_ackFor407 = false;
    int m_acksFor200 = 0;
    SipMessage m_reInvite;
    QString m_reInviteBranch;
    bool m_t38Rejected = false;
    QString m_byeBranch;
    bool m_byeAnswered = false;
    int m_localCseq = 100;

    std::unique_ptr<RtpSession> m_rtp;
    std::unique_ptr<FaxEngine> m_fax;
    bool m_faxFinished = false;
    FaxResult m_faxResult;
};

} // namespace faxwerk::test
