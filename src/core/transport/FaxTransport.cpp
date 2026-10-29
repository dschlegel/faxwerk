#include "FaxTransport.h"

namespace faxwerk {

QString faxStatusName(FaxStatus status)
{
    switch (status) {
    case FaxStatus::Success: return QStringLiteral("success");
    case FaxStatus::InvalidRequest: return QStringLiteral("invalid request");
    case FaxStatus::DocumentError: return QStringLiteral("document error");
    case FaxStatus::NetworkError: return QStringLiteral("network error");
    case FaxStatus::AuthenticationFailed: return QStringLiteral("authentication failed");
    case FaxStatus::RegistrationFailed: return QStringLiteral("registration failed");
    case FaxStatus::CallFailed: return QStringLiteral("call failed");
    case FaxStatus::FaxFailed: return QStringLiteral("fax transfer failed");
    case FaxStatus::Timeout: return QStringLiteral("timeout");
    case FaxStatus::Aborted: return QStringLiteral("aborted");
    case FaxStatus::InternalError: return QStringLiteral("internal error");
    }
    return QStringLiteral("unknown");
}

} // namespace faxwerk
