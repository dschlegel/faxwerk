#include "AppSettings.h"

#include "ProductInfo.h"

#include <QSet>
#include <QUuid>

namespace faxwerk::gui {

namespace {

quint16 portValue(const QVariant &value, quint16 fallback)
{
    const uint port = value.toUInt();
    return port > 0 && port <= 65535 ? quint16(port) : fallback;
}

// Reads passwords from the keychain; after the first error that says there is
// no keychain, it doesn't ask again.
class PasswordReader
{
public:
    explicit PasswordReader(PasswordStore *store) : m_store(store) {}

    QString read(const QString &key)
    {
        if (!m_store)
            return QString();
        QString password;
        QString error;
        const PasswordStore::Result result = m_store->read(key, &password, &error);
        if (result == PasswordStore::Result::Unavailable)
            m_store = nullptr;
        if (result == PasswordStore::Result::Unavailable || result == PasswordStore::Result::Failed)
            qWarning("Cannot read %s from the keychain: %s", qPrintable(key), qPrintable(error));
        return password;
    }

private:
    PasswordStore *m_store;
};

} // namespace

bool AppSettings::isConfigured() const
{
    return !completeLocations().isEmpty();
}

const Location *AppSettings::find(const QString &id) const
{
    for (const Location &location : locations) {
        if (location.id == id)
            return &location;
    }
    return nullptr;
}

QList<Location> AppSettings::completeLocations() const
{
    QList<Location> result;
    for (const Location &location : locations) {
        if (location.isComplete())
            result.append(location);
    }
    return result;
}

SipAccount AppSettings::account(const Location &location) const
{
    SipAccount account;
    account.server = location.server;
    account.serverPort = location.port;
    account.domain = location.domain;
    account.user = location.user;
    account.authUser = location.authUser;
    account.password = location.password;
    account.displayName = displayName;
    account.localPort = localPort;
    account.registerExpires = expires;
    account.userAgent = QStringLiteral("%1/%2").arg(QStringLiteral(FAXWERK_PRODUCT_NAME), QStringLiteral(FAXWERK_VERSION));
    return account;
}

Location AppSettings::newLocation(const QString &name)
{
    Location location;
    location.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    location.name = name;
    return location;
}

AppSettings AppSettings::load(QSettings &ini, PasswordStore *passwords)
{
    AppSettings s;
    PasswordReader keychain(passwords);
    s.displayName = ini.value(QStringLiteral("sip/display_name")).toString();
    s.localPort = portValue(ini.value(QStringLiteral("sip/local_port")), 0);
    s.expires = ini.value(QStringLiteral("sip/expires"), s.expires).toInt();
    s.maxRate = ini.value(QStringLiteral("fax/max_rate"), s.maxRate).toInt() == 14400 ? 14400 : 9600;
    s.ecm = ini.value(QStringLiteral("fax/ecm"), s.ecm).toBool();
    s.dither = ini.value(QStringLiteral("fax/dither"), s.dither).toBool();
    const int timeout = ini.value(QStringLiteral("job/timeout"), s.timeoutSeconds).toInt();
    if (timeout > 0)
        s.timeoutSeconds = timeout;
    s.autoSelect = ini.value(QStringLiteral("gui/auto_select"), true).toBool();
    s.preferredId = ini.value(QStringLiteral("gui/location")).toString();
    s.appearance = ini.value(QStringLiteral("gui/appearance"), s.appearance).toString();

    const int count = ini.beginReadArray(QStringLiteral("locations"));
    for (int i = 0; i < count; ++i) {
        ini.setArrayIndex(i);
        Location location;
        location.id = ini.value(QStringLiteral("id")).toString();
        if (location.id.isEmpty())
            location.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        location.name = ini.value(QStringLiteral("name")).toString();
        location.server = ini.value(QStringLiteral("server"), location.server).toString();
        location.port = portValue(ini.value(QStringLiteral("port")), 5060);
        location.domain = ini.value(QStringLiteral("domain")).toString();
        location.user = ini.value(QStringLiteral("user")).toString();
        location.authUser = ini.value(QStringLiteral("auth_user")).toString();
        location.password = ini.value(QStringLiteral("password")).toString();
        location.ident = ini.value(QStringLiteral("ident")).toString().left(20);
        location.header = ini.value(QStringLiteral("header")).toString().left(50);
        location.phonebookUser = ini.value(QStringLiteral("phonebook_user")).toString();
        location.phonebookPassword = ini.value(QStringLiteral("phonebook_password")).toString();
        if (!location.password.isEmpty() || !location.phonebookPassword.isEmpty())
            s.passwordsInFile = true;
        if (location.password.isEmpty())
            location.password = keychain.read(PasswordStore::sipKey(location.id));
        if (location.phonebookPassword.isEmpty())
            location.phonebookPassword = keychain.read(PasswordStore::phonebookKey(location.id));
        s.locations.append(location);
    }
    ini.endArray();

    // A configuration written by the command line tool or an older version
    // only has [sip] and [fax]: that becomes the first location.
    if (s.locations.isEmpty() && !ini.value(QStringLiteral("sip/user")).toString().isEmpty()) {
        Location location = newLocation(QStringLiteral("Standort 1"));
        location.server = ini.value(QStringLiteral("sip/server"), location.server).toString();
        location.port = portValue(ini.value(QStringLiteral("sip/port")), 5060);
        location.domain = ini.value(QStringLiteral("sip/domain")).toString();
        location.user = ini.value(QStringLiteral("sip/user")).toString();
        location.authUser = ini.value(QStringLiteral("sip/auth_user")).toString();
        location.password = ini.value(QStringLiteral("sip/password")).toString();
        location.ident = ini.value(QStringLiteral("fax/ident")).toString().left(20);
        location.header = ini.value(QStringLiteral("fax/header")).toString().left(50);
        s.passwordsInFile = !location.password.isEmpty();
        s.locations.append(location);
    }
    if (!s.locations.isEmpty() && s.locations.first().password.isEmpty())
        s.locations.first().password = qEnvironmentVariable("FAXWERK_PASSWORD");
    return s;
}

bool AppSettings::save(QSettings &ini, PasswordStore *passwords, QString *error) const
{
    if (error)
        error->clear();
    QSet<QString> oldIds;
    const int oldCount = ini.beginReadArray(QStringLiteral("locations"));
    for (int i = 0; i < oldCount; ++i) {
        ini.setArrayIndex(i);
        oldIds.insert(ini.value(QStringLiteral("id")).toString());
    }
    ini.endArray();

    // Into the keychain first: if that fails, the passwords go into the INI
    // file, so that none is lost.
    bool inKeychain = passwords != nullptr;
    if (passwords) {
        auto store = [&](const QString &key, const QString &password) {
            if (password.isEmpty()) {
                passwords->remove(key);
                return true;
            }
            QString why;
            if (passwords->write(key, password, &why) == PasswordStore::Result::Ok)
                return true;
            if (error)
                *error = why.isEmpty() ? QStringLiteral("no keychain available") : why;
            return false;
        };
        for (const Location &location : locations) {
            oldIds.remove(location.id);
            if (!store(PasswordStore::sipKey(location.id), location.password)
                || !store(PasswordStore::phonebookKey(location.id), location.phonebookPassword)) {
                inKeychain = false;
                break;
            }
        }
        if (inKeychain) {
            for (const QString &id : std::as_const(oldIds)) {
                passwords->remove(PasswordStore::sipKey(id));
                passwords->remove(PasswordStore::phonebookKey(id));
            }
        }
    }

    ini.remove(QStringLiteral("locations"));
    ini.beginWriteArray(QStringLiteral("locations"), int(locations.size()));
    for (int i = 0; i < locations.size(); ++i) {
        const Location &location = locations.at(i);
        ini.setArrayIndex(i);
        ini.setValue(QStringLiteral("id"), location.id);
        ini.setValue(QStringLiteral("name"), location.name);
        ini.setValue(QStringLiteral("server"), location.server);
        ini.setValue(QStringLiteral("port"), location.port);
        ini.setValue(QStringLiteral("domain"), location.domain);
        ini.setValue(QStringLiteral("user"), location.user);
        ini.setValue(QStringLiteral("auth_user"), location.authUser);
        if (!inKeychain)
            ini.setValue(QStringLiteral("password"), location.password);
        ini.setValue(QStringLiteral("ident"), location.ident);
        ini.setValue(QStringLiteral("header"), location.header);
        ini.setValue(QStringLiteral("phonebook_user"), location.phonebookUser);
        if (!inKeychain)
            ini.setValue(QStringLiteral("phonebook_password"), location.phonebookPassword);
    }
    ini.endArray();

    if (!locations.isEmpty()) {
        const Location &first = locations.first();
        ini.setValue(QStringLiteral("sip/server"), first.server);
        ini.setValue(QStringLiteral("sip/port"), first.port);
        ini.setValue(QStringLiteral("sip/user"), first.user);
        // The command line tool finds the password under this location's key.
        ini.setValue(QStringLiteral("sip/location"), first.id);
        if (inKeychain)
            ini.remove(QStringLiteral("sip/password"));
        else
            ini.setValue(QStringLiteral("sip/password"), first.password);
        ini.setValue(QStringLiteral("fax/ident"), first.ident);
        ini.setValue(QStringLiteral("fax/header"), first.header);
        if (first.domain.isEmpty())
            ini.remove(QStringLiteral("sip/domain"));
        else
            ini.setValue(QStringLiteral("sip/domain"), first.domain);
        if (first.authUser.isEmpty())
            ini.remove(QStringLiteral("sip/auth_user"));
        else
            ini.setValue(QStringLiteral("sip/auth_user"), first.authUser);
    }
    ini.setValue(QStringLiteral("fax/max_rate"), maxRate);
    ini.setValue(QStringLiteral("fax/ecm"), ecm);
    ini.setValue(QStringLiteral("fax/dither"), dither);
    ini.setValue(QStringLiteral("gui/auto_select"), autoSelect);
    ini.setValue(QStringLiteral("gui/location"), preferredId);
    ini.setValue(QStringLiteral("gui/appearance"), appearance);
    ini.sync();
    return inKeychain;
}

std::unique_ptr<QSettings> AppSettings::openDefault()
{
    return std::make_unique<QSettings>(QSettings::IniFormat, QSettings::UserScope,
                                       QStringLiteral(FAXWERK_PRODUCT_NAME), QStringLiteral("faxwerk"));
}

} // namespace faxwerk::gui
