#include "LocationProbe.h"

namespace faxwerk::gui {

namespace {

// Upper bound for signing off a probe registration before the agent goes away.
constexpr int kRetireTimeoutMs = 40000;

} // namespace

LocationProbe::LocationProbe(QObject *parent)
    : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { evaluate(true); });
}

LocationProbe::~LocationProbe()
{
    for (const Attempt &attempt : m_attempts)
        retire(attempt.agent, attempt.state == State::Registered);
}

void LocationProbe::cancel()
{
    m_timeout.stop();
    for (const Attempt &attempt : m_attempts)
        retire(attempt.agent, attempt.state == State::Registered);
    m_attempts.clear();
}

void LocationProbe::start(const AppSettings &settings, const QList<Location> &locations, int timeoutMs)
{
    cancel();

    if (locations.isEmpty()) {
        QMetaObject::invokeMethod(this, [this] { emit finished(QString()); }, Qt::QueuedConnection);
        return;
    }

    m_attempts.reserve(size_t(locations.size()));
    for (const Location &location : locations) {
        auto *agent = new SipUserAgent(settings.account(location));
        m_attempts.push_back({location.id, agent, State::Pending});
        connect(agent, &SipUserAgent::registered, this, [this, agent](int) {
            for (Attempt &attempt : m_attempts) {
                if (attempt.agent == agent)
                    attempt.state = State::Registered;
            }
            evaluate(false);
        });
        connect(agent, &SipUserAgent::registrationFailed, this, [this, agent] {
            for (Attempt &attempt : m_attempts) {
                if (attempt.agent == agent)
                    attempt.state = State::Failed;
            }
            evaluate(false);
        });
    }
    m_timeout.start(timeoutMs);
    for (const Attempt &attempt : m_attempts)
        attempt.agent->start();
}

void LocationProbe::evaluate(bool timedOut)
{
    // The first location in list order that is not known to be unreachable
    // decides: registered wins, pending means wait (unless time is up).
    for (const Attempt &attempt : m_attempts) {
        if (attempt.state == State::Registered) {
            finish(attempt.id);
            return;
        }
        if (attempt.state == State::Pending && !timedOut)
            return;
    }
    finish(QString());
}

void LocationProbe::finish(const QString &id)
{
    m_timeout.stop();
    for (const Attempt &attempt : m_attempts)
        retire(attempt.agent, attempt.state == State::Registered);
    m_attempts.clear();
    emit finished(id);
}

void LocationProbe::retire(SipUserAgent *agent, bool registered)
{
    // Leave no registration behind: sign off what registered, and also what
    // registers late; then delete the agent.
    agent->disconnect();
    QObject::connect(agent, &SipUserAgent::registered, agent, [agent](int) { agent->unregister(); });
    QObject::connect(agent, &SipUserAgent::unregistered, agent, &QObject::deleteLater);
    QObject::connect(agent, &SipUserAgent::registrationFailed, agent, &QObject::deleteLater);
    QTimer::singleShot(kRetireTimeoutMs, agent, &QObject::deleteLater);
    if (registered)
        agent->unregister();
}

} // namespace faxwerk::gui
