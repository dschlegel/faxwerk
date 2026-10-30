#include "SendingPage.h"

#include "Messages.h"
#include "Widgets.h"

#include "FaxJob.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace faxwerk::gui {

namespace {

enum Step { StepDocument, StepRegister, StepDial, StepAnswer, StepPages, StepHangup, StepCount };

QLabel *darkLabel(const QString &text, int pixelSize, int weight, bool muted = false)
{
    auto *label = new QLabel(text);
    if (muted)
        label->setProperty("role", QStringLiteral("onDarkMuted"));
    label->setStyleSheet(QStringLiteral("font-size: %1px; font-weight: %2;").arg(pixelSize).arg(weight));
    label->setWordWrap(true);
    return label;
}

} // namespace

SendingPage::SendingPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("scrollContent"));
    scroll->setWidget(content);
    auto *centering = new QHBoxLayout(content);
    centering->setContentsMargins(28, 36, 28, 40);
    auto *inner = new QWidget;
    inner->setMaximumWidth(1180);
    centering->addWidget(inner);

    m_columns = new QBoxLayout(QBoxLayout::LeftToRight, inner);
    m_columns->setContentsMargins(0, 0, 0, 0);
    m_columns->setSpacing(28);

    // --- Dark progress card -------------------------------------------------------
    auto *card = new QFrame;
    card->setProperty("role", QStringLiteral("darkCard"));
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(36, 34, 36, 36);
    cardLayout->setSpacing(6);
    m_lead = darkLabel(QStringLiteral("Wird gesendet an"), 15, 400, true);
    m_recipient = darkLabel(QString(), 38, 600);
    m_number = darkLabel(QString(), 17, 400, true);
    cardLayout->addWidget(m_lead);
    cardLayout->addWidget(m_recipient);
    cardLayout->addWidget(m_number);
    cardLayout->addStretch(1);
    cardLayout->addSpacing(22);
    m_wave = new WaveformWidget;
    m_wave->setFixedHeight(120);
    cardLayout->addWidget(m_wave);
    cardLayout->addSpacing(22);

    auto *bottom = new QHBoxLayout;
    auto *bottomText = new QVBoxLayout;
    bottomText->setSpacing(8);
    m_headline = darkLabel(QString(), 48, 600);
    m_subline = darkLabel(QString(), 16, 400, true);
    bottomText->addWidget(m_headline);
    bottomText->addWidget(m_subline);
    bottom->addLayout(bottomText, 1);
    m_pageProgress = new PageProgressWidget;
    bottom->addWidget(m_pageProgress, 0, Qt::AlignBottom);
    cardLayout->addLayout(bottom);
    m_columns->addWidget(card, 1);

    // --- Steps and actions ---------------------------------------------------------
    auto *side = new QWidget;
    side->setMinimumWidth(320);
    side->setMaximumWidth(380);
    auto *sideLayout = new QVBoxLayout(side);
    sideLayout->setContentsMargins(0, 0, 0, 0);
    sideLayout->setSpacing(16);

    auto *stepsCard = new QFrame;
    stepsCard->setProperty("role", QStringLiteral("card"));
    auto *stepsLayout = new QVBoxLayout(stepsCard);
    stepsLayout->setContentsMargins(24, 24, 24, 24);
    m_steps = new StepList({QStringLiteral("Dokument vorbereitet"), QStringLiteral("An der FRITZ!Box angemeldet"),
                            QStringLiteral("Nummer gewählt"), QStringLiteral("Faxgerät hat geantwortet"),
                            QStringLiteral("Seiten übertragen"), QStringLiteral("Auflegen und abmelden")});
    stepsLayout->addWidget(m_steps);
    sideLayout->addWidget(stepsCard);

    m_result = new QLabel;
    m_result->setProperty("banner", QStringLiteral("info"));
    m_result->setContentsMargins(16, 12, 16, 12);
    m_result->setWordWrap(true);
    m_result->hide();
    sideLayout->addWidget(m_result);

    m_cancel = new QPushButton(QStringLiteral("Senden abbrechen"));
    sideLayout->addWidget(m_cancel);
    m_cancelNote = new QLabel(QStringLiteral("Faxwerk legt dann sauber auf und meldet sich ab."));
    m_cancelNote->setProperty("role", QStringLiteral("muted"));
    m_cancelNote->setAlignment(Qt::AlignHCenter);
    m_cancelNote->setWordWrap(true);
    sideLayout->addWidget(m_cancelNote);

    m_primary = new QPushButton;
    m_primary->setProperty("role", QStringLiteral("primary"));
    m_primary->hide();
    sideLayout->addWidget(m_primary);
    m_secondary = new QPushButton;
    m_secondary->hide();
    sideLayout->addWidget(m_secondary);
    m_journal = new QPushButton(QStringLiteral("Gesendete Faxe anzeigen"));
    m_journal->setProperty("role", QStringLiteral("link"));
    m_journal->hide();
    sideLayout->addWidget(m_journal, 0, Qt::AlignHCenter);
    sideLayout->addStretch(1);
    m_columns->addWidget(side, 0);

    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, &SendingPage::updateElapsed);
    connect(m_cancel, &QPushButton::clicked, this, [this] {
        m_cancel->setEnabled(false);
        m_cancel->setText(QStringLiteral("Wird abgebrochen …"));
        emit cancelRequested();
    });
    connect(m_primary, &QPushButton::clicked, this, [this] {
        if (m_success)
            emit newFaxRequested();
        else
            emit retryRequested();
    });
    connect(m_secondary, &QPushButton::clicked, this, [this] {
        if (m_success)
            emit journalRequested();
        else
            emit editRequested();
    });
    connect(m_journal, &QPushButton::clicked, this, &SendingPage::journalRequested);
}

void SendingPage::attach(FaxJob *job, const QString &recipient, const QString &number, int pages)
{
    m_pages = pages;
    m_done = 0;
    m_activeStep = StepDocument;
    m_success = false;

    m_lead->setText(QStringLiteral("Wird gesendet an"));
    m_recipient->setText(recipient);
    m_number->setText(number);
    m_number->setVisible(recipient != number);
    m_wave->setProgress(0);
    m_wave->setActive(true);
    m_headline->setText(QStringLiteral("Vorbereiten"));
    m_pageProgress->setPages(pages, 0, false);
    m_steps->reset();
    m_steps->setState(StepDocument, StepList::State::Active);
    m_steps->setDetail(StepDocument, pagesText(pages));

    m_result->hide();
    m_cancel->setEnabled(true);
    m_cancel->setText(QStringLiteral("Senden abbrechen"));
    m_cancel->show();
    m_cancelNote->show();
    m_primary->hide();
    m_secondary->hide();
    m_journal->hide();

    m_elapsed.start();
    m_clock.start();
    updateElapsed();

    connect(job, &FaxJob::phaseChanged, this, &SendingPage::onPhase);
    connect(job, &FaxJob::pageTransferred, this, &SendingPage::onPages);
    connect(job, &FaxJob::negotiated, this, [this](int bitRate, bool ecm, const QString &) {
        m_steps->setDetail(StepAnswer, QStringLiteral("%1 bit/s, %2")
                                           .arg(bitRate)
                                           .arg(ecm ? QStringLiteral("mit Fehlerkorrektur")
                                                    : QStringLiteral("ohne Fehlerkorrektur")));
    });
    connect(job, &FaxJob::finished, this, &SendingPage::onFinished);
}

void SendingPage::onPhase(FaxPhase phase)
{
    int step = StepDocument;
    switch (phase) {
    case FaxPhase::Rendering: step = StepDocument; break;
    case FaxPhase::Registering: step = StepRegister; break;
    case FaxPhase::Dialing: step = StepDial; break;
    case FaxPhase::Connected: step = StepAnswer; break;
    case FaxPhase::Transferring: step = StepPages; break;
    case FaxPhase::HangingUp: step = StepHangup; break;
    }
    for (int i = 0; i < step; ++i)
        m_steps->setState(i, StepList::State::Done);
    m_steps->setState(step, StepList::State::Active);
    m_activeStep = step;

    switch (phase) {
    case FaxPhase::Rendering: m_headline->setText(QStringLiteral("Vorbereiten")); break;
    case FaxPhase::Registering: m_headline->setText(QStringLiteral("Anmelden")); break;
    case FaxPhase::Dialing:
        m_headline->setText(QStringLiteral("Es klingelt"));
        m_steps->setDetail(StepDial, QStringLiteral("Warten, bis die Gegenstelle abnimmt"));
        break;
    case FaxPhase::Connected:
        m_headline->setText(QStringLiteral("Verbunden"));
        m_steps->setDetail(StepDial, QStringLiteral("Gegenstelle hat abgenommen"));
        break;
    case FaxPhase::Transferring:
        onPages(m_done, m_pages);
        break;
    case FaxPhase::HangingUp: m_headline->setText(QStringLiteral("Auflegen")); break;
    }
}

void SendingPage::onPages(int done, int total)
{
    if (total > 0)
        m_pages = total;
    m_done = done;
    const bool sending = done < m_pages;
    m_headline->setText(sending ? QStringLiteral("Seite %1 von %2").arg(done + 1).arg(m_pages)
                                : QStringLiteral("Alle Seiten gesendet"));
    m_steps->setDetail(StepPages, QStringLiteral("%1 von %2").arg(done).arg(pagesText(m_pages)));
    m_pageProgress->setPages(m_pages, done, sending);
    m_wave->setProgress(m_pages > 0 ? double(done) / m_pages : 0);
}

void SendingPage::onFinished(const FaxOutcome &outcome)
{
    m_clock.stop();
    m_wave->setActive(false);
    m_cancel->hide();
    m_cancelNote->hide();

    m_success = outcome.status == FaxStatus::Success;
    const OutcomeText text =
        describeOutcome(outcome.status, outcome.sipStatus, outcome.fax.pagesTransferred, m_pages);
    const int transferred = m_success ? m_pages : outcome.fax.pagesTransferred;
    m_pageProgress->setPages(m_pages, transferred, false);
    m_wave->setProgress(m_pages > 0 ? double(transferred) / m_pages : 0);

    if (m_success) {
        for (int i = 0; i < m_steps->count(); ++i)
            m_steps->setState(i, StepList::State::Done);
        m_steps->setDetail(StepPages, pagesText(m_pages));
        m_lead->setText(QStringLiteral("Gesendet an"));
        m_headline->setText(QStringLiteral("Zugestellt"));
        m_subline->setText(QStringLiteral("In %1 übertragen").arg(formatDuration(m_elapsed.elapsed())));
        m_primary->setText(QStringLiteral("Neues Fax"));
        m_secondary->setText(QStringLiteral("Gesendete Faxe anzeigen"));
    } else {
        m_steps->setState(m_activeStep, StepList::State::Failed);
        m_lead->setText(QStringLiteral("Nicht gesendet an"));
        m_headline->setText(text.title);
        m_subline->setText(QString());
        setStyleProperty(m_result, "banner",
                         text.severity == Severity::Warning ? QStringLiteral("info") : QStringLiteral("fail"));
        m_result->setText(text.detail);
        m_result->setToolTip(outcome.message);
        m_result->show();
        m_primary->setText(QStringLiteral("Erneut senden"));
        m_secondary->setText(QStringLiteral("Fax bearbeiten"));
        m_journal->show();
    }
    m_primary->show();
    m_secondary->show();
}

void SendingPage::updateElapsed()
{
    m_subline->setText(QStringLiteral("Läuft seit %1").arg(formatDuration(m_elapsed.elapsed())));
}

void SendingPage::resizeEvent(QResizeEvent *event)
{
    m_columns->setDirection(width() < 860 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    QWidget::resizeEvent(event);
}

} // namespace faxwerk::gui
