#include "MainWindow.h"

#include "JournalPage.h"
#include "Messages.h"
#include "SendingPage.h"
#include "SetupPage.h"
#include "Widgets.h"

#include "FaxJob.h"
#include "ProductInfo.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace faxwerk::gui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_ini(AppSettings::openDefault())
    , m_settings(AppSettings::load(*m_ini))
    , m_journal(Journal::defaultPath())
{
    setWindowTitle(QStringLiteral(FAXWERK_PRODUCT_NAME));
    setWindowIcon(appIcon());
    setMinimumSize(720, 560);
    m_journal.load();

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setCentralWidget(central);

    // --- Top bar ---------------------------------------------------------------------
    auto *topBar = new QFrame;
    topBar->setObjectName(QStringLiteral("topBar"));
    auto *bar = new QHBoxLayout(topBar);
    bar->setContentsMargins(24, 12, 24, 12);
    bar->setSpacing(10);
    bar->addWidget(new LogoMark(32));
    m_productName = new QLabel(QStringLiteral(FAXWERK_PRODUCT_NAME));
    m_productName->setObjectName(QStringLiteral("productName"));
    bar->addWidget(m_productName);
    bar->addSpacing(18);

    m_nav = new QButtonGroup(this);
    m_nav->setExclusive(true);
    const QString navLabels[] = {QStringLiteral("Neues Fax"), QStringLiteral("Gesendet"),
                                 QStringLiteral("Einstellungen")};
    for (int i = 0; i < 3; ++i) {
        m_navButtons[i] = new QPushButton(navLabels[i]);
        m_navButtons[i]->setProperty("role", QStringLiteral("nav"));
        m_navButtons[i]->setCheckable(true);
        m_nav->addButton(m_navButtons[i], i);
        bar->addWidget(m_navButtons[i]);
    }
    bar->addStretch(1);
    m_statusChip = new QLabel;
    m_statusChip->setObjectName(QStringLiteral("statusChip"));
    m_statusChip->setTextFormat(Qt::RichText);
    bar->addWidget(m_statusChip);
    layout->addWidget(topBar);

    // --- Pages --------------------------------------------------------------------------
    m_stack = new QStackedWidget;
    m_compose = new ComposePage;
    m_sending = new SendingPage;
    m_journalPage = new JournalPage(&m_journal);
    m_setup = new SetupPage;
    m_stack->insertWidget(ComposeIndex, m_compose);
    m_stack->insertWidget(SendingIndex, m_sending);
    m_stack->insertWidget(JournalIndex, m_journalPage);
    m_stack->insertWidget(SetupIndex, m_setup);
    layout->addWidget(m_stack, 1);

    connect(m_nav, &QButtonGroup::idClicked, this, [this](int id) {
        switch (id) {
        case 0: showPage(ComposeIndex); break;
        case 1: showPage(JournalIndex); break;
        case 2:
            m_setup->load(m_settings, false);
            showPage(SetupIndex);
            break;
        }
    });

    connect(m_compose, &ComposePage::sendRequested, this, &MainWindow::startSend);
    connect(m_compose, &ComposePage::settingsRequested, this, [this] {
        m_setup->load(m_settings, false);
        showPage(SetupIndex);
    });

    connect(m_sending, &SendingPage::cancelRequested, this, [this] {
        if (m_job)
            m_job->abort(QStringLiteral("cancelled by user"));
    });
    connect(m_sending, &SendingPage::newFaxRequested, this, [this] {
        m_compose->clear();
        showPage(ComposeIndex);
    });
    connect(m_sending, &SendingPage::retryRequested, this, [this] { startSend(m_lastRequest); });
    connect(m_sending, &SendingPage::editRequested, this, [this] {
        m_compose->load(m_lastRequest);
        showPage(ComposeIndex);
    });
    connect(m_sending, &SendingPage::journalRequested, this, [this] { showPage(JournalIndex); });

    connect(m_journalPage, &JournalPage::newFaxRequested, this, [this] { showPage(ComposeIndex); });
    connect(m_journalPage, &JournalPage::resendRequested, this, [this](const JournalEntry &entry) {
        QStringList missing;
        for (const QString &doc : entry.documents) {
            if (!QFileInfo::exists(doc))
                missing.append(QFileInfo(doc).fileName());
        }
        SendRequest request;
        request.destination = entry.destination;
        request.recipientName = entry.recipientName;
        request.dither = m_settings.dither;
        for (const QString &doc : entry.documents) {
            if (QFileInfo::exists(doc))
                request.documents.append(doc);
        }
        m_compose->load(request);
        showPage(ComposeIndex);
        if (!missing.isEmpty())
            QMessageBox::information(this, QStringLiteral("Dokument fehlt"),
                                     QStringLiteral("Diese Datei gibt es nicht mehr: %1. Fügen Sie das Dokument "
                                                    "erneut hinzu.")
                                         .arg(missing.join(QStringLiteral(", "))));
    });
    connect(&m_journal, &Journal::changed, this,
            [this] { m_compose->setRecentRecipients(m_journal.recentRecipients(4)); });

    connect(m_setup, &SetupPage::saved, this, [this](const AppSettings &settings) {
        const bool serverChanged = settings.server != m_settings.server || settings.user != m_settings.user
                                   || settings.password != m_settings.password;
        settings.save(*m_ini);
        applySettings(settings);
        if (serverChanged && m_connection != Connection::Connected)
            setConnection(Connection::Configured);
        showPage(ComposeIndex);
    });
    connect(m_setup, &SetupPage::connectionChecked, this, [this](bool ok, const QString &) {
        setConnection(ok ? Connection::Connected : Connection::Failed);
    });
    connect(m_setup, &SetupPage::cancelled, this, [this] { showPage(ComposeIndex); });

    m_compose->setRecentRecipients(m_journal.recentRecipients(4));
    applySettings(m_settings);
    setConnection(m_settings.isConfigured() ? Connection::Configured : Connection::NotConfigured);
    if (m_settings.isConfigured()) {
        showPage(ComposeIndex);
    } else {
        m_setup->load(m_settings, true);
        showPage(SetupIndex);
    }
}

MainWindow::~MainWindow() = default;

void MainWindow::openDocuments(const QStringList &files)
{
    QStringList pdfs;
    for (const QString &file : files) {
        if (QFileInfo(file).isFile())
            pdfs.append(file);
    }
    if (pdfs.isEmpty())
        return;
    m_compose->addDocuments(pdfs);
    if (m_settings.isConfigured() && !m_job)
        showPage(ComposeIndex);
}

void MainWindow::showPage(Page page)
{
    m_stack->setCurrentIndex(page);
    const bool sending = m_job && !m_job->isFinished();
    const bool firstRun = !m_settings.isConfigured();
    for (QPushButton *button : m_navButtons)
        button->setEnabled(!sending && !firstRun);

    QAbstractButton *active = nullptr;
    switch (page) {
    case ComposeIndex: active = m_navButtons[0]; break;
    case JournalIndex: active = m_navButtons[1]; break;
    case SetupIndex: active = m_navButtons[2]; break;
    case SendingIndex: break;
    }
    if (active) {
        active->setChecked(true);
    } else {
        m_nav->setExclusive(false);
        for (QPushButton *button : m_navButtons)
            button->setChecked(false);
        m_nav->setExclusive(true);
    }
}

void MainWindow::setConnection(Connection state)
{
    m_connection = state;
    QString dot;
    QString text;
    switch (state) {
    case Connection::NotConfigured:
        dot = QStringLiteral("#9a4a12");
        text = QStringLiteral("Nicht eingerichtet");
        break;
    case Connection::Configured:
        dot = QStringLiteral("#b9a0c9");
        text = QStringLiteral("%1 eingerichtet").arg(m_settings.server.toHtmlEscaped());
        break;
    case Connection::Connected:
        dot = QStringLiteral("#2f7d5b");
        text = QStringLiteral("Verbunden mit %1").arg(m_settings.server.toHtmlEscaped());
        break;
    case Connection::Failed:
        dot = QStringLiteral("#8e2626");
        text = QStringLiteral("Keine Verbindung zu %1").arg(m_settings.server.toHtmlEscaped());
        break;
    }
    m_statusChip->setText(QStringLiteral("<span style='color:%1'>●</span>&nbsp; %2").arg(dot, text));
}

void MainWindow::applySettings(const AppSettings &settings)
{
    m_settings = settings;
    m_compose->setSenderSummary(senderSummary());
    m_compose->setDitherDefault(settings.dither);
}

QString MainWindow::senderSummary() const
{
    QStringList parts;
    parts.append(m_settings.header.isEmpty() ? QStringLiteral("Ohne Kopfzeile")
                                             : QStringLiteral("Kopfzeile „%1“").arg(m_settings.header));
    if (!m_settings.ident.isEmpty())
        parts.append(QStringLiteral("Kennung %1").arg(m_settings.ident));
    parts.append(QStringLiteral("bis %1 bit/s").arg(m_settings.maxRate));
    parts.append(m_settings.ecm ? QStringLiteral("Fehlerkorrektur an") : QStringLiteral("Fehlerkorrektur aus"));
    return parts.join(QStringLiteral(", "));
}

void MainWindow::startSend(const SendRequest &request)
{
    if (m_job && !m_job->isFinished())
        return;
    if (!m_settings.isConfigured()) {
        m_setup->load(m_settings, true);
        showPage(SetupIndex);
        return;
    }
    m_lastRequest = request;

    FaxJobConfig config;
    config.productName = QStringLiteral(FAXWERK_PRODUCT_NAME);
    config.sip.account = m_settings.account();
    config.destination = normalizeNumber(request.destination);
    config.pdfFiles = request.documents;
    config.fax.stationId = m_settings.ident;
    config.fax.headerInfo = m_settings.header;
    config.fax.maxBitRate = m_settings.maxRate;
    config.fax.ecm = m_settings.ecm;
    config.raster.mode = request.dither ? PdfRasterizer::Mode::Dither : PdfRasterizer::Mode::Threshold;
    config.timeoutSeconds = m_settings.timeoutSeconds;

    auto *job = new FaxJob(config, this);
    m_job = job;
    const QString recipient = request.recipientName.isEmpty() ? request.destination : request.recipientName;
    m_sending->attach(job, recipient, request.destination, request.pages);
    connect(job, &FaxJob::phaseChanged, this, [this](FaxPhase phase) {
        if (phase == FaxPhase::Dialing)
            setConnection(Connection::Connected);
    });
    connect(job, &FaxJob::finished, this, &MainWindow::onJobFinished, Qt::QueuedConnection);
    m_jobTimer.start();
    showPage(SendingIndex);
    job->start();
}

void MainWindow::onJobFinished()
{
    if (!m_job)
        return;
    const FaxOutcome outcome = m_job->outcome();

    JournalEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.destination = m_lastRequest.destination;
    entry.recipientName = m_lastRequest.recipientName;
    entry.documents = m_lastRequest.documents;
    entry.pages = m_lastRequest.pages;
    entry.status = outcome.status;
    entry.sipStatus = outcome.sipStatus;
    entry.message = outcome.message;
    entry.pagesTransferred = outcome.status == FaxStatus::Success ? m_lastRequest.pages : outcome.fax.pagesTransferred;
    entry.bitRate = outcome.fax.bitRate;
    entry.ecm = outcome.fax.ecm;
    entry.remoteIdent = outcome.fax.remoteIdent.trimmed();
    entry.durationMs = m_jobTimer.elapsed();
    if (outcome.status != FaxStatus::InvalidRequest)
        m_journal.add(entry);

    if (outcome.status == FaxStatus::AuthenticationFailed || outcome.status == FaxStatus::NetworkError
        || outcome.status == FaxStatus::RegistrationFailed)
        setConnection(Connection::Failed);

    m_job->deleteLater();
    m_job.clear();
    showPage(SendingIndex);

    if (m_closeAfterJob)
        close();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    // Keep the navigation and the connection state visible in narrow windows.
    m_productName->setVisible(width() >= 900);
    QMainWindow::resizeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!m_job || m_job->isFinished()) {
        event->accept();
        return;
    }
    const auto answer = QMessageBox::question(
        this, QStringLiteral("Fax wird gesendet"),
        QStringLiteral("Das Fax wird gerade gesendet. Abbrechen und %1 beenden?")
            .arg(QStringLiteral(FAXWERK_PRODUCT_NAME)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    event->ignore();
    if (answer == QMessageBox::Yes) {
        // Hang up and sign off cleanly, then close.
        m_closeAfterJob = true;
        m_job->abort(QStringLiteral("application closed"));
    }
}

} // namespace faxwerk::gui
