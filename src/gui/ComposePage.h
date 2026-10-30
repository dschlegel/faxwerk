#pragma once

#include "Journal.h"

#include <QImage>
#include <QList>
#include <QStringList>
#include <QWidget>

class QBoxLayout;
class QCheckBox;
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
    void clear();

    void setRecentRecipients(const QList<Recipient> &recipients);
    void setSenderSummary(const QString &summary);
    void setDitherDefault(bool dither);

signals:
    void sendRequested(const faxwerk::gui::SendRequest &request);
    void settingsRequested();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void chooseDocuments();
    void renderPreview();
    void updateSendButton();

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
    QLineEdit *m_destination = nullptr;
    QLineEdit *m_name = nullptr;
    QLabel *m_recentTitle = nullptr;
    QVBoxLayout *m_recentList = nullptr;
    QToolButton *m_disclosure = nullptr;
    QWidget *m_senderBox = nullptr;
    QLabel *m_senderSummary = nullptr;
    QPushButton *m_send = nullptr;
    QWidget *m_sendCard = nullptr;
};

} // namespace faxwerk::gui
