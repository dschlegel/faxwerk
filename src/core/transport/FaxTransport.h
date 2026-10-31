#pragma once

#include "fax/FaxEngine.h"

#include <QObject>
#include <QString>

namespace faxwerk {

enum class FaxStatus {
    Success,
    InvalidRequest,
    DocumentError,
    NetworkError,         // DNS, socket, no route
    AuthenticationFailed, // wrong SIP credentials
    RegistrationFailed,   // registrar rejected us or did not answer
    CallFailed,           // busy, not found, no answer, rejected
    FaxFailed,            // T.30 negotiation or transfer failed
    Timeout,              // overall job timeout
    Aborted,              // cancelled by the user
    InternalError,
};

QString faxStatusName(FaxStatus status);

// Coarse progress of a delivery, for user interfaces.
enum class FaxPhase {
    Rendering,    // PDF -> fax pages
    Registering,  // signing in at the router
    Dialing,      // call placed, waiting for the remote side to answer
    Connected,    // call answered, fax handshake running
    Transferring, // fax parameters agreed, pages are being sent
    HangingUp,    // ending the call and signing off
};

struct FaxSendRequest
{
    QString destination; // number to dial
    QString tiffPath;    // fax TIFF produced by PdfRasterizer
    int pageCount = 0;
    FaxOptions fax;
};

struct FaxOutcome
{
    FaxStatus status = FaxStatus::InternalError;
    QString message;
    FaxResult fax;       // valid when the T.30 session started
    int sipStatus = 0;   // final SIP status code if a SIP failure occurred
};

// Abstract fax delivery backend.
//
// The current implementation is SipG711Transport (SIP registration + G.711
// audio + software modem). The interface is deliberately independent of SIP
// so that T.38 (UDPTL) or CAPI-over-TCP backends can be added later without
// touching FaxJob or the user interfaces.
class FaxTransport : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~FaxTransport() override = default;

    virtual QString name() const = 0;
    // Starts the delivery; finished() is emitted exactly once afterwards.
    virtual void send(const FaxSendRequest &request) = 0;
    // Stops as soon as possible (hang up, unregister) and emits finished().
    virtual void abort(const QString &reason) = 0;

signals:
    void statusChanged(const QString &status);
    void phaseChanged(faxwerk::FaxPhase phase);
    void negotiated(int bitRate, bool ecm, const QString &remoteIdent);
    void pageTransferred(int pages, int total);
    void finished(const faxwerk::FaxOutcome &outcome);
    void logMessage(const QString &message);
    // Raw signaling traffic (e.g. SIP datagrams) for diagnostics.
    void protocolTrace(bool outgoing, const QString &peer, const QByteArray &data);
};

} // namespace faxwerk

Q_DECLARE_METATYPE(faxwerk::FaxOutcome)
Q_DECLARE_METATYPE(faxwerk::FaxPhase)
