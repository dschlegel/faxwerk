#pragma once

#include "AppSettings.h"
#include "Contacts.h"

#include <QTimer>
#include <QWidget>

#include <memory>

class QBoxLayout;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace faxwerk {
class SipUserAgent;
}

namespace faxwerk::gui {

class FritzPhonebook;

// Screen D, in two modes:
//  - first run: a three-step assistant (Weiter/Zurück). Step 1 edits the
//    locations (router credentials with a registration check, sender
//    details), step 2 the optional FRITZ!Box user for the phonebook, step 3
//    the transfer options.
//  - later: the settings, the same three parts as tabs, saved from any of
//    them.
class SetupPage : public QWidget
{
    Q_OBJECT

public:
    explicit SetupPage(QWidget *parent = nullptr);
    ~SetupPage() override;

    void load(const AppSettings &settings, bool firstRun);

signals:
    void saved(const faxwerk::gui::AppSettings &settings);
    void connectionChecked(bool ok, const QString &locationId);
    void cancelled();
    // The phonebook check read the phonebooks of a location.
    void phonebookRead(const QString &locationId, const QList<faxwerk::gui::Contact> &contacts);

private:
    void showStep(int step);
    // Leaves the current step, keeping its input. False if the locations are
    // incomplete (the form then shows what is missing).
    bool leaveStep(int target);
    void prepareBookStep();
    void save();
    void storeForm();
    void loadForm(int index);
    void refreshLocationBox();
    void addLocation();
    void removeLocation();
    bool validateLocations();
    void checkConnection();
    void finishCheck(bool ok, const QString &message);
    void updateButtons();
    void storeBookForm();
    void loadBookForm(int index);
    void checkPhonebook();
    AppSettings collect();

    AppSettings m_base;
    QList<Location> m_locations;
    int m_current = -1;
    std::unique_ptr<SipUserAgent> m_ua;
    QString m_checkingId;
    QTimer m_checkTimeout;

    bool m_settingsMode = false;
    QPushButton *m_stepPills[3] = {};
    QBoxLayout *m_pillLayout = nullptr;
    QPushButton *m_bookBack = nullptr;
    QPushButton *m_bookNext = nullptr;
    QPushButton *m_transferBack = nullptr;
    QPushButton *m_save = nullptr;
    QLabel *m_helpTitle = nullptr;
    QLabel *m_helpText = nullptr;
    QStackedWidget *m_steps = nullptr;

    QComboBox *m_locationBox = nullptr;
    QPushButton *m_addLocation = nullptr;
    QPushButton *m_removeLocation = nullptr;
    QLineEdit *m_name = nullptr;
    QLineEdit *m_server = nullptr;
    QLineEdit *m_user = nullptr;
    QLineEdit *m_password = nullptr;
    QLineEdit *m_header = nullptr;
    QLineEdit *m_ident = nullptr;
    QLabel *m_checkResult = nullptr;
    QPushButton *m_check = nullptr;
    QPushButton *m_next = nullptr;
    QPushButton *m_cancel = nullptr;

    int m_bookCurrent = -1;
    FritzPhonebook *m_phonebook = nullptr;
    QString m_bookCheckingId;
    QWidget *m_bookLocationRow = nullptr;
    QComboBox *m_bookLocation = nullptr;
    QLineEdit *m_bookUser = nullptr;
    QLineEdit *m_bookPassword = nullptr;
    QLabel *m_bookResult = nullptr;
    QPushButton *m_bookCheck = nullptr;

    QCheckBox *m_autoSelect = nullptr;
    QComboBox *m_rate = nullptr;
    QCheckBox *m_ecm = nullptr;
    QCheckBox *m_dither = nullptr;
};

} // namespace faxwerk::gui
