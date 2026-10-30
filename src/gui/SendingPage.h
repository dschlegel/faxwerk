#pragma once

#include "transport/FaxTransport.h"

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

class QBoxLayout;
class QLabel;
class QPushButton;

namespace faxwerk {
class FaxJob;
}

namespace faxwerk::gui {

class PageProgressWidget;
class StepList;
class WaveformWidget;

// Screen B: progress of one send, then its result.
class SendingPage : public QWidget
{
    Q_OBJECT

public:
    explicit SendingPage(QWidget *parent = nullptr);

    // Shows the progress of job until it finishes.
    void attach(FaxJob *job, const QString &recipient, const QString &number, int pages);

signals:
    void cancelRequested();
    void newFaxRequested();
    void retryRequested();
    void editRequested();
    void journalRequested();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void onPhase(FaxPhase phase);
    void onPages(int done, int total);
    void onFinished(const FaxOutcome &outcome);
    void updateElapsed();

    int m_pages = 0;
    int m_done = 0;
    int m_activeStep = 0;
    QElapsedTimer m_elapsed;
    QTimer m_clock;

    QBoxLayout *m_columns = nullptr;
    QLabel *m_lead = nullptr;
    QLabel *m_recipient = nullptr;
    QLabel *m_number = nullptr;
    WaveformWidget *m_wave = nullptr;
    QLabel *m_headline = nullptr;
    QLabel *m_subline = nullptr;
    PageProgressWidget *m_pageProgress = nullptr;
    StepList *m_steps = nullptr;
    QLabel *m_result = nullptr;
    QPushButton *m_cancel = nullptr;
    QLabel *m_cancelNote = nullptr;
    QPushButton *m_primary = nullptr;
    QPushButton *m_secondary = nullptr;
    QPushButton *m_journal = nullptr;
    bool m_success = false;
};

} // namespace faxwerk::gui
