#pragma once

#include "transport/FaxTransport.h"

namespace faxwerk::cli {

// Process exit codes, one per error class (documented in README.md).
enum ExitCode : int {
    kExitOk = 0,
    kExitInternal = 1,       // unexpected internal error
    kExitUsage = 2,          // invalid command line or configuration
    kExitDocument = 3,       // PDF cannot be read or rendered, TIFF cannot be written
    kExitNetwork = 4,        // server not resolvable, socket/port problems
    kExitAuth = 5,           // wrong SIP user name or password
    kExitRegistration = 6,   // registration rejected or not answered
    kExitCall = 7,           // busy, not found, no answer, rejected
    kExitFax = 8,            // T.30 negotiation/transfer failed
    kExitTimeout = 9,        // overall timeout
    kExitInterrupted = 130,  // SIGINT / Ctrl+C
};

inline int exitCodeFor(FaxStatus status)
{
    switch (status) {
    case FaxStatus::Success: return kExitOk;
    case FaxStatus::InvalidRequest: return kExitUsage;
    case FaxStatus::DocumentError: return kExitDocument;
    case FaxStatus::NetworkError: return kExitNetwork;
    case FaxStatus::AuthenticationFailed: return kExitAuth;
    case FaxStatus::RegistrationFailed: return kExitRegistration;
    case FaxStatus::CallFailed: return kExitCall;
    case FaxStatus::FaxFailed: return kExitFax;
    case FaxStatus::Timeout: return kExitTimeout;
    case FaxStatus::Aborted: return kExitInterrupted;
    case FaxStatus::InternalError: return kExitInternal;
    }
    return kExitInternal;
}

} // namespace faxwerk::cli
