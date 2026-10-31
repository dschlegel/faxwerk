#include "Contacts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <algorithm>
#include <optional>

namespace faxwerk::gui {

namespace {

struct TypeName
{
    Contact::Type type;
    const char *key;
};

constexpr TypeName kTypeNames[] = {
    {Contact::Type::Fax, "fax"},       {Contact::Type::Work, "work"},   {Contact::Type::Home, "home"},
    {Contact::Type::Mobile, "mobile"}, {Contact::Type::Other, "other"},
};

QString typeKey(Contact::Type type)
{
    for (const TypeName &name : kTypeNames) {
        if (name.type == type)
            return QString::fromLatin1(name.key);
    }
    return QStringLiteral("other");
}

Contact::Type typeFromKey(const QString &key)
{
    for (const TypeName &name : kTypeNames) {
        if (key == QLatin1String(name.key))
            return name.type;
    }
    return Contact::Type::Other;
}

// FRITZ!Box number types: fax_work, work, home, mobile, other, intern, memo.
std::optional<Contact::Type> typeFromFritz(const QString &type)
{
    if (type.startsWith(QLatin1String("fax")))
        return Contact::Type::Fax;
    if (type == QLatin1String("work"))
        return Contact::Type::Work;
    if (type == QLatin1String("home"))
        return Contact::Type::Home;
    if (type == QLatin1String("mobile"))
        return Contact::Type::Mobile;
    if (type == QLatin1String("other") || type.isEmpty())
        return Contact::Type::Other;
    return std::nullopt; // intern, memo, ...
}

QList<Contact> readContacts(const QJsonArray &array)
{
    QList<Contact> contacts;
    for (const QJsonValue &value : array)
        contacts.append(Contact::fromJson(value.toObject()));
    return contacts;
}

QJsonArray writeContacts(const QList<Contact> &contacts)
{
    QJsonArray array;
    for (const Contact &contact : contacts)
        array.append(contact.toJson());
    return array;
}

int nameScore(const QString &name, const QString &query)
{
    if (name.startsWith(query, Qt::CaseInsensitive))
        return 3;
    const int at = int(name.indexOf(query, 0, Qt::CaseInsensitive));
    if (at < 0)
        return 0;
    // At the start of a later word ("Berger" in "Praxis Dr. Berger").
    return name.at(at - 1).isLetterOrNumber() ? 1 : 2;
}

} // namespace

QString Contact::typeLabel() const
{
    switch (type) {
    case Type::Fax: return QStringLiteral("Fax");
    case Type::Work: return QStringLiteral("Telefon geschäftlich");
    case Type::Home: return QStringLiteral("Telefon privat");
    case Type::Mobile: return QStringLiteral("Mobil");
    case Type::Other: return QStringLiteral("Sonstige");
    }
    return QString();
}

QJsonObject Contact::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("number"), number);
    o.insert(QStringLiteral("type"), typeKey(type));
    o.insert(QStringLiteral("phonebook"), phonebookId);
    o.insert(QStringLiteral("uniqueid"), uniqueId);
    return o;
}

Contact Contact::fromJson(const QJsonObject &o)
{
    Contact c;
    c.name = o.value(QStringLiteral("name")).toString();
    c.number = o.value(QStringLiteral("number")).toString();
    c.type = typeFromKey(o.value(QStringLiteral("type")).toString(QStringLiteral("fax")));
    c.phonebookId = o.value(QStringLiteral("phonebook")).toInt();
    c.uniqueId = o.value(QStringLiteral("uniqueid")).toString();
    return c;
}

QString comparableNumber(const QString &number)
{
    QString digits;
    bool plus = false;
    for (const QChar c : number) {
        if (c.isDigit())
            digits.append(c);
        else if (c == QLatin1Char('+') && digits.isEmpty())
            plus = true;
    }
    if (plus)
        digits.prepend(QStringLiteral("00"));
    if (digits.startsWith(QLatin1String("0049")))
        digits.replace(0, 4, QStringLiteral("0"));
    return digits;
}

bool parseFritzPhonebook(const QByteArray &xml, int phonebookId, QList<Contact> *contacts, QString *error)
{
    QXmlStreamReader reader(xml);
    bool seenPhonebook = false;
    QString name;
    QString uniqueId;
    QList<Contact> numbers;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const auto element = reader.name();
            if (element == QLatin1String("phonebook")) {
                seenPhonebook = true;
            } else if (element == QLatin1String("contact")) {
                name.clear();
                uniqueId.clear();
                numbers.clear();
            } else if (element == QLatin1String("realName")) {
                name = reader.readElementText().trimmed();
            } else if (element == QLatin1String("uniqueid")) {
                uniqueId = reader.readElementText().trimmed();
            } else if (element == QLatin1String("number")) {
                const QString typeName = reader.attributes().value(QLatin1String("type")).toString();
                const QString number = reader.readElementText().trimmed();
                const auto type = typeFromFritz(typeName);
                // "**" numbers are internal phones.
                if (!type || number.isEmpty() || number.startsWith(QLatin1String("**")))
                    continue;
                Contact contact;
                contact.number = number;
                contact.type = *type;
                numbers.append(contact);
            }
        } else if (reader.isEndElement() && reader.name() == QLatin1String("contact")) {
            const bool hasFax = std::any_of(numbers.cbegin(), numbers.cend(),
                                            [](const Contact &c) { return c.type == Contact::Type::Fax; });
            for (Contact contact : std::as_const(numbers)) {
                if (contact.type == Contact::Type::Mobile || (hasFax && contact.type != Contact::Type::Fax))
                    continue;
                contact.name = name;
                contact.uniqueId = uniqueId;
                contact.phonebookId = phonebookId;
                contacts->append(contact);
            }
        }
    }
    if (reader.hasError() || !seenPhonebook) {
        if (error)
            *error = reader.hasError() ? reader.errorString() : QStringLiteral("no phonebook element");
        return false;
    }
    return true;
}

QByteArray newFritzEntry(const QString &name, const QString &number)
{
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    writer.writeStartDocument();
    writer.writeStartElement(QStringLiteral("contact"));
    writer.writeTextElement(QStringLiteral("category"), QStringLiteral("0"));
    writer.writeStartElement(QStringLiteral("person"));
    writer.writeTextElement(QStringLiteral("realName"), name);
    writer.writeEndElement();
    writer.writeStartElement(QStringLiteral("telephony"));
    writer.writeAttribute(QStringLiteral("nid"), QStringLiteral("1"));
    writer.writeStartElement(QStringLiteral("number"));
    writer.writeAttribute(QStringLiteral("type"), QStringLiteral("fax_work"));
    writer.writeAttribute(QStringLiteral("prio"), QStringLiteral("1"));
    writer.writeAttribute(QStringLiteral("id"), QStringLiteral("0"));
    writer.writeCharacters(number);
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndElement();
    writer.writeEndDocument();
    return xml;
}

QByteArray editFritzEntry(const QByteArray &entry, const QString &name, const QString &oldNumber,
                          const QString &newNumber)
{
    const QString oldKey = comparableNumber(oldNumber);
    QXmlStreamReader reader(entry);
    QByteArray xml;
    QXmlStreamWriter writer(&xml);
    bool replaced = false;
    bool sawTelephony = false;
    bool sawContact = false;
    int numberCount = 0;

    auto writeNewNumber = [&] {
        writer.writeStartElement(QStringLiteral("number"));
        writer.writeAttribute(QStringLiteral("type"), QStringLiteral("fax_work"));
        writer.writeAttribute(QStringLiteral("prio"), QStringLiteral("0"));
        writer.writeAttribute(QStringLiteral("id"), QString::number(numberCount));
        writer.writeCharacters(newNumber);
        writer.writeEndElement();
        replaced = true;
    };

    while (!reader.atEnd()) {
        if (reader.readNext() == QXmlStreamReader::Invalid)
            break;
        if (reader.isStartElement() && reader.name() == QLatin1String("realName")) {
            reader.readElementText();
            writer.writeTextElement(QStringLiteral("realName"), name);
        } else if (reader.isStartElement() && reader.name() == QLatin1String("number")) {
            const QXmlStreamAttributes attributes = reader.attributes();
            const QString number = reader.readElementText();
            ++numberCount;
            writer.writeStartElement(QStringLiteral("number"));
            writer.writeAttributes(attributes);
            if (!replaced && !oldKey.isEmpty() && comparableNumber(number) == oldKey) {
                writer.writeCharacters(newNumber);
                replaced = true;
            } else {
                writer.writeCharacters(number);
            }
            writer.writeEndElement();
        } else if (reader.isEndElement() && reader.name() == QLatin1String("telephony")) {
            sawTelephony = true;
            if (!replaced)
                writeNewNumber();
            writer.writeCurrentToken(reader);
        } else if (reader.isEndElement() && reader.name() == QLatin1String("contact")) {
            sawContact = true;
            if (!sawTelephony) {
                writer.writeStartElement(QStringLiteral("telephony"));
                writeNewNumber();
                writer.writeEndElement();
            }
            writer.writeCurrentToken(reader);
        } else if (!reader.isWhitespace()) {
            writer.writeCurrentToken(reader);
        }
    }
    if (reader.hasError() || !sawContact)
        return QByteArray();
    return xml;
}

QList<Contact> searchContacts(const QList<Contact> &contacts, const QString &query, int limit)
{
    const QString text = query.trimmed();
    if (text.isEmpty())
        return {};
    const bool hasDigit = std::any_of(text.cbegin(), text.cend(), [](QChar c) { return c.isDigit(); });
    const QString digits = hasDigit ? comparableNumber(text) : QString();

    struct Hit
    {
        int score;
        const Contact *contact;
    };
    QList<Hit> hits;
    for (const Contact &contact : contacts) {
        int match = nameScore(contact.name, text);
        if (digits.size() >= 2) {
            const QString number = comparableNumber(contact.number);
            if (number.startsWith(digits))
                match = std::max(match, 3);
            else if (number.contains(digits))
                match = std::max(match, 2);
        }
        if (match == 0)
            continue;
        hits.append({match * 10 + (contact.type == Contact::Type::Fax ? 1 : 0), &contact});
    }
    std::stable_sort(hits.begin(), hits.end(), [](const Hit &a, const Hit &b) {
        if (a.score != b.score)
            return a.score > b.score;
        return QString::localeAwareCompare(a.contact->name, b.contact->name) < 0;
    });

    QList<Contact> result;
    for (const Hit &hit : std::as_const(hits)) {
        if (result.size() >= limit)
            break;
        result.append(*hit.contact);
    }
    return result;
}

ContactStore::ContactStore(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
}

QString ContactStore::defaultPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("contacts.json"));
}

bool ContactStore::load(QString *error)
{
    m_phonebooks.clear();
    QFile file(m_path);
    if (!file.exists()) {
        emit changed();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = parseError.errorString();
        return false;
    }
    const QJsonObject books = doc.object().value(QStringLiteral("phonebooks")).toObject();
    for (auto it = books.constBegin(); it != books.constEnd(); ++it) {
        const QJsonObject book = it.value().toObject();
        Phonebook phonebook;
        phonebook.fetched = QDateTime::fromString(book.value(QStringLiteral("fetched")).toString(), Qt::ISODate);
        phonebook.contacts = readContacts(book.value(QStringLiteral("contacts")).toArray());
        m_phonebooks.insert(it.key(), phonebook);
    }
    emit changed();
    return true;
}

bool ContactStore::save(QString *error) const
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonObject books;
    for (auto it = m_phonebooks.constBegin(); it != m_phonebooks.constEnd(); ++it) {
        QJsonObject book;
        book.insert(QStringLiteral("fetched"), it->fetched.toString(Qt::ISODate));
        book.insert(QStringLiteral("contacts"), writeContacts(it->contacts));
        books.insert(it.key(), book);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("phonebooks"), books);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

void ContactStore::setPhonebook(const QString &locationId, const QList<Contact> &contacts, const QDateTime &fetched)
{
    Phonebook phonebook;
    phonebook.fetched = fetched;
    phonebook.contacts = contacts;
    std::stable_sort(phonebook.contacts.begin(), phonebook.contacts.end(), [](const Contact &a, const Contact &b) {
        return QString::localeAwareCompare(a.name, b.name) < 0;
    });
    m_phonebooks.insert(locationId, phonebook);
    save();
    emit changed();
}

QDateTime ContactStore::phonebookTime(const QString &locationId) const
{
    return m_phonebooks.value(locationId).fetched;
}

void ContactStore::prunePhonebooks(const AppSettings &previous, const AppSettings &current)
{
    bool removed = false;
    const QStringList ids = m_phonebooks.keys();
    for (const QString &id : ids) {
        const Location *now = current.find(id);
        const Location *before = previous.find(id);
        const bool keep = now && now->hasPhonebook() && before && before->server == now->server
                          && before->phonebookUser == now->phonebookUser
                          && before->phonebookPassword == now->phonebookPassword;
        if (!keep) {
            m_phonebooks.remove(id);
            removed = true;
        }
    }
    if (removed) {
        save();
        emit changed();
    }
}

QList<Contact> ContactStore::all(const AppSettings &settings) const
{
    QList<Contact> result;
    QSet<QString> seen;
    for (const Location &location : settings.locations) {
        const auto it = m_phonebooks.constFind(location.id);
        if (it == m_phonebooks.constEnd())
            continue;
        for (Contact contact : it->contacts) {
            const QString key = comparableNumber(contact.number);
            if (key.isEmpty() || seen.contains(key))
                continue;
            seen.insert(key);
            contact.locationId = location.id;
            contact.locationName = location.displayName();
            result.append(contact);
        }
    }
    return result;
}

bool ContactStore::knowsNumber(const QString &number, const AppSettings &settings) const
{
    const QString key = comparableNumber(number);
    if (key.isEmpty())
        return false;
    for (const Contact &contact : all(settings)) {
        if (comparableNumber(contact.number) == key)
            return true;
    }
    return false;
}

} // namespace faxwerk::gui
