#pragma once

#include "sip/SipUserAgent.h"

#include <QList>
#include <QSettings>
#include <QString>

#include <memory>

namespace faxwerk::gui {

// One router Faxwerk can send through (e.g. one practice), with its own IP
// phone credentials and sender details.
struct Location
{
    QString id; // stable key, also stored in the journal
    QString name;
    QString server = QStringLiteral("fritz.box");
    quint16 port = 5060;
    QString domain;
    QString user;
    QString authUser;
    QString password;
    QString ident;  // station ID, max. 20 characters
    QString header; // page header, max. 50 characters
    // FRITZ!Box user for the phonebooks (TR-064); without a password the
    // phonebook is not used. The user may be empty for routers that sign in
    // with a password only.
    QString phonebookUser;
    QString phonebookPassword;

    bool hasPhonebook() const { return !server.isEmpty() && !phonebookPassword.isEmpty(); }
    bool isComplete() const { return !server.isEmpty() && !user.isEmpty() && !password.isEmpty(); }
    QString displayName() const { return name.isEmpty() ? server : name; }
};

// The settings the desktop app edits.
//
// Locations are stored as the INI array [locations]. The first location is
// also written to [sip] and [fax], the keys the command line tool reads, so
// both share one configuration.
struct AppSettings
{
    QList<Location> locations;
    bool autoSelect = true; // pick the reachable location before each fax
    QString preferredId;    // used when autoSelect is off
    QString appearance = QStringLiteral("system"); // "system", "light" or "dark"

    // [sip], shared by all locations
    QString displayName;
    quint16 localPort = 0;
    int expires = 600;

    // [fax]
    int maxRate = 9600;
    bool ecm = true;
    bool dither = false;

    // [job]
    int timeoutSeconds = 900;

    bool isConfigured() const;
    const Location *find(const QString &id) const;
    QList<Location> completeLocations() const;
    SipAccount account(const Location &location) const;

    static Location newLocation(const QString &name);
    static AppSettings load(QSettings &ini);
    void save(QSettings &ini) const;

    // The file the command line tool reads by default.
    static std::unique_ptr<QSettings> openDefault();
};

} // namespace faxwerk::gui
