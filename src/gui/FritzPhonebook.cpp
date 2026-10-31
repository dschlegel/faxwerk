#include "FritzPhonebook.h"

#include <QAuthenticator>
#include <QHash>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QXmlStreamReader>

namespace faxwerk::gui {

namespace {

const QString kService = QStringLiteral("urn:dslforum-org:service:X_AVM-DE_OnTel:1");
const QString kControlPath = QStringLiteral("/upnp/control/x_contact");
const QString kDownload = QStringLiteral("download");

// Text of every element without child elements, e.g. NewPhonebookURL or
// errorCode in a SOAP fault.
QHash<QString, QString> leafValues(const QByteArray &xml)
{
    QHash<QString, QString> values;
    QXmlStreamReader reader(xml);
    QString current;
    QString text;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            current = reader.name().toString();
            text.clear();
        } else if (reader.isCharacters()) {
            text += reader.text();
        } else if (reader.isEndElement()) {
            if (reader.name() == current)
                values.insert(current, text.trimmed());
            current.clear();
        }
    }
    return values;
}

} // namespace

FritzPhonebook::FritzPhonebook(QObject *parent)
    : QObject(parent)
{
    // Routers are in the LAN or behind the VPN, never behind a proxy.
    m_network.setProxy(QNetworkProxy::NoProxy);
    connect(&m_network, &QNetworkAccessManager::authenticationRequired, this,
            [this](QNetworkReply *reply, QAuthenticator *authenticator) {
                // A second challenge for the same request means the credentials were rejected:
                // leave the authenticator empty so the request fails.
                if (reply != m_reply || ++m_authAttempts > 1)
                    return;
                authenticator->setUser(m_user);
                authenticator->setPassword(m_password);
            });
}

FritzPhonebook::~FritzPhonebook()
{
    cancel();
}

void FritzPhonebook::setBox(const QString &host, const QString &user, const QString &password, quint16 port,
                            int timeoutMs)
{
    cancel();
    m_network.clearAccessCache();
    m_base = QUrl();
    m_base.setScheme(QStringLiteral("http"));
    m_base.setHost(host);
    m_base.setPort(port);
    m_user = user;
    m_password = password;
    m_timeoutMs = timeoutMs;
}

void FritzPhonebook::start(Mode mode)
{
    cancel();
    m_mode = mode;
    m_pending.clear();
    m_contacts.clear();
}

void FritzPhonebook::fetch()
{
    start(Mode::Fetch);
    soap(QStringLiteral("GetPhonebookList"), QString());
}

void FritzPhonebook::saveEntry(const Contact &original, const QString &name, const QString &number)
{
    start(Mode::Save);
    m_original = original;
    m_name = name;
    m_number = number;
    if (original.uniqueId.isEmpty()) {
        writeEntry(newFritzEntry(name, number));
        return;
    }
    soap(QStringLiteral("GetPhonebookEntryUID"),
         QStringLiteral("<NewPhonebookID>%1</NewPhonebookID><NewPhonebookEntryUniqueID>%2</NewPhonebookEntryUniqueID>")
             .arg(original.phonebookId)
             .arg(original.uniqueId.toHtmlEscaped()));
}

void FritzPhonebook::writeEntry(const QByteArray &entry)
{
    soap(QStringLiteral("SetPhonebookEntryUID"),
         QStringLiteral("<NewPhonebookID>%1</NewPhonebookID><NewPhonebookEntryData>%2</NewPhonebookEntryData>")
             .arg(m_original.phonebookId)
             .arg(QString::fromUtf8(entry).toHtmlEscaped()));
}

void FritzPhonebook::cancel()
{
    if (QNetworkReply *reply = m_reply) {
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
}

void FritzPhonebook::soap(const QString &action, const QString &arguments)
{
    QUrl url = m_base;
    url.setPath(kControlPath);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=\"utf-8\""));
    request.setRawHeader("SOAPACTION", QStringLiteral("\"%1#%2\"").arg(kService, action).toUtf8());
    request.setTransferTimeout(m_timeoutMs);
    const QString body = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:%1 xmlns:u=\"%2\">%3</u:%1></s:Body></s:Envelope>")
                             .arg(action, kService, arguments);
    m_action = action;
    m_authAttempts = 0;
    m_reply = m_network.post(request, body.toUtf8());
    connect(m_reply, &QNetworkReply::finished, this, &FritzPhonebook::onReply);
}

void FritzPhonebook::onReply()
{
    QNetworkReply *reply = m_reply;
    if (!reply || sender() != reply)
        return;
    m_reply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const QNetworkReply::NetworkError error = reply->error();

    if (error == QNetworkReply::AuthenticationRequiredError || status == 401) {
        fail(QStringLiteral("Die FRITZ!Box hat Benutzername oder Kennwort für das Telefonbuch abgelehnt."));
        return;
    }
    if (status == 500 && m_action != kDownload) {
        const QString code = leafValues(body).value(QStringLiteral("errorCode"));
        if (code == QLatin1String("606") || code == QLatin1String("401"))
            fail(m_mode == Mode::Save
                     ? QStringLiteral("Der FRITZ!Box-Benutzer darf das Telefonbuch nicht ändern. Geben Sie ihm in "
                                      "der FRITZ!Box das Recht „FRITZ!Box Einstellungen“.")
                     : QStringLiteral("Dem FRITZ!Box-Benutzer fehlt das Recht „Sprachnachrichten, Faxnachrichten, "
                                      "FRITZ!App Fon und Anrufliste“."));
        else if (m_mode == Mode::Save && code == QLatin1String("714"))
            fail(QStringLiteral("Den Eintrag gibt es im Telefonbuch der FRITZ!Box nicht mehr."));
        else
            fail(QStringLiteral("Die FRITZ!Box hat die Anfrage abgelehnt (Fehler %1).")
                     .arg(code.isEmpty() ? QString::number(status) : code));
        return;
    }
    switch (error) {
    case QNetworkReply::NoError:
        break;
    case QNetworkReply::ConnectionRefusedError:
    case QNetworkReply::RemoteHostClosedError:
    case QNetworkReply::HostNotFoundError:
    case QNetworkReply::TimeoutError:
    case QNetworkReply::OperationCanceledError: // transfer timeout
    case QNetworkReply::NetworkSessionFailedError:
    case QNetworkReply::TemporaryNetworkFailureError:
        fail(QStringLiteral("Die FRITZ!Box %1 hat beim Telefonbuch nicht geantwortet. Ist dort „Zugriff für "
                            "Anwendungen zulassen“ eingeschaltet (Heimnetz › Netzwerk › Netzwerkeinstellungen)?")
                 .arg(m_base.host()));
        return;
    default:
        fail(QStringLiteral("Das Telefonbuch konnte nicht gelesen werden: %1").arg(reply->errorString()));
        return;
    }

    if (m_action != kDownload) {
        handleSoap(body);
        return;
    }
    QString parseError;
    if (!parseFritzPhonebook(body, m_phonebookId, &m_contacts, &parseError)) {
        fail(QStringLiteral("Die FRITZ!Box hat ein unlesbares Telefonbuch geliefert (%1).").arg(parseError));
        return;
    }
    nextPhonebook();
}

void FritzPhonebook::handleSoap(const QByteArray &body)
{
    const QHash<QString, QString> values = leafValues(body);
    if (m_action == QLatin1String("GetPhonebookEntryUID")) {
        const QByteArray entry = editFritzEntry(values.value(QStringLiteral("NewPhonebookEntryData")).toUtf8(),
                                                m_name, m_original.number, m_number);
        if (entry.isEmpty()) {
            fail(QStringLiteral("Die FRITZ!Box hat einen unlesbaren Telefonbucheintrag geliefert."));
            return;
        }
        writeEntry(entry);
        return;
    }
    if (m_action == QLatin1String("SetPhonebookEntryUID")) {
        emit saved(true, QString());
        return;
    }
    if (m_action == QLatin1String("GetPhonebookList")) {
        const QStringList ids = values.value(QStringLiteral("NewPhonebookList")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (const QString &id : ids) {
            bool ok = false;
            const int number = id.trimmed().toInt(&ok);
            if (ok)
                m_pending.append(number);
        }
        nextPhonebook();
        return;
    }

    // GetPhonebook: download the file it points to. The URL names the router
    // as it sees itself; use the address we reached it under instead, because
    // "fritz.box" or its LAN address may mean another router behind a VPN.
    QUrl url = m_base.resolved(QUrl(values.value(QStringLiteral("NewPhonebookURL"))));
    if (url.path().isEmpty()) {
        fail(QStringLiteral("Die FRITZ!Box hat keine Adresse für das Telefonbuch genannt."));
        return;
    }
    url.setHost(m_base.host());
    QNetworkRequest request(url);
    request.setTransferTimeout(m_timeoutMs);
    m_action = kDownload;
    m_authAttempts = 0;
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, &FritzPhonebook::onReply);
}

void FritzPhonebook::nextPhonebook()
{
    if (m_pending.isEmpty()) {
        emit fetched(true, m_contacts, QString());
        return;
    }
    m_phonebookId = m_pending.takeFirst();
    soap(QStringLiteral("GetPhonebook"), QStringLiteral("<NewPhonebookID>%1</NewPhonebookID>").arg(m_phonebookId));
}

void FritzPhonebook::fail(const QString &error)
{
    m_pending.clear();
    if (m_mode == Mode::Save)
        emit saved(false, error);
    else
        emit fetched(false, {}, error);
}

} // namespace faxwerk::gui
