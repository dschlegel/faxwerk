#pragma once

#include "AppSettings.h"
#include "ComposePage.h"
#include "Contacts.h"
#include "FritzPhonebook.h"
#include "Journal.h"
#include "LocationProbe.h"
#include "Messages.h"

#include <QElapsedTimer>
#include <QHash>
#include <QMainWindow>
#include <QPointer>

class QButtonGroup;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace faxwerk {
class FaxJob;
}

namespace faxwerk::gui {

class ContactsPage;
class JournalPage;
class SendingPage;
class SetupPage;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // Adds PDFs given on the command line or opened with the app.
    void openDocuments(const QStringList &files);

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    enum class Connection { NotConfigured, Searching, Connected, Unreachable, Failed };
    enum Page { ComposeIndex, SendingIndex, JournalIndex, ContactsIndex, SetupIndex };

    void showPage(Page page);
    void updateStatusChip();
    void setConnection(Connection state, const QString &locationId = QString());
    void applySettings(const AppSettings &settings);
    void refreshStatus();
    QList<Location> probeCandidates() const;
    void startSend(const SendRequest &request);
    void startJob(const Location &location);
    void finishWithoutJob(FaxStatus status, const QString &message, const OutcomeText &text);
    void onJobFinished();
    void addJournalEntry(const FaxOutcome &outcome, const QString &location);
    bool busy() const;
    QString senderSummary(const QString &locationId) const;
    // The router new phonebook entries go to: the reachable one if it has
    // phonebook access, otherwise the first that has.
    const Location *phonebookTarget() const;
    void updateContacts();
    void fetchPhonebook(const QString &locationId, bool force);
    void savePhonebookEntry(const Contact &original, const QString &name, const QString &number);

    std::unique_ptr<QSettings> m_ini;
    AppSettings m_settings;
    Journal m_journal;
    ContactStore m_contacts;
    FritzPhonebook m_phonebook;
    enum class PhonebookTask { None, Fetch, Save } m_phonebookTask = PhonebookTask::None;
    QString m_phonebookLocationId; // router of the running phonebook request
    QHash<QString, QList<Contact>> m_checkedPhonebooks; // read in the setup, stored when it is saved
    Connection m_connection = Connection::NotConfigured;
    QString m_connectedId; // location of the last successful probe or send

    // Background check for the status display, and the check before a send.
    LocationProbe m_statusProbe;
    LocationProbe m_sendProbe;
    QElapsedTimer m_lastStatusProbe;

    QPointer<FaxJob> m_job;
    Location m_jobLocation;
    SendRequest m_lastRequest;
    QElapsedTimer m_jobTimer;
    bool m_closeAfterJob = false;

    QLabel *m_productName = nullptr;
    QPushButton *m_statusChip = nullptr;
    QString m_statusText; // the chip shows only its dot in narrow windows
    QButtonGroup *m_nav = nullptr;
    QPushButton *m_navButtons[4] = {};
    QStackedWidget *m_stack = nullptr;
    ComposePage *m_compose = nullptr;
    SendingPage *m_sending = nullptr;
    JournalPage *m_journalPage = nullptr;
    ContactsPage *m_contactsPage = nullptr;
    SetupPage *m_setup = nullptr;
};

} // namespace faxwerk::gui
