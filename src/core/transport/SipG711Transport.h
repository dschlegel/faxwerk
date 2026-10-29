#pragma once

#include "FaxTransport.h"
#include "rtp/RtpSession.h"
#include "sip/SipUserAgent.h"

#include <QPointer>
#include <QTimer>

#include <memory>
#include <optional>

namespace faxwerk {

// Fax over SIP with G.711 audio: register as an IP phone, call the
// destination and run the T.30 modem in software over RTP.
//
// Sequence: REGISTER -> INVITE -> RTP + FaxEngine -> T.30 phase E -> BYE
// -> unregister -> finished().
class SipG711Transport : public FaxTransport
{
    Q_OBJECT

public:
    struct Options
    {
        SipAccount account;
        SipTimers timers;
        RtpSession::Options rtp;
        int ringTimeoutMs = 90000;
        // After T.30 completes, give the remote side this long to hang up first.
        int hangupDelayMs = 1500;
        // Upper bound for BYE/unregister during teardown.
        int teardownTimeoutMs = 5000;
    };

    explicit SipG711Transport(const Options &options, QObject *parent = nullptr);
    ~SipG711Transport() override;

    QString name() const override { return QStringLiteral("SIP/G.711"); }
    void send(const FaxSendRequest &request) override;
    void abort(const QString &reason) override;

    SipUserAgent *userAgent() const { return m_ua.get(); }

private:
    void onRegistered();
    void onCallEstablished(const AudioEndpointInfo &remote);
    void onFaxFinished(const FaxResult &result);
    void complete(const FaxOutcome &outcome);
    void teardown();
    bool callActive() const;

    Options m_options;
    FaxSendRequest m_request;
    std::unique_ptr<SipUserAgent> m_ua;
    std::unique_ptr<RtpSession> m_rtp;
    std::unique_ptr<FaxEngine> m_engine;
    std::optional<FaxOutcome> m_outcome;
    QTimer m_ringTimer;
    QTimer m_teardownGuard;
    bool m_started = false;
    bool m_callStarted = false;
    bool m_hangupSent = false;
    bool m_unregisterSent = false;
    bool m_unregistered = false;
    bool m_finishedEmitted = false;
};

} // namespace faxwerk
