#include "FaxEngine.h"

#include <QMetaObject>

#include <algorithm>
#include <mutex>

#include <spandsp.h>

namespace faxwerk {

namespace {

t30_state_t *t30Of(fax_state_t *fax)
{
    return fax_get_t30_state(fax);
}

FaxEngine *engineFrom(void *userData)
{
    return static_cast<FaxEngine *>(userData);
}

// --- spandsp callbacks; the signatures differ between API generations -------

#if FAXWERK_SPANDSP_NEW_API
int phaseBHandler(void *userData, int result)
{
    engineFrom(userData)->handlePhaseB(result);
    return T30_ERR_OK;
}

int phaseDHandler(void *userData, int result)
{
    engineFrom(userData)->handlePhaseD(result);
    return T30_ERR_OK;
}

void phaseEHandler(void *userData, int completionCode)
{
    engineFrom(userData)->handlePhaseE(completionCode);
}
#else
int phaseBHandler(t30_state_t *, void *userData, int result)
{
    engineFrom(userData)->handlePhaseB(result);
    return T30_ERR_OK;
}

int phaseDHandler(t30_state_t *, void *userData, int result)
{
    engineFrom(userData)->handlePhaseD(result);
    return T30_ERR_OK;
}

void phaseEHandler(t30_state_t *, void *userData, int completionCode)
{
    engineFrom(userData)->handlePhaseE(completionCode);
}
#endif

#if FAXWERK_SPANDSP_LOG_USER_DATA
void logHandler(void *userData, int level, const char *text)
{
    engineFrom(userData)->handleLog(level, text);
}

void installLogHandler(logging_state_t *log, FaxEngine *engine)
{
    span_log_set_message_handler(log, logHandler, engine);
}

void removeLogHandler(FaxEngine *) {}
#else
// spandsp 0.0.6 has no user_data for log handlers: route the messages to the
// (single) engine that enabled protocol logging most recently.
std::mutex g_logMutex;
FaxEngine *g_logEngine = nullptr;

void logHandler(int level, const char *text)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logEngine)
        g_logEngine->handleLog(level, text);
}

void installLogHandler(logging_state_t *log, FaxEngine *engine)
{
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        g_logEngine = engine;
    }
    span_log_set_message_handler(log, logHandler);
}

void removeLogHandler(FaxEngine *engine)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logEngine == engine)
        g_logEngine = nullptr;
}
#endif

void configureLogging(logging_state_t *log, FaxEngine *engine, bool enabled, const char *tag)
{
    if (!enabled) {
        span_log_set_level(log, 0);
        return;
    }
    span_log_set_level(log, SPAN_LOG_SHOW_SEVERITY | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_FLOW);
    span_log_set_tag(log, tag);
    installLogHandler(log, engine);
}

int supportedModems(int maxBitRate)
{
    int modems = T30_SUPPORT_V27TER;
    if (maxBitRate > 4800)
        modems |= T30_SUPPORT_V29;
    if (maxBitRate > 9600)
        modems |= T30_SUPPORT_V17;
    return modems;
}

} // namespace

FaxEngine::FaxEngine(Role role, bool callingParty, const FaxOptions &options, QObject *parent)
    : QObject(parent)
    , m_role(role)
    , m_callingParty(callingParty)
    , m_options(options)
{
    qRegisterMetaType<faxwerk::FaxResult>();
}

FaxEngine::~FaxEngine()
{
    removeLogHandler(this);
    if (m_fax) {
        // Detach the callbacks first: releasing must not call back into us.
        t30_state_t *t30 = t30Of(m_fax);
        t30_set_phase_b_handler(t30, nullptr, nullptr);
        t30_set_phase_d_handler(t30, nullptr, nullptr);
        t30_set_phase_e_handler(t30, nullptr, nullptr);
        fax_free(m_fax);
    }
}

bool FaxEngine::start(const QString &tiffPath, QString *error)
{
    if (m_fax) {
        if (error)
            *error = QStringLiteral("fax engine already started");
        return false;
    }
    m_fax = fax_init(nullptr, m_callingParty ? 1 : 0);
    if (!m_fax) {
        if (error)
            *error = QStringLiteral("fax_init() failed");
        return false;
    }

    configureLogging(fax_get_logging_state(m_fax), this, m_options.protocolLog, "FAX");
    t30_state_t *t30 = t30Of(m_fax);
    configureLogging(t30_get_logging_state(t30), this, m_options.protocolLog, "T.30");

    // Keep emitting silence when the modem has nothing to send, so RTP
    // keeps flowing with constant timing.
    fax_set_transmit_on_idle(m_fax, 1);

    const QByteArray ident = m_options.stationId.left(20).toLatin1();
    if (!ident.isEmpty())
        t30_set_tx_ident(t30, ident.constData());
    const QByteArray header = m_options.headerInfo.left(50).toLatin1();
    if (!header.isEmpty())
        t30_set_tx_page_header_info(t30, header.constData());

    t30_set_ecm_capability(t30, m_options.ecm ? 1 : 0);
    t30_set_supported_modems(t30, supportedModems(m_options.maxBitRate));

    t30_set_phase_b_handler(t30, phaseBHandler, this);
    t30_set_phase_d_handler(t30, phaseDHandler, this);
    t30_set_phase_e_handler(t30, phaseEHandler, this);

    const QByteArray path = tiffPath.toLocal8Bit();
    if (m_role == Role::Send)
        t30_set_tx_file(t30, path.constData(), -1, -1);
    else
        t30_set_rx_file(t30, path.constData(), -1);
    return true;
}

void FaxEngine::terminate()
{
    if (m_fax && !m_finished)
        t30_terminate(t30Of(m_fax));
}

void FaxEngine::receiveAudio(const std::int16_t *samples, int count)
{
    if (!m_fax || m_finished || count <= 0)
        return;
    // spandsp's fax_rx() takes a non-const buffer but does not modify it.
    fax_rx(m_fax, const_cast<std::int16_t *>(samples), count);
}

void FaxEngine::receiveGap(int count)
{
    if (!m_fax || m_finished || count <= 0)
        return;
    fax_rx_fillin(m_fax, count);
}

void FaxEngine::produceAudio(std::int16_t *samples, int count)
{
    int produced = 0;
    if (m_fax && !m_finished)
        produced = std::max(0, fax_tx(m_fax, samples, count));
    std::fill(samples + produced, samples + count, std::int16_t(0));
}

void FaxEngine::fillStatistics(FaxResult &result) const
{
    if (!m_fax)
        return;
    t30_state_t *t30 = t30Of(m_fax);
    t30_stats_t stats;
    t30_get_transfer_statistics(t30, &stats);
    result.bitRate = stats.bit_rate;
    result.ecm = stats.error_correcting_mode != 0;
    result.pagesTransferred = m_role == Role::Send ? stats.pages_tx : stats.pages_rx;
    result.pagesInFile = stats.pages_in_file;
    const char *remote = t30_get_rx_ident(t30);
    result.remoteIdent = remote ? QString::fromLatin1(remote).trimmed() : QString();
}

void FaxEngine::handlePhaseB(int)
{
    FaxResult r;
    fillStatistics(r);
    emit negotiated(r.bitRate, r.ecm, r.remoteIdent);
}

void FaxEngine::handlePhaseD(int)
{
    FaxResult r;
    fillStatistics(r);
    emit pageTransferred(r.pagesTransferred, r.pagesInFile);
}

void FaxEngine::handlePhaseE(int completionCode)
{
    if (m_finished)
        return;
    m_finished = true;
    m_result = FaxResult{};
    fillStatistics(m_result);
    m_result.completionCode = completionCode;
    m_result.success = completionCode == T30_ERR_OK;
    m_result.completionText = QString::fromLatin1(t30_completion_code_to_str(completionCode));
    // We are inside fax_rx()/fax_tx(); deliver the result from the event loop
    // so that receivers may safely tear down the engine.
    QMetaObject::invokeMethod(this, [this] { emit finished(m_result); }, Qt::QueuedConnection);
}

void FaxEngine::handleLog(int, const char *text)
{
    QString message = QString::fromLatin1(text);
    while (message.endsWith(QLatin1Char('\n')))
        message.chop(1);
    emit logMessage(message);
}

} // namespace faxwerk
