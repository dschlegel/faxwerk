#pragma once

#include "AppSettings.h"
#include "Contacts.h"
#include "Journal.h"

#include <QImage>
#include <QList>
#include <QStringList>
#include <QWidget>

class QBoxLayout;
class QCheckBox;
class QComboBox;
class QCompleter;
class QStandardItemModel;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace faxwerk::gui {

// What the user wants to send.
struct SendRequest
{
    QString destination; // as typed
    QString recipientName;
    QStringList documents;
    bool dither = false;
    int pages = 0;
    QString locationId; // empty: pick the reachable location
};

// Screen A: documents with a fax preview on the left, recipient and the send
// button on the right.
class ComposePage : public QWidget
{
    Q_OBJECT

public:
    explicit ComposePage(QWidget *parent = nullptr);

    void addDocuments(const QStringList &files);
    void load(const SendRequest &request);
    // Fills in the recipient and keeps the documents.
    void setRecipient(const QString &name, const QString &number);
    void clear();

    void setRecentRecipients(const QList<Recipient> &recipients);
    // Suggested while typing a name or number. canSave: offer to save an
    // unknown number in the FRITZ!Box phonebook.
    void setContacts(const QList<Contact> &contacts, bool canSave);
    void setSenderSummary(const QString &summary);
    // Offers "automatic" plus every location; hidden with one location.
    void setLocations(const QList<Location> &locations, const QString &selectedId);
    // Shown below the selection while "automatic" is chosen, e.g. the
    // location found last. Empty hides it.
    void setAutoLocationHint(const QString &hint);
    QString selectedLocationId() const;
    void setDitherDefault(bool dither);

signals:
    void sendRequested(const faxwerk::gui::SendRequest &request);
    void settingsRequested();
    void locationChanged(const QString &locationId);
    void saveContactRequested(const QString &name, const QString &number);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void chooseDocuments();
    void renderPreview();
    void updateSendButton();
    void suggest(QCompleter *completer, const QString &text);
    void updateSaveContact();

    QStringList m_documents;
    QList<QImage> m_thumbnails;

    QBoxLayout *m_columns = nullptr;
    QHBoxLayout *m_pagesRow = nullptr;
    QPushButton *m_addTile = nullptr;
    QLabel *m_fileTitle = nullptr;
    QLabel *m_fileDetail = nullptr;
    QLabel *m_error = nullptr;
    QPushButton *m_removeAll = nullptr;
    QCheckBox *m_dither = nullptr;
    QWidget *m_locationRow = nullptr;
    QComboBox *m_location = nullptr;
    QLabel *m_locationHint = nullptr;
    QLineEdit *m_destination = nullptr;
    QLineEdit *m_name = nullptr;
    QList<Contact> m_contacts;
    bool m_canSaveContacts = false;
    QStandardItemModel *m_suggestions = nullptr;
    QCompleter *m_numberCompleter = nullptr;
    QCompleter *m_nameCompleter = nullptr;
    QPushButton *m_saveContact = nullptr;
    QLabel *m_recentTitle = nullptr;
    QVBoxLayout *m_recentList = nullptr;
    QToolButton *m_disclosure = nullptr;
    QWidget *m_senderBox = nullptr;
    QLabel *m_senderSummary = nullptr;
    QPushButton *m_send = nullptr;
    QWidget *m_sendCard = nullptr;
};

} // namespace faxwerk::gui
