#include "AppSettings.h"

#include "ProductInfo.h"

namespace faxwerk::gui {

SipAccount AppSettings::account() const
{
    SipAccount account;
    account.server = server;
    account.serverPort = port;
    account.domain = domain;
    account.user = user;
    account.authUser = authUser;
    account.password = password;
    account.displayName = displayName;
    account.localPort = localPort;
    account.registerExpires = expires;
    account.userAgent = QStringLiteral("%1/%2").arg(QStringLiteral(FAXWERK_PRODUCT_NAME), QStringLiteral(FAXWERK_VERSION));
    return account;
}

AppSettings AppSettings::load(QSettings &ini)
{
    AppSettings s;
    s.server = ini.value(QStringLiteral("sip/server"), s.server).toString();
    s.port = quint16(ini.value(QStringLiteral("sip/port"), s.port).toUInt());
    if (s.port == 0)
        s.port = 5060;
    s.domain = ini.value(QStringLiteral("sip/domain")).toString();
    s.user = ini.value(QStringLiteral("sip/user")).toString();
    s.authUser = ini.value(QStringLiteral("sip/auth_user")).toString();
    s.password = ini.value(QStringLiteral("sip/password")).toString();
    if (s.password.isEmpty())
        s.password = qEnvironmentVariable("FAXWERK_PASSWORD");
    s.displayName = ini.value(QStringLiteral("sip/display_name")).toString();
    s.localPort = quint16(ini.value(QStringLiteral("sip/local_port"), 0).toUInt());
    s.expires = ini.value(QStringLiteral("sip/expires"), s.expires).toInt();

    s.ident = ini.value(QStringLiteral("fax/ident")).toString().left(20);
    s.header = ini.value(QStringLiteral("fax/header")).toString().left(50);
    s.maxRate = ini.value(QStringLiteral("fax/max_rate"), s.maxRate).toInt() == 14400 ? 14400 : 9600;
    s.ecm = ini.value(QStringLiteral("fax/ecm"), s.ecm).toBool();
    s.dither = ini.value(QStringLiteral("fax/dither"), s.dither).toBool();

    const int timeout = ini.value(QStringLiteral("job/timeout"), s.timeoutSeconds).toInt();
    if (timeout > 0)
        s.timeoutSeconds = timeout;
    return s;
}

void AppSettings::save(QSettings &ini) const
{
    ini.setValue(QStringLiteral("sip/server"), server);
    ini.setValue(QStringLiteral("sip/port"), port);
    ini.setValue(QStringLiteral("sip/user"), user);
    ini.setValue(QStringLiteral("sip/password"), password);
    ini.setValue(QStringLiteral("fax/ident"), ident);
    ini.setValue(QStringLiteral("fax/header"), header);
    ini.setValue(QStringLiteral("fax/max_rate"), maxRate);
    ini.setValue(QStringLiteral("fax/ecm"), ecm);
    ini.setValue(QStringLiteral("fax/dither"), dither);
    ini.sync();
}

std::unique_ptr<QSettings> AppSettings::openDefault()
{
    return std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope,
                                       QStringLiteral(FAXWERK_PRODUCT_NAME), QStringLiteral("faxwerk"));
}

} // namespace faxwerk::gui
