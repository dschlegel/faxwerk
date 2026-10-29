#include "SipG711Transport.h"

namespace faxwerk {

namespace {

QString describeSipFailure(int code, const QString &reason)
{
    switch (code) {
    case 404: return QStringLiteral("number not found (%1)").arg(reason);
    case 408: return QStringLiteral("no response (%1)").arg(reason);
    case 480: return QStringLiteral("destination unavailable (%1)").arg(reason);
    case 486:
    case 600: return QStringLiteral("destination busy (%1)").arg(reason);
    case 487: return QStringLiteral("call cancelled (%1)").arg(reason);
    case 603: return QStringLiteral("call declined (%1)").arg(reason);
    default: return reason;
    }
}

} // namespace

SipG711Transport::SipG711Transport(const Options &options, QObject *parent)
    : FaxTransport(parent)
    , m_options(options)
{
    qRegisterMetaType<faxwerk::FaxOutcome>();

    m_ringTimer.setSingleShot(true);
    connect(&m_ringTimer, &QTimer::timeout, this, [this] {
        complete({FaxStatus::CallFailed,
                  QStringLiteral("no answer within %1 s").arg(m_options.ringTimeoutMs / 1000), {}, 0});
    });

    m_teardownGuard.setSingleShot(true);
    connect(&m_teardownGuard, &QTimer::timeout, this, [this] {
        emit logMessage(QStringLiteral("Teardown timed out, finishing anyway"));
        m_hangupSent = true;
        m_unregistered = true;
        if (m_rtp)
            m_rtp->stop();
        if (!m_finishedEmitted && m_outcome) {
            m_finishedEmitted = true;
            emit finished(*m_outcome);
        }
    });
}

SipG711Transport::~SipG711Transport()
{
    // The RTP session references the engine as its audio port.
    if (m_rtp)
        m_rtp->setAudioPort(nullptr);
}

void SipG711Transport::send(const FaxSendRequest &request)
{
    if (m_started)
        return;
    m_started = true;
    m_request = request;

    m_ua = std::make_unique<SipUserAgent>(m_options.account);
    m_ua->setTimers(m_options.timers);
    SipUserAgent *ua = m_ua.get();

    connect(ua, &SipUserAgent::trace, this,
            [this](bool outgoing, const QHostAddress &peer, quint16 port, const QByteArray &data) {
                emit protocolTrace(outgoing, QStringLiteral("%1:%2").arg(peer.toString()).arg(port), data);
            });
    connect(ua, &SipUserAgent::logMessage, this, &FaxTransport::logMessage);
    connect(ua, &SipUserAgent::registered, this, &SipG711Transport::onRegistered);
    connect(ua, &SipUserAgent::registrationFailed, this,
            [this](SipUserAgent::Failure failure, int code, const QString &reason) {
                if (m_callStarted) {
                    // A failed refresh does not affect an ongoing call.
                    emit logMessage(QStringLiteral("Registration refresh failed: %1").arg(reason));
                    return;
                }
                m_unregistered = true;
                FaxStatus status = FaxStatus::RegistrationFailed;
                if (failure == SipUserAgent::Failure::ResolveFailed || failure == SipUserAgent::Failure::NetworkError)
                    status = FaxStatus::NetworkError;
                else if (failure == SipUserAgent::Failure::AuthenticationFailed)
                    status = FaxStatus::AuthenticationFailed;
                complete({status, QStringLiteral("registration failed: %1").arg(reason), {}, code});
            });
    connect(ua, &SipUserAgent::unregistered, this, [this] {
        m_unregistered = true;
        teardown();
    });
    connect(ua, &SipUserAgent::callProgress, this, [this](int code, const QString &reason) {
        if (code == 180 || code == 183)
            emit statusChanged(QStringLiteral("Ringing (%1 %2)").arg(code).arg(reason));
    });
    connect(ua, &SipUserAgent::callEstablished, this, &SipG711Transport::onCallEstablished);
    connect(ua, &SipUserAgent::remoteMediaChanged, this, [this](const AudioEndpointInfo &remote) {
        emit logMessage(QStringLiteral("Remote media now %1:%2 (PT %3)")
                            .arg(remote.address.toString())
                            .arg(remote.port)
                            .arg(remote.payloadType));
        if (m_rtp)
            m_rtp->setRemote(remote.address, remote.port, remote.payloadType);
    });
    connect(ua, &SipUserAgent::t38Rejected, this, [this] {
        emit statusChanged(QStringLiteral("Remote requested T.38; declined, continuing with G.711"));
    });
    connect(ua, &SipUserAgent::callFailed, this,
            [this](SipUserAgent::Failure failure, int code, const QString &reason) {
                m_ringTimer.stop();
                const FaxStatus status = failure == SipUserAgent::Failure::AuthenticationFailed
                                             ? FaxStatus::AuthenticationFailed
                                             : FaxStatus::CallFailed;
                complete({status, QStringLiteral("call failed: %1").arg(describeSipFailure(code, reason)), {}, code});
            });
    connect(ua, &SipUserAgent::callEnded, this, [this](bool byRemote, const QString &reason) {
        m_ringTimer.stop();
        emit logMessage(QStringLiteral("Call ended: %1").arg(reason));
        if (m_rtp)
            m_rtp->stop();
        if (m_engine && !m_engine->isFinished()) {
            // Let T.30 decide whether the transfer was already complete;
            // its phase E result arrives via onFaxFinished().
            m_engine->terminate();
            return;
        }
        if (!m_engine) {
            complete({FaxStatus::CallFailed,
                      byRemote ? QStringLiteral("remote side hung up before the fax started") : reason, {}, 0});
            return;
        }
        teardown();
    });

    emit statusChanged(QStringLiteral("Registering at %1").arg(m_options.account.server));
    ua->start();
}

void SipG711Transport::onRegistered()
{
    if (m_callStarted || m_outcome)
        return;

    m_rtp = std::make_unique<RtpSession>(m_options.rtp);
    const QHostAddress local = m_ua->localAddress();
    const QHostAddress bindAddress = local.protocol() == QAbstractSocket::IPv6Protocol
                                         ? QHostAddress(QHostAddress::AnyIPv6)
                                         : QHostAddress(QHostAddress::AnyIPv4);
    QString error;
    if (!m_rtp->open(bindAddress, 0, &error)) {
        complete({FaxStatus::NetworkError, QStringLiteral("cannot open RTP port: %1").arg(error), {}, 0});
        return;
    }
    connect(m_rtp.get(), &RtpSession::socketError, this, [this](const QString &message) {
        emit logMessage(QStringLiteral("RTP: %1").arg(message));
    });

    m_callStarted = true;
    emit statusChanged(QStringLiteral("Registered, calling %1").arg(m_request.destination));
    if (!m_ua->call(m_request.destination, m_rtp->localPort())) {
        complete({FaxStatus::InternalError, QStringLiteral("cannot start call"), {}, 0});
        return;
    }
    m_ringTimer.start(m_options.ringTimeoutMs);
}

void SipG711Transport::onCallEstablished(const AudioEndpointInfo &remote)
{
    m_ringTimer.stop();
    if (m_outcome) {
        teardown(); // aborted while ringing
        return;
    }
    emit statusChanged(QStringLiteral("Connected, remote media %1:%2 (%3)")
                           .arg(remote.address.toString())
                           .arg(remote.port)
                           .arg(remote.payloadType == kPayloadPcma ? QStringLiteral("PCMA")
                                                                   : QStringLiteral("PCMU")));

    m_rtp->setRemote(remote.address, remote.port, remote.payloadType);
    m_engine = std::make_unique<FaxEngine>(FaxEngine::Role::Send, true, m_request.fax);
    connect(m_engine.get(), &FaxEngine::logMessage, this, &FaxTransport::logMessage);
    connect(m_engine.get(), &FaxEngine::negotiated, this,
            [this](int bitRate, bool ecm, const QString &remoteIdent) {
                emit statusChanged(QStringLiteral("Negotiated %1 bit/s%2%3")
                                       .arg(bitRate)
                                       .arg(ecm ? QStringLiteral(", ECM") : QString())
                                       .arg(remoteIdent.isEmpty() ? QString()
                                                                  : QStringLiteral(", remote \"%1\"").arg(remoteIdent)));
            });
    connect(m_engine.get(), &FaxEngine::pageTransferred, this, [this](int pages, int total) {
        emit pageTransferred(pages, total > 0 ? total : m_request.pageCount);
    });
    connect(m_engine.get(), &FaxEngine::finished, this, &SipG711Transport::onFaxFinished);

    QString error;
    if (!m_engine->start(m_request.tiffPath, &error)) {
        complete({FaxStatus::InternalError, error, {}, 0});
        return;
    }
    m_rtp->setAudioPort(m_engine.get());
    m_rtp->start();
}

void SipG711Transport::onFaxFinished(const FaxResult &result)
{
    if (!m_outcome) {
        FaxOutcome outcome;
        outcome.fax = result;
        if (result.success) {
            outcome.status = FaxStatus::Success;
            outcome.message = QStringLiteral("%1 page(s) sent at %2 bit/s%3")
                                  .arg(result.pagesTransferred)
                                  .arg(result.bitRate)
                                  .arg(result.ecm ? QStringLiteral(" with ECM") : QString());
        } else {
            outcome.status = FaxStatus::FaxFailed;
            outcome.message = QStringLiteral("fax transfer failed after %1 page(s): %2 (T.30 code %3)")
                                  .arg(result.pagesTransferred)
                                  .arg(result.completionText)
                                  .arg(result.completionCode);
        }
        m_outcome = outcome;
    }
    if (callActive()) {
        // Usually the receiver hangs up after DCN; if not, we do.
        QTimer::singleShot(m_options.hangupDelayMs, this, &SipG711Transport::teardown);
        return;
    }
    teardown();
}

void SipG711Transport::abort(const QString &reason)
{
    if (m_engine && !m_engine->isFinished())
        m_rtp->setAudioPort(nullptr);
    complete({FaxStatus::Aborted, reason, m_engine ? m_engine->result() : FaxResult{}, 0});
}

void SipG711Transport::complete(const FaxOutcome &outcome)
{
    if (!m_outcome)
        m_outcome = outcome;
    teardown();
}

bool SipG711Transport::callActive() const
{
    if (!m_ua)
        return false;
    switch (m_ua->callState()) {
    case SipUserAgent::CallState::Calling:
    case SipUserAgent::CallState::Proceeding:
    case SipUserAgent::CallState::Connected:
    case SipUserAgent::CallState::Terminating:
        return true;
    default:
        return false;
    }
}

void SipG711Transport::teardown()
{
    if (!m_outcome || m_finishedEmitted)
        return;
    if (!m_teardownGuard.isActive())
        m_teardownGuard.start(m_options.teardownTimeoutMs);

    if (!m_hangupSent && callActive()) {
        m_hangupSent = true;
        emit statusChanged(QStringLiteral("Hanging up"));
        m_ua->hangup();
        return;
    }
    if (callActive())
        return; // waiting for the BYE/CANCEL to complete

    if (m_rtp)
        m_rtp->stop();
    if (!m_ua)
        m_unregistered = true;
    if (!m_unregistered) {
        if (!m_unregisterSent) {
            m_unregisterSent = true;
            m_ua->unregister();
        }
        return;
    }

    m_teardownGuard.stop();
    m_finishedEmitted = true;
    emit finished(*m_outcome);
}

} // namespace faxwerk
