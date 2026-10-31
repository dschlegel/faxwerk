#include "Contacts.h"
#include "FritzPhonebook.h"

#include "sip/DigestAuth.h"

#include <QHash>
#include <QMap>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>

using namespace faxwerk;
using namespace faxwerk::gui;

namespace {

const QByteArray kPhonebook0 = R"(<?xml version="1.0" encoding="utf-8"?>
<phonebooks><phonebook owner="1" name="Telefonbuch">
<contact><category>0</category><person><realName>Praxis Dr. Berger, HNO</realName></person>
<telephony nid="3"><number type="fax_work" prio="1" id="0">+49 761 4567890</number>
<number type="work" prio="0" id="1">0761 4567800</number>
<number type="mobile" prio="0" id="2">0171 1111111</number></telephony>
<services/><setup/><uniqueid>7</uniqueid></contact>
<contact><category>0</category><person><realName>Krankenkasse Süd</realName></person>
<telephony nid="3"><number type="work" id="0">0761 999</number><number type="home" id="1">0761 998</number>
<number type="mobile" id="2">0172 2222222</number></telephony><uniqueid>8</uniqueid></contact>
<contact><person><realName>Küche</realName></person>
<telephony><number type="intern" id="0">**610</number></telephony><uniqueid>9</uniqueid></contact>
</phonebook></phonebooks>)";

const QByteArray kPhonebook1 = R"(<?xml version="1.0" encoding="utf-8"?>
<phonebooks><phonebook name="Praxis"><contact><person><realName>Neurologie Dr. Sander</realName></person>
<telephony><number type="fax_work" id="0">0761 8090117</number></telephony><uniqueid>3</uniqueid></contact>
</phonebook></phonebooks>)";

const QByteArray kEntry7 = R"(<?xml version="1.0" encoding="utf-8"?><contact><category>0</category><person><realName>Praxis Dr. Berger, HNO</realName></person><telephony nid="3"><number type="fax_work" prio="1" id="0">+49 761 4567890</number><number type="work" prio="0" id="1">0761 4567800</number><number type="mobile" prio="0" id="2">0171 1111111</number></telephony><services><email classifier="work">info@example.org</email></services><uniqueid>7</uniqueid></contact>)";

QHash<QString, QString> leaves(const QByteArray &xml)
{
    QHash<QString, QString> values;
    QXmlStreamReader reader(xml);
    QString current;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement())
            current = reader.name().toString();
        else if (reader.isCharacters() && !current.isEmpty())
            values[current] += reader.text();
        else if (reader.isEndElement())
            current.clear();
    }
    return values;
}

// A FRITZ!Box TR-064 endpoint on 127.0.0.1: SOAP with digest authentication
// on /upnp/control/x_contact and the phonebook files without.
class MockTr064 : public QObject
{
public:
    QString user = QStringLiteral("faxwerk");
    QString password = QStringLiteral("geheim");
    bool denyWrite = false;
    QMap<int, QByteArray> phonebooks{{0, kPhonebook0}, {1, kPhonebook1}};
    QHash<QString, QByteArray> entries{{QStringLiteral("7"), kEntry7}};
    QStringList actions;
    QByteArray written; // NewPhonebookEntryData of the last SetPhonebookEntryUID
    int writtenTo = -1;

    bool listen()
    {
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = m_server.nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onData(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        return m_server.listen(QHostAddress::LocalHost);
    }
    quint16 port() const { return m_server.serverPort(); }

private:
    void onData(QTcpSocket *socket)
    {
        QByteArray &buffer = m_buffers[socket];
        buffer += socket->readAll();
        for (;;) {
            const qsizetype end = buffer.indexOf("\r\n\r\n");
            if (end < 0)
                return;
            const QList<QByteArray> lines = buffer.left(end).split('\n');
            QHash<QByteArray, QByteArray> headers;
            for (qsizetype i = 1; i < lines.size(); ++i) {
                const qsizetype colon = lines.at(i).indexOf(':');
                if (colon > 0)
                    headers.insert(lines.at(i).left(colon).trimmed().toLower(), lines.at(i).mid(colon + 1).trimmed());
            }
            const qsizetype length = headers.value("content-length").toLongLong();
            if (buffer.size() < end + 4 + length)
                return;
            const QList<QByteArray> requestLine = lines.first().trimmed().split(' ');
            const QByteArray body = buffer.mid(end + 4, length);
            buffer.remove(0, end + 4 + length);
            handle(socket, requestLine.value(0), requestLine.value(1), headers, body);
        }
    }

    void reply(QTcpSocket *socket, int status, const QByteArray &body, const QByteArray &extra = QByteArray())
    {
        const QByteArray reason = status == 200 ? "OK" : status == 401 ? "Unauthorized" : "Internal Server Error";
        socket->write("HTTP/1.1 " + QByteArray::number(status) + ' ' + reason + "\r\n" + extra
                      + "Content-Type: text/xml; charset=\"utf-8\"\r\nContent-Length: " + QByteArray::number(body.size())
                      + "\r\n\r\n" + body);
    }

    bool authorized(const QByteArray &method, const QByteArray &header)
    {
        if (!header.startsWith("Digest "))
            return false;
        QHash<QString, QString> p;
        static const QRegularExpression param(QStringLiteral("(\\w+)=(?:\"([^\"]*)\"|([^,\\s]*))"));
        auto it = param.globalMatch(QString::fromUtf8(header.mid(7)));
        while (it.hasNext()) {
            const auto m = it.next();
            p.insert(m.captured(1), m.captured(2).isNull() ? m.captured(3) : m.captured(2));
        }
        if (p.value(QStringLiteral("username")) != user || p.value(QStringLiteral("nonce")) != QLatin1String("n0nce"))
            return false;
        const QByteArray expected = DigestAuth::computeResponse(
            DigestAuth::Algorithm::Md5, user, QStringLiteral("F!Box SOAP-Auth"), password, QString::fromLatin1(method),
            p.value(QStringLiteral("uri")), p.value(QStringLiteral("nonce")), p.value(QStringLiteral("nc")),
            p.value(QStringLiteral("cnonce")), p.value(QStringLiteral("qop")));
        return expected == p.value(QStringLiteral("response")).toLatin1();
    }

    static QByteArray envelope(const QString &action, const QString &content)
    {
        return QStringLiteral("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                              "<s:Body><u:%1Response xmlns:u=\"urn:dslforum-org:service:X_AVM-DE_OnTel:1\">%2"
                              "</u:%1Response></s:Body></s:Envelope>")
            .arg(action, content)
            .toUtf8();
    }

    static QByteArray fault(int code)
    {
        return QStringLiteral("<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\">"
                              "<s:Body><s:Fault><faultcode>s:Client</faultcode><faultstring>UPnPError</faultstring>"
                              "<detail><UPnPError xmlns=\"urn:dslforum-org:control-1-0\"><errorCode>%1</errorCode>"
                              "<errorDescription>error</errorDescription></UPnPError></detail></s:Fault></s:Body>"
                              "</s:Envelope>")
            .arg(code)
            .toUtf8();
    }

    void handle(QTcpSocket *socket, const QByteArray &method, const QByteArray &path,
                const QHash<QByteArray, QByteArray> &headers, const QByteArray &body)
    {
        if (method == "GET" && path.startsWith("/phonebook.lua?sid=s1d&pbid=")) {
            reply(socket, 200, phonebooks.value(path.mid(path.lastIndexOf('=') + 1).toInt()));
            return;
        }
        if (method != "POST" || path != "/upnp/control/x_contact") {
            reply(socket, 500, QByteArray());
            return;
        }
        if (!authorized(method, headers.value("authorization"))) {
            reply(socket, 401, QByteArray(),
                  "WWW-Authenticate: Digest realm=\"F!Box SOAP-Auth\", nonce=\"n0nce\", algorithm=MD5, qop=\"auth\"\r\n");
            return;
        }
        QByteArray soapAction = headers.value("soapaction");
        soapAction.replace('"', "");
        const QString action = QString::fromLatin1(soapAction.mid(soapAction.indexOf('#') + 1));
        actions.append(action);
        const QHash<QString, QString> args = leaves(body);
        const int id = args.value(QStringLiteral("NewPhonebookID")).toInt();
        if (action == QLatin1String("GetPhonebookList")) {
            QStringList ids;
            for (int key : phonebooks.keys())
                ids.append(QString::number(key));
            reply(socket, 200, envelope(action, QStringLiteral("<NewPhonebookList>%1</NewPhonebookList>")
                                                    .arg(ids.join(QLatin1Char(',')))));
        } else if (action == QLatin1String("GetPhonebook")) {
            // The router names itself fritz.box; the client must use the address it reached it under.
            reply(socket, 200,
                  envelope(action, QStringLiteral("<NewPhonebookName>Telefonbuch</NewPhonebookName>"
                                                  "<NewPhonebookURL>http://fritz.box:%1/phonebook.lua?sid=s1d&amp;"
                                                  "pbid=%2</NewPhonebookURL>")
                                       .arg(port())
                                       .arg(id)));
        } else if (action == QLatin1String("GetPhonebookEntryUID")) {
            const QString uid = args.value(QStringLiteral("NewPhonebookEntryUniqueID"));
            if (!entries.contains(uid)) {
                reply(socket, 500, fault(714));
                return;
            }
            reply(socket, 200, envelope(action, QStringLiteral("<NewPhonebookEntryData>%1</NewPhonebookEntryData>")
                                                    .arg(QString::fromUtf8(entries.value(uid)).toHtmlEscaped())));
        } else if (action == QLatin1String("SetPhonebookEntryUID")) {
            if (denyWrite) {
                reply(socket, 500, fault(606));
                return;
            }
            written = args.value(QStringLiteral("NewPhonebookEntryData")).toUtf8();
            writtenTo = id;
            reply(socket, 200, envelope(action, QStringLiteral("<NewPhonebookEntryUniqueID>12</NewPhonebookEntryUniqueID>")));
        } else {
            reply(socket, 500, fault(401));
        }
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};

} // namespace

class TestContacts : public QObject
{
    Q_OBJECT

private slots:
    void parsesPhonebook()
    {
        QList<Contact> contacts;
        QVERIFY(parseFritzPhonebook(kPhonebook0, 0, &contacts));
        QCOMPARE(contacts.size(), 3);
        // Fax numbers only for a person who has one; no mobile or internal numbers.
        QCOMPARE(contacts.at(0).name, QStringLiteral("Praxis Dr. Berger, HNO"));
        QCOMPARE(contacts.at(0).number, QStringLiteral("+49 761 4567890"));
        QCOMPARE(contacts.at(0).type, Contact::Type::Fax);
        QCOMPARE(contacts.at(0).uniqueId, QStringLiteral("7"));
        QCOMPARE(contacts.at(1).name, QStringLiteral("Krankenkasse Süd"));
        QCOMPARE(contacts.at(1).type, Contact::Type::Work);
        QCOMPARE(contacts.at(2).type, Contact::Type::Home);

        QList<Contact> none;
        QVERIFY(!parseFritzPhonebook("<html>login</html>", 0, &none));
        QVERIFY(!parseFritzPhonebook("<phonebooks><phonebook>", 0, &none));
    }

    void searches()
    {
        QCOMPARE(comparableNumber(QStringLiteral("+49 (761) 456-7890")), QStringLiteral("07614567890"));
        QCOMPARE(comparableNumber(QStringLiteral("0049 761 1")), QStringLiteral("07611"));
        QCOMPARE(comparableNumber(QStringLiteral("+33 1 23")), QStringLiteral("0033123"));

        QList<Contact> contacts;
        QVERIFY(parseFritzPhonebook(kPhonebook0, 0, &contacts));
        QVERIFY(parseFritzPhonebook(kPhonebook1, 1, &contacts));

        auto names = [](const QList<Contact> &list) {
            QStringList result;
            for (const Contact &c : list)
                result.append(c.name);
            return result;
        };
        QCOMPARE(names(searchContacts(contacts, QStringLiteral("berg"), 10)),
                 QStringList{QStringLiteral("Praxis Dr. Berger, HNO")});
        // Typed in national format, stored in international format.
        QCOMPARE(names(searchContacts(contacts, QStringLiteral("0761 456"), 10)),
                 QStringList{QStringLiteral("Praxis Dr. Berger, HNO")});
        // Name at the start before a word inside; fax before phone.
        QCOMPARE(names(searchContacts(contacts, QStringLiteral("kr"), 10)),
                 (QStringList{QStringLiteral("Krankenkasse Süd"), QStringLiteral("Krankenkasse Süd")}));
        QCOMPARE(searchContacts(contacts, QStringLiteral("0761"), 10).first().type, Contact::Type::Fax);
        QCOMPARE(searchContacts(contacts, QStringLiteral("0761"), 2).size(), 2);
        QVERIFY(searchContacts(contacts, QStringLiteral("  "), 10).isEmpty());
    }

    void editsEntryKeepingTheRest()
    {
        const QByteArray edited = editFritzEntry(kEntry7, QStringLiteral("HNO Berger & Partner"),
                                                 QStringLiteral("0761 4567890"), QStringLiteral("0761 4567891"));
        QVERIFY(!edited.isEmpty());
        const QString text = QString::fromUtf8(edited);
        QVERIFY(text.contains(QStringLiteral("<realName>HNO Berger &amp; Partner</realName>")));
        QVERIFY(text.contains(QStringLiteral("<number type=\"fax_work\" prio=\"1\" id=\"0\">0761 4567891</number>")));
        QVERIFY(text.contains(QStringLiteral("0171 1111111")));
        QVERIFY(text.contains(QStringLiteral("info@example.org")));
        QVERIFY(text.contains(QStringLiteral("<uniqueid>7</uniqueid>")));
        QVERIFY(!text.contains(QStringLiteral("4567890")));

        // Number not in the entry: added as a fax number.
        const QString added = QString::fromUtf8(
            editFritzEntry(kEntry7, QStringLiteral("Berger"), QStringLiteral("0761 1"), QStringLiteral("0761 2")));
        QVERIFY(added.contains(QStringLiteral("<number type=\"fax_work\" prio=\"0\" id=\"3\">0761 2</number></telephony>")));
        QVERIFY(added.contains(QStringLiteral("+49 761 4567890")));

        QVERIFY(editFritzEntry("<contact>", QStringLiteral("x"), QString(), QString()).isEmpty());

        QList<Contact> parsed;
        const QByteArray fresh = "<phonebooks><phonebook>"
                                 + newFritzEntry(QStringLiteral("Ärztehaus <Nord>"), QStringLiteral("0761 5"))
                                       .replace("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", "")
                                 + "</phonebook></phonebooks>";
        QVERIFY(parseFritzPhonebook(fresh, 0, &parsed));
        QCOMPARE(parsed.size(), 1);
        QCOMPARE(parsed.first().name, QStringLiteral("Ärztehaus <Nord>"));
        QCOMPARE(parsed.first().type, Contact::Type::Fax);
    }

    void storesPhonebooks()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("contacts.json"));
        AppSettings settings;
        Location a = AppSettings::newLocation(QStringLiteral("Freiburg"));
        a.phonebookPassword = QStringLiteral("x");
        Location b = AppSettings::newLocation(QStringLiteral("Offenburg"));
        b.phonebookPassword = QStringLiteral("y");
        settings.locations = {a, b};

        QList<Contact> book0;
        QList<Contact> book1;
        QVERIFY(parseFritzPhonebook(kPhonebook0, 0, &book0));
        QVERIFY(parseFritzPhonebook(kPhonebook1, 1, &book1));
        {
            ContactStore store(path);
            store.setPhonebook(a.id, book0, QDateTime::currentDateTime());
            store.setPhonebook(b.id, book0 + book1, QDateTime::currentDateTime());
        }
        ContactStore store(path);
        QVERIFY(store.load());
        const QList<Contact> all = store.all(settings);
        // The same numbers in both routers appear once, from the first location.
        QCOMPARE(all.size(), 4);
        QCOMPARE(all.first().locationName, QStringLiteral("Freiburg"));
        QCOMPARE(all.last().name, QStringLiteral("Neurologie Dr. Sander"));
        QCOMPARE(all.last().locationId, b.id);
        QCOMPARE(all.last().phonebookId, 1);
        QCOMPARE(all.last().uniqueId, QStringLiteral("3"));
        QVERIFY(store.knowsNumber(QStringLiteral("0761 456 7890"), settings));
        QVERIFY(!store.knowsNumber(QStringLiteral("0761 1"), settings));

        AppSettings changed = settings;
        changed.locations[1].phonebookPassword = QStringLiteral("other");
        store.prunePhonebooks(settings, changed);
        QVERIFY(store.phonebookTime(a.id).isValid());
        QVERIFY(!store.phonebookTime(b.id).isValid());
        changed.locations.removeFirst();
        store.prunePhonebooks(settings, changed);
        QVERIFY(store.all(settings).isEmpty());
    }

    void fetchesFromRouter()
    {
        MockTr064 box;
        QVERIFY(box.listen());
        FritzPhonebook phonebook;
        phonebook.setBox(QStringLiteral("127.0.0.1"), box.user, box.password, box.port(), 5000);
        QSignalSpy spy(&phonebook, &FritzPhonebook::fetched);
        phonebook.fetch();
        QVERIFY(spy.wait(10000));
        QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(2).toString()));
        const auto contacts = spy.first().at(1).value<QList<Contact>>();
        QCOMPARE(contacts.size(), 4);
        QCOMPARE(contacts.last().phonebookId, 1);
        QCOMPARE(box.actions, (QStringList{QStringLiteral("GetPhonebookList"), QStringLiteral("GetPhonebook"),
                                           QStringLiteral("GetPhonebook")}));
    }

    void rejectsWrongPassword()
    {
        MockTr064 box;
        QVERIFY(box.listen());
        FritzPhonebook phonebook;
        phonebook.setBox(QStringLiteral("127.0.0.1"), box.user, QStringLiteral("falsch"), box.port(), 5000);
        QSignalSpy spy(&phonebook, &FritzPhonebook::fetched);
        phonebook.fetch();
        QVERIFY(spy.wait(10000));
        QVERIFY(!spy.first().at(0).toBool());
        QVERIFY(spy.first().at(2).toString().contains(QStringLiteral("abgelehnt")));
        QVERIFY(box.actions.isEmpty());
    }

    void reportsUnreachableRouter()
    {
        quint16 port = 0;
        {
            QTcpServer closed;
            QVERIFY(closed.listen(QHostAddress::LocalHost));
            port = closed.serverPort();
        }
        FritzPhonebook phonebook;
        phonebook.setBox(QStringLiteral("127.0.0.1"), QStringLiteral("u"), QStringLiteral("p"), port, 3000);
        QSignalSpy spy(&phonebook, &FritzPhonebook::fetched);
        phonebook.fetch();
        QVERIFY(spy.wait(10000));
        QVERIFY(!spy.first().at(0).toBool());
        QVERIFY(spy.first().at(2).toString().contains(QStringLiteral("nicht geantwortet")));
    }

    void changesEntry()
    {
        MockTr064 box;
        QVERIFY(box.listen());
        FritzPhonebook phonebook;
        phonebook.setBox(QStringLiteral("127.0.0.1"), box.user, box.password, box.port(), 5000);
        Contact original;
        original.name = QStringLiteral("Praxis Dr. Berger, HNO");
        original.number = QStringLiteral("+49 761 4567890");
        original.uniqueId = QStringLiteral("7");
        QSignalSpy spy(&phonebook, &FritzPhonebook::saved);
        phonebook.saveEntry(original, QStringLiteral("Praxis Dr. Berger"), QStringLiteral("0761 4567899"));
        QVERIFY(spy.wait(10000));
        QVERIFY2(spy.first().at(0).toBool(), qPrintable(spy.first().at(1).toString()));
        QCOMPARE(box.actions, (QStringList{QStringLiteral("GetPhonebookEntryUID"), QStringLiteral("SetPhonebookEntryUID")}));
        QCOMPARE(box.writtenTo, 0);
        QVERIFY(box.written.contains("<uniqueid>7</uniqueid>"));
        QVERIFY(box.written.contains(">0761 4567899<"));
        QVERIFY(box.written.contains("0171 1111111"));
        QVERIFY(box.written.contains("<realName>Praxis Dr. Berger</realName>"));
    }

    void addsEntry()
    {
        MockTr064 box;
        QVERIFY(box.listen());
        FritzPhonebook phonebook;
        phonebook.setBox(QStringLiteral("127.0.0.1"), box.user, box.password, box.port(), 5000);
        Contact target;
        target.phonebookId = 1;
        QSignalSpy spy(&phonebook, &FritzPhonebook::saved);
        phonebook.saveEntry(target, QStringLiteral("Logopädie & Co"), QStringLiteral("0781 123"));
        QVERIFY(spy.wait(10000));
        QVERIFY(spy.first().at(0).toBool());
        QCOMPARE(box.actions, QStringList{QStringLiteral("SetPhonebookEntryUID")});
        QCOMPARE(box.writtenTo, 1);
        QVERIFY(!box.written.contains("uniqueid"));
        QVERIFY(box.written.contains("<realName>Logopädie &amp; Co</realName>"));
        QVERIFY(box.written.contains("type=\"fax_work\""));
    }

    void reportsMissingWriteRight()
    {
        MockTr064 box;
        box.denyWrite = true;
        QVERIFY(box.listen());
        FritzPhonebook phonebook;
        phonebook.setBox(QStringLiteral("127.0.0.1"), box.user, box.password, box.port(), 5000);
        QSignalSpy spy(&phonebook, &FritzPhonebook::saved);
        phonebook.saveEntry(Contact(), QStringLiteral("A"), QStringLiteral("1"));
        QVERIFY(spy.wait(10000));
        QVERIFY(!spy.first().at(0).toBool());
        QVERIFY(spy.first().at(1).toString().contains(QStringLiteral("darf das Telefonbuch nicht ändern")));

        Contact gone;
        gone.uniqueId = QStringLiteral("99");
        QSignalSpy spy2(&phonebook, &FritzPhonebook::saved);
        phonebook.saveEntry(gone, QStringLiteral("A"), QStringLiteral("1"));
        QVERIFY(spy2.wait(10000));
        QVERIFY(spy2.first().at(1).toString().contains(QStringLiteral("nicht mehr")));
    }
};

QTEST_MAIN(TestContacts)
#include "tst_contacts.moc"
