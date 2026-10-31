#pragma once

#include "Contacts.h"

#include <QWidget>

class QBoxLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace faxwerk::gui {

// The phonebooks of the routers: search, and an editor that saves new and
// changed entries in the FRITZ!Box.
class ContactsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ContactsPage(QWidget *parent = nullptr);

    // configured: at least one location has phonebook access.
    void setContacts(const QList<Contact> &contacts, bool configured);
    // The router new entries go to; empty when there is none.
    void setTarget(const QString &locationName);
    // Opens the editor for a new entry, e.g. with a number typed for a fax.
    void startNew(const QString &name, const QString &number);
    // Progress line under the list ("Telefonbuch wird gelesen …"); empty hides it.
    void setStatus(const QString &text, bool error = false);
    void showSaveResult(bool ok, const QString &message);

signals:
    void saveRequested(const faxwerk::gui::Contact &original, const QString &name, const QString &number);
    void faxRequested(const QString &name, const QString &number);
    void refreshRequested();
    void settingsRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    void edit(const Contact &contact);
    void updateSaveButton();

    QList<Contact> m_contacts;
    Contact m_editing;
    bool m_isNew = false;
    QString m_target;

    QBoxLayout *m_columns = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_add = nullptr;
    QWidget *m_editor = nullptr;
    QLabel *m_editorTitle = nullptr;
    QLabel *m_editorSource = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_number = nullptr;
    QLabel *m_result = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_fax = nullptr;
};

} // namespace faxwerk::gui
