#pragma once

#include "Contacts.h"

#include <QList>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QUrl>



namespace faxwerk::gui {

// The phonebooks of a FRITZ!Box over TR-064 (service X_AVM-DE_OnTel).
//
// Reading: GetPhonebookList, then GetPhonebook for each ID, then the XML file
// each one points to. Writing: GetPhonebookEntryUID, then the changed entry
// with SetPhonebookEntryUID, so that the entry's other numbers and details
// stay. TR-064 uses HTTP digest authentication with a FRITZ!Box user; the IP
// phone credentials don't work for it.
class FritzPhonebook : public QObject
{
    Q_OBJECT

public:
    explicit FritzPhonebook(QObject *parent = nullptr);
    ~FritzPhonebook() override;

    static constexpr quint16 kDefaultPort = 49000;

    void setBox(const QString &host, const QString &user, const QString &password, quint16 port = kDefaultPort,
                int timeoutMs = 15000);

    // Each starts a request and cancels one that is still running.
    void fetch();
    // Writes name and number into the entry original came from, replacing
    // original's number there. An original without uniqueId adds a new entry
    // to phonebook original.phonebookId.
    void saveEntry(const Contact &original, const QString &name, const QString &number);
    void cancel();
    bool isRunning() const { return m_reply != nullptr; }

signals:
    // error is a sentence for the user when ok is false.
    void fetched(bool ok, const QList<faxwerk::gui::Contact> &contacts, const QString &error);
    void saved(bool ok, const QString &error);

private:
    enum class Mode { Fetch, Save };

    void start(Mode mode);
    void soap(const QString &action, const QString &arguments);
    void onReply();
    void handleSoap(const QByteArray &body);
    void nextPhonebook();
    void writeEntry(const QByteArray &entry);
    void fail(const QString &error);

    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QUrl m_base;
    QString m_user;
    QString m_password;
    int m_timeoutMs = 15000;
    int m_authAttempts = 0;
    Mode m_mode = Mode::Fetch;
    QString m_action;
    QList<int> m_pending;
    int m_phonebookId = 0;
    QList<Contact> m_contacts;
    Contact m_original;
    QString m_name;
    QString m_number;
};

} // namespace faxwerk::gui
