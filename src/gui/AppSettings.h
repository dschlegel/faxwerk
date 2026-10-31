#pragma once

#include "config/PasswordStore.h"
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
// both share one configuration; [sip] location= names it.
//
// The passwords go into a PasswordStore (the system keychain) under
// PasswordStore::sipKey() and phonebookKey(). Only without a keychain, or
// when it fails, they are written to the INI file (password=,
// phonebook_password=, [sip] password=).
struct AppSettings
{
    QList<Location> locations;
    bool autoSelect = true; // pick the reachable location before each fax
    QString preferredId;    // used when autoSelect is off
    QString appearance = QStringLiteral("system"); // "system", "light", "dark" or "contrast"

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

    // Not stored: load() found passwords in the INI file. save() moves them
    // into the keychain.
    bool passwordsInFile = false;

    bool isConfigured() const;
    const Location *find(const QString &id) const;
    QList<Location> completeLocations() const;
    SipAccount account(const Location &location) const;

    static Location newLocation(const QString &name);
    // Passwords in the INI file win over the keychain; `passwords` may be
    // nullptr (INI file only).
    static AppSettings load(QSettings &ini, PasswordStore *passwords);
    // Writes the passwords into `passwords` and leaves them out of the INI
    // file. Returns false when they went into the INI file instead: without
    // a store, or when it could not store them (*error says why).
    bool save(QSettings &ini, PasswordStore *passwords, QString *error = nullptr) const;

    // The file the command line tool reads by default.
    static std::unique_ptr<QSettings> openDefault();
};

} // namespace faxwerk::gui
