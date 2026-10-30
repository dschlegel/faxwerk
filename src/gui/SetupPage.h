#pragma once

#include "AppSettings.h"

#include <QTimer>
#include <QWidget>

#include <memory>

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

// Screen D: two-step assistant. Step 1 connects to the router (with a
// registration check), step 2 sets the sender details.
class SetupPage : public QWidget
{
    Q_OBJECT

public:
    explicit SetupPage(QWidget *parent = nullptr);
    ~SetupPage() override;

    void load(const AppSettings &settings, bool firstRun);

signals:
    void saved(const faxwerk::gui::AppSettings &settings);
    void connectionChecked(bool ok, const QString &server);
    void cancelled();

private:
    void showStep(int step);
    void checkConnection();
    void finishCheck(bool ok, const QString &message);
    void updateButtons();
    AppSettings collect() const;

    AppSettings m_base;
    std::unique_ptr<SipUserAgent> m_ua;
    QTimer m_checkTimeout;

    QPushButton *m_stepPills[2] = {};
    QLabel *m_helpTitle = nullptr;
    QLabel *m_helpText = nullptr;
    QStackedWidget *m_steps = nullptr;

    QLineEdit *m_server = nullptr;
    QLineEdit *m_user = nullptr;
    QLineEdit *m_password = nullptr;
    QLabel *m_checkResult = nullptr;
    QPushButton *m_check = nullptr;
    QPushButton *m_next = nullptr;
    QPushButton *m_cancel = nullptr;

    QLineEdit *m_header = nullptr;
    QLineEdit *m_ident = nullptr;
    QComboBox *m_rate = nullptr;
    QCheckBox *m_ecm = nullptr;
    QCheckBox *m_dither = nullptr;
};

} // namespace faxwerk::gui
