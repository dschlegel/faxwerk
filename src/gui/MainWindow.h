#pragma once

#include "AppSettings.h"
#include "ComposePage.h"
#include "Journal.h"

#include <QElapsedTimer>
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
    enum class Connection { NotConfigured, Configured, Connected, Failed };
    enum Page { ComposeIndex, SendingIndex, JournalIndex, SetupIndex };

    void showPage(Page page);
    void setConnection(Connection state);
    void applySettings(const AppSettings &settings);
    void startSend(const SendRequest &request);
    void onJobFinished();
    QString senderSummary() const;

    std::unique_ptr<QSettings> m_ini;
    AppSettings m_settings;
    Journal m_journal;
    Connection m_connection = Connection::NotConfigured;

    QPointer<FaxJob> m_job;
    SendRequest m_lastRequest;
    QElapsedTimer m_jobTimer;
    bool m_closeAfterJob = false;

    QLabel *m_productName = nullptr;
    QLabel *m_statusChip = nullptr;
    QButtonGroup *m_nav = nullptr;
    QPushButton *m_navButtons[3] = {};
    QStackedWidget *m_stack = nullptr;
    ComposePage *m_compose = nullptr;
    SendingPage *m_sending = nullptr;
    JournalPage *m_journalPage = nullptr;
    SetupPage *m_setup = nullptr;
};

} // namespace faxwerk::gui
