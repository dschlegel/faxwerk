#pragma once

#include "AppSettings.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

namespace faxwerk::gui {

// A name with one number from a FRITZ!Box phonebook. A person with several
// numbers there becomes several contacts.
struct Contact
{
    enum class Type { Fax, Work, Home, Mobile, Other };

    QString name;
    QString number;
    Type type = Type::Fax;
    int phonebookId = 0;  // the router's phonebook (0 = "Telefonbuch")
    QString uniqueId;     // the entry in it; empty for a new entry
    QString locationId;   // the router; set by ContactStore::all()
    QString locationName;

    QString typeLabel() const;

    bool operator==(const Contact &other) const
    {
        return name == other.name && number == other.number && type == other.type
               && phonebookId == other.phonebookId && uniqueId == other.uniqueId && locationId == other.locationId;
    }

    QJsonObject toJson() const;
    static Contact fromJson(const QJsonObject &object);
};

// The digits of a number for comparing, with +49 / 0049 as a leading 0.
QString comparableNumber(const QString &number);

// Reads the XML a FRITZ!Box exports a phonebook as. A person with fax numbers
// yields only those; otherwise their landline and other numbers. Mobile and
// internal numbers are left out. Returns false for XML that isn't a phonebook.
bool parseFritzPhonebook(const QByteArray &xml, int phonebookId, QList<Contact> *contacts,
                         QString *error = nullptr);

// Phonebook entry XML (as SetPhonebookEntryUID takes it) for a new person
// with one fax number.
QByteArray newFritzEntry(const QString &name, const QString &number);

// Changes an entry read with GetPhonebookEntryUID: the name, and the number
// equal to oldNumber (or a new fax number if there is none). Everything else
// in the entry stays as it is. Returns an empty array for unreadable XML.
QByteArray editFritzEntry(const QByteArray &entry, const QString &name, const QString &oldNumber,
                          const QString &newNumber);

// Contacts matching what the user typed (part of the name, or digits of the
// number), best match first: names starting with the text, then fax numbers.
QList<Contact> searchContacts(const QList<Contact> &contacts, const QString &query, int limit);

// The phonebooks last read from the routers, kept as JSON in the app data
// directory so that they are there without a connection, too.
class ContactStore : public QObject
{
    Q_OBJECT

public:
    explicit ContactStore(const QString &path, QObject *parent = nullptr);

    static QString defaultPath();

    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;

    void setPhonebook(const QString &locationId, const QList<Contact> &contacts, const QDateTime &fetched);
    QDateTime phonebookTime(const QString &locationId) const;
    // Forgets the phonebooks of locations that are gone or have no phonebook
    // access, and those whose router or access changed since they were read.
    void prunePhonebooks(const AppSettings &previous, const AppSettings &current);

    // All phonebooks in location order. A number appears once.
    QList<Contact> all(const AppSettings &settings) const;
    bool knowsNumber(const QString &number, const AppSettings &settings) const;

signals:
    void changed();

private:
    struct Phonebook
    {
        QDateTime fetched;
        QList<Contact> contacts;
    };

    QString m_path;
    QHash<QString, Phonebook> m_phonebooks;
};

} // namespace faxwerk::gui
