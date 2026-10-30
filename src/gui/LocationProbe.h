#pragma once

#include "AppSettings.h"

#include <QObject>
#include <QTimer>

#include <vector>

namespace faxwerk::gui {

// Finds the location whose router is reachable right now (for example the one
// the VPN currently connects to): registers at all locations in parallel,
// signs off again at once and reports the first one, in list order, that
// accepted the registration.
class LocationProbe : public QObject
{
    Q_OBJECT

public:
    explicit LocationProbe(QObject *parent = nullptr);
    ~LocationProbe() override;

    // Restarts if a probe is running. timeoutMs bounds the wait for routers
    // that don't answer.
    void start(const AppSettings &settings, const QList<Location> &locations, int timeoutMs = 8000);
    // Stops without emitting finished().
    void cancel();
    bool isRunning() const { return !m_attempts.empty(); }

signals:
    // The reachable location's id, or empty if none answered.
    void finished(const QString &locationId);

private:
    enum class State { Pending, Registered, Failed };
    struct Attempt
    {
        QString id;
        SipUserAgent *agent = nullptr;
        State state = State::Pending;
    };

    void evaluate(bool timedOut);
    void finish(const QString &id);
    static void retire(SipUserAgent *agent, bool registered);

    std::vector<Attempt> m_attempts;
    QTimer m_timeout;
};

} // namespace faxwerk::gui
