#pragma once

#include "Journal.h"

#include <QWidget>

class QBoxLayout;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;

namespace faxwerk::gui {

// Screen C: list of sent faxes with the details of the selected one.
class JournalPage : public QWidget
{
    Q_OBJECT

public:
    explicit JournalPage(Journal *journal, QWidget *parent = nullptr);

    void selectEntry(const QString &id);

signals:
    void resendRequested(const faxwerk::gui::JournalEntry &entry);
    void newFaxRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void rebuild();
    void showDetails(const QString &id);
    void saveReport();

    Journal *m_journal;
    QString m_selectedId;

    QBoxLayout *m_columns = nullptr;
    QStackedWidget *m_stack = nullptr;
    QLineEdit *m_search = nullptr;
    QListWidget *m_list = nullptr;
    QWidget *m_detailCard = nullptr;
    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailPill = nullptr;
    QLabel *m_detailText = nullptr;
    QGridLayout *m_facts = nullptr;
    QPushButton *m_resend = nullptr;
    QPushButton *m_report = nullptr;
    QPushButton *m_remove = nullptr;
};

} // namespace faxwerk::gui
