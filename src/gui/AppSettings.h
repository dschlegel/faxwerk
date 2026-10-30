#pragma once

#include "sip/SipUserAgent.h"

#include <QSettings>
#include <QString>

#include <memory>

namespace faxwerk::gui {

// The settings the desktop app edits. They use the same INI file and keys as
// the command line tool, so both share one configuration.
struct AppSettings
{
    // [sip]
    QString server = QStringLiteral("fritz.box");
    quint16 port = 5060;
    QString domain;
    QString user;
    QString authUser;
    QString password;
    QString displayName;
    quint16 localPort = 0;
    int expires = 600;

    // [fax]
    QString ident;  // station ID, max. 20 characters
    QString header; // page header, max. 50 characters
    int maxRate = 9600;
    bool ecm = true;
    bool dither = false;

    // [job]
    int timeoutSeconds = 900;

    bool isConfigured() const { return !server.isEmpty() && !user.isEmpty() && !password.isEmpty(); }
    SipAccount account() const;

    static AppSettings load(QSettings &ini);
    void save(QSettings &ini) const;

    // The file the command line tool reads by default.
    static std::unique_ptr<QSettings> openDefault();
};

} // namespace faxwerk::gui
