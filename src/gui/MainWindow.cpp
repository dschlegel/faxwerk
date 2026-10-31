#include "MainWindow.h"

#include "ContactsPage.h"
#include "JournalPage.h"
#include "Messages.h"
#include "SendingPage.h"
#include "SetupPage.h"
#include "Theme.h"
#include "Widgets.h"

#include "FaxJob.h"
#include "ProductInfo.h"

#include <QButtonGroup>
#include <QCloseEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QTime>
#include <QVBoxLayout>

namespace faxwerk::gui {

namespace {

// Look for the reachable location again when the window comes back to the
// front (e.g. after switching the VPN), but not more often than this.
constexpr qint64 kStatusRefreshMs = 30000;
// Read the phonebook of the reachable router again after this time.
constexpr qint64 kPhonebookMaxAgeSecs = 30 * 60;

QIcon dotIcon(const QColor &color)
{
    QPixmap pixmap(20, 20);
    pixmap.setDevicePixelRatio(2);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawEllipse(QRectF(1, 1, 8, 8));
    return QIcon(pixmap);
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_ini(AppSettings::openDefault())
    , m_settings(AppSettings::load(*m_ini))
    , m_journal(Journal::defaultPath())
    , m_contacts(ContactStore::defaultPath())
{
    setWindowTitle(QStringLiteral(FAXWERK_PRODUCT_NAME));
    setWindowIcon(appIcon());
    setMinimumSize(720, 560);
    m_journal.load();
    m_contacts.load();

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
                                 QStringLiteral("Kontakte"), QStringLiteral("Einstellungen")};
    for (int i = 0; i < 4; ++i) {
        m_navButtons[i] = new QPushButton(navLabels[i]);
        m_navButtons[i]->setProperty("role", QStringLiteral("nav"));
        m_navButtons[i]->setCheckable(true);
        m_nav->addButton(m_navButtons[i], i);
        bar->addWidget(m_navButtons[i]);
    }
    bar->addStretch(1);
    m_statusChip = new QPushButton;
    m_statusChip->setObjectName(QStringLiteral("statusChip"));
    bar->addWidget(m_statusChip);
    layout->addWidget(topBar);

    // --- Pages --------------------------------------------------------------------------
    m_stack = new QStackedWidget;
    m_compose = new ComposePage;
    m_sending = new SendingPage;
    m_journalPage = new JournalPage(&m_journal);
    m_contactsPage = new ContactsPage;
    m_setup = new SetupPage;
    m_stack->insertWidget(ComposeIndex, m_compose);
    m_stack->insertWidget(SendingIndex, m_sending);
    m_stack->insertWidget(JournalIndex, m_journalPage);
    m_stack->insertWidget(ContactsIndex, m_contactsPage);
    m_stack->insertWidget(SetupIndex, m_setup);
    layout->addWidget(m_stack, 1);

    connect(m_nav, &QButtonGroup::idClicked, this, [this](int id) {
        switch (id) {
        case 0: showPage(ComposeIndex); break;
        case 1: showPage(JournalIndex); break;
        case 2: showPage(ContactsIndex); break;
        case 3:
            m_setup->load(m_settings, false);
            showPage(SetupIndex);
            break;
        }
    });
    connect(m_statusChip, &QPushButton::clicked, this, &MainWindow::refreshStatus);

    connect(m_compose, &ComposePage::sendRequested, this, &MainWindow::startSend);
    connect(m_compose, &ComposePage::settingsRequested, this, [this] {
        m_setup->load(m_settings, false);
        showPage(SetupIndex);
    });
    connect(m_compose, &ComposePage::locationChanged, this,
            [this](const QString &id) { m_compose->setSenderSummary(senderSummary(id)); });
    connect(m_compose, &ComposePage::saveContactRequested, this, [this](const QString &name, const QString &number) {
        m_contactsPage->startNew(name, number);
        showPage(ContactsIndex);
    });

    connect(m_contactsPage, &ContactsPage::saveRequested, this, &MainWindow::savePhonebookEntry);
    connect(m_contactsPage, &ContactsPage::faxRequested, this, [this](const QString &name, const QString &number) {
        m_compose->setRecipient(name, number);
        showPage(ComposeIndex);
    });
    connect(m_contactsPage, &ContactsPage::refreshRequested, this, [this] {
        if (const Location *target = phonebookTarget())
            fetchPhonebook(target->id, true);
    });
    connect(m_contactsPage, &ContactsPage::settingsRequested, this, [this] {
        m_setup->load(m_settings, false);
        showPage(SetupIndex);
    });
    connect(&m_contacts, &ContactStore::changed, this, &MainWindow::updateContacts);
    connect(&m_phonebook, &FritzPhonebook::fetched, this,
            [this](bool ok, const QList<Contact> &contacts, const QString &error) {
                m_phonebookTask = PhonebookTask::None;
                const Location *location = m_settings.find(m_phonebookLocationId);
                if (!location)
                    return;
                if (ok) {
                    m_contacts.setPhonebook(location->id, contacts, QDateTime::currentDateTime());
                    m_contactsPage->setStatus(QStringLiteral("Telefonbuch der FRITZ!Box „%1“ gelesen um %2 Uhr.")
                                                  .arg(location->displayName(),
                                                       QTime::currentTime().toString(QStringLiteral("HH:mm"))));
                } else {
                    m_contactsPage->setStatus(error, true);
                }
            });
    connect(&m_phonebook, &FritzPhonebook::saved, this, [this](bool ok, const QString &error) {
        m_phonebookTask = PhonebookTask::None;
        const Location *location = m_settings.find(m_phonebookLocationId);
        const QString name = location ? location->displayName() : QString();
        m_contactsPage->showSaveResult(ok, ok ? QStringLiteral("✓  In der FRITZ!Box „%1“ gespeichert.").arg(name) : error);
        if (ok && location)
            fetchPhonebook(location->id, true);
    });

    connect(m_sending, &SendingPage::cancelRequested, this, [this] {
        if (m_job) {
            m_job->abort(QStringLiteral("cancelled by user"));
        } else if (m_sendProbe.isRunning()) {
            m_sendProbe.cancel();
            finishWithoutJob(FaxStatus::Aborted, QStringLiteral("cancelled while looking for a location"),
                             describeOutcome(FaxStatus::Aborted, 0, 0, 0));
        }
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
        SendRequest request;
        request.destination = entry.destination;
        request.recipientName = entry.recipientName;
        request.dither = m_settings.dither;
        request.locationId = m_compose->selectedLocationId();
        for (const QString &doc : entry.documents) {
            if (QFileInfo::exists(doc))
                request.documents.append(doc);
            else
                missing.append(QFileInfo(doc).fileName());
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
            [this] { m_compose->setRecentRecipients(m_journal.recentRecipients(3)); });

    connect(m_setup, &SetupPage::phonebookRead, this, [this](const QString &id, const QList<Contact> &contacts) {
        m_checkedPhonebooks.insert(id, contacts);
    });
    connect(m_setup, &SetupPage::saved, this, [this](const AppSettings &settings) {
        settings.save(*m_ini);
        const AppSettings previous = m_settings;
        applySettings(settings);
        m_contacts.prunePhonebooks(previous, settings);
        for (auto it = m_checkedPhonebooks.constBegin(); it != m_checkedPhonebooks.constEnd(); ++it) {
            if (const Location *location = settings.find(it.key()); location && location->hasPhonebook())
                m_contacts.setPhonebook(it.key(), it.value(), QDateTime::currentDateTime());
        }
        m_checkedPhonebooks.clear();
        updateContacts();
        showPage(ComposeIndex);
        refreshStatus();
    });
    connect(m_setup, &SetupPage::connectionChecked, this, [this](bool ok, const QString &locationId) {
        setConnection(ok ? Connection::Connected : Connection::Failed, locationId);
    });
    connect(m_setup, &SetupPage::cancelled, this, [this] {
        m_checkedPhonebooks.clear();
        showPage(ComposeIndex);
    });

    connect(&m_statusProbe, &LocationProbe::finished, this, [this](const QString &id) {
        setConnection(id.isEmpty() ? Connection::Unreachable : Connection::Connected, id);
        if (!id.isEmpty())
            fetchPhonebook(id, false);
    });
    connect(&m_sendProbe, &LocationProbe::finished, this, [this](const QString &id) {
        if (const Location *location = m_settings.find(id)) {
            setConnection(Connection::Connected, id);
            startJob(*location);
            return;
        }
        setConnection(Connection::Unreachable);
        finishWithoutJob(FaxStatus::NetworkError, QStringLiteral("no location reachable"),
                         {QStringLiteral("Keine FRITZ!Box erreichbar"),
                          QStringLiteral("Keiner Ihrer Standorte hat geantwortet. Ist das VPN verbunden?"),
                          Severity::Failure});
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive && m_lastStatusProbe.isValid()
            && m_lastStatusProbe.elapsed() > kStatusRefreshMs)
            refreshStatus();
    });

    m_compose->setRecentRecipients(m_journal.recentRecipients(3));
    applySettings(m_settings);
    updateContacts();
    if (m_settings.isConfigured()) {
        showPage(ComposeIndex);
        refreshStatus();
    } else {
        setConnection(Connection::NotConfigured);
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
    if (m_settings.isConfigured() && !busy())
        showPage(ComposeIndex);
}

bool MainWindow::busy() const
{
    return (m_job && !m_job->isFinished()) || m_sendProbe.isRunning();
}

void MainWindow::showPage(Page page)
{
    m_stack->setCurrentIndex(page);
    const bool firstRun = !m_settings.isConfigured();
    for (QPushButton *button : m_navButtons)
        button->setEnabled(!busy() && !firstRun);

    QAbstractButton *active = nullptr;
    switch (page) {
    case ComposeIndex: active = m_navButtons[0]; break;
    case JournalIndex: active = m_navButtons[1]; break;
    case ContactsIndex: active = m_navButtons[2]; break;
    case SetupIndex: active = m_navButtons[3]; break;
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

void MainWindow::setConnection(Connection state, const QString &locationId)
{
    m_connection = state;
    const Location *location = m_settings.find(locationId);
    const QString name = location ? location->displayName() : QString();
    QColor dot;
    QString text;
    switch (state) {
    case Connection::NotConfigured:
        dot = color::warning;
        text = QStringLiteral("Nicht eingerichtet");
        break;
    case Connection::Searching:
        dot = QColor(0xb9, 0xa0, 0xc9);
        text = QStringLiteral("Standort wird gesucht …");
        break;
    case Connection::Connected:
        m_connectedId = locationId;
        dot = color::success;
        text = QStringLiteral("Verbunden mit %1").arg(name);
        break;
    case Connection::Unreachable:
        m_connectedId.clear();
        dot = color::error;
        text = QStringLiteral("Keine FRITZ!Box erreichbar");
        break;
    case Connection::Failed:
        dot = color::error;
        text = QStringLiteral("Keine Verbindung zu %1").arg(name);
        break;
    }
    m_statusChip->setIcon(dotIcon(dot));
    m_statusText = text;
    updateStatusChip();

    const Location *found = m_settings.find(m_connectedId);
    m_compose->setAutoLocationHint(state == Connection::Searching ? QString()
                                   : found ? QStringLiteral("Zuletzt erreichbar: %1").arg(found->displayName())
                                           : QStringLiteral("Zurzeit ist keine FRITZ!Box erreichbar."));
    m_compose->setSenderSummary(senderSummary(m_compose->selectedLocationId()));
    if (state == Connection::Connected || state == Connection::Unreachable)
        updateContacts();
}

const Location *MainWindow::phonebookTarget() const
{
    if (const Location *connected = m_settings.find(m_connectedId); connected && connected->hasPhonebook())
        return connected;
    for (const Location &location : m_settings.locations) {
        if (location.hasPhonebook())
            return &location;
    }
    return nullptr;
}

void MainWindow::updateContacts()
{
    bool configured = false;
    for (const Location &location : m_settings.locations)
        configured = configured || location.hasPhonebook();
    const Location *target = phonebookTarget();
    const QList<Contact> contacts = m_contacts.all(m_settings);
    m_compose->setContacts(contacts, target != nullptr);
    m_contactsPage->setTarget(target ? target->displayName() : QString());
    m_contactsPage->setContacts(contacts, configured);
}

void MainWindow::fetchPhonebook(const QString &locationId, bool force)
{
    const Location *location = m_settings.find(locationId);
    if (!location || !location->hasPhonebook() || m_phonebookTask == PhonebookTask::Save)
        return;
    const QDateTime fetched = m_contacts.phonebookTime(location->id);
    if (!force && fetched.isValid() && fetched.secsTo(QDateTime::currentDateTime()) < kPhonebookMaxAgeSecs)
        return;
    m_phonebookTask = PhonebookTask::Fetch;
    m_phonebookLocationId = location->id;
    m_contactsPage->setStatus(QStringLiteral("Telefonbuch der FRITZ!Box „%1“ wird gelesen …").arg(location->displayName()));
    m_phonebook.setBox(location->server, location->phonebookUser, location->phonebookPassword);
    m_phonebook.fetch();
}

void MainWindow::savePhonebookEntry(const Contact &original, const QString &name, const QString &number)
{
    const Location *location = original.uniqueId.isEmpty() ? phonebookTarget() : m_settings.find(original.locationId);
    if (!location || !location->hasPhonebook()) {
        m_contactsPage->showSaveResult(false, QStringLiteral("Für diese FRITZ!Box ist kein Telefonbuch-Zugang "
                                                             "eingerichtet."));
        return;
    }
    m_phonebookTask = PhonebookTask::Save;
    m_phonebookLocationId = location->id;
    m_phonebook.setBox(location->server, location->phonebookUser, location->phonebookPassword);
    m_phonebook.saveEntry(original, name, number);
}

QList<Location> MainWindow::probeCandidates() const
{
    if (!m_settings.autoSelect) {
        if (const Location *preferred = m_settings.find(m_settings.preferredId); preferred && preferred->isComplete())
            return {*preferred};
    }
    return m_settings.completeLocations();
}

void MainWindow::refreshStatus()
{
    if (busy())
        return;
    if (!m_settings.isConfigured()) {
        setConnection(Connection::NotConfigured);
        return;
    }
    m_lastStatusProbe.start();
    setConnection(Connection::Searching);
    m_statusProbe.start(m_settings, probeCandidates());
}

void MainWindow::applySettings(const AppSettings &settings)
{
    m_settings = settings;
    m_compose->setLocations(settings.locations, settings.autoSelect ? QString() : settings.preferredId);
    m_compose->setDitherDefault(settings.dither);
}

QString MainWindow::senderSummary(const QString &locationId) const
{
    const Location *location = m_settings.find(locationId);
    if (!location)
        location = m_settings.find(m_connectedId);
    if (!location && !m_settings.locations.isEmpty())
        location = &m_settings.locations.first();

    QStringList parts;
    if (location) {
        if (m_settings.locations.size() > 1)
            parts.append(QStringLiteral("Über %1").arg(location->displayName()));
        parts.append(location->header.isEmpty() ? QStringLiteral("ohne Kopfzeile")
                                                : QStringLiteral("Kopfzeile „%1“").arg(location->header));
        if (!location->ident.isEmpty())
            parts.append(QStringLiteral("Kennung %1").arg(location->ident));
    }
    parts.append(QStringLiteral("bis %1 bit/s").arg(m_settings.maxRate));
    parts.append(m_settings.ecm ? QStringLiteral("Fehlerkorrektur an") : QStringLiteral("Fehlerkorrektur aus"));
    QString text = parts.join(QStringLiteral(", "));
    text[0] = text[0].toUpper();
    return text;
}

void MainWindow::startSend(const SendRequest &request)
{
    if (busy())
        return;
    if (!m_settings.isConfigured()) {
        m_setup->load(m_settings, true);
        showPage(SetupIndex);
        return;
    }
    m_statusProbe.cancel();
    m_lastRequest = request;
    m_jobLocation = Location();
    m_jobTimer.start();

    const QString recipient = request.recipientName.isEmpty() ? request.destination : request.recipientName;
    m_sending->begin(recipient, request.destination, request.pages);

    const Location *chosen = m_settings.find(request.locationId);
    const QList<Location> candidates = m_settings.completeLocations();
    if (chosen) {
        startJob(*chosen);
    } else if (candidates.size() == 1) {
        startJob(candidates.first());
    } else {
        // Find the router that is reachable now, then send through it.
        m_sending->showSearching();
        setConnection(Connection::Searching);
        m_sendProbe.start(m_settings, candidates);
    }
    showPage(SendingIndex);
}

void MainWindow::startJob(const Location &location)
{
    m_jobLocation = location;

    FaxJobConfig config;
    config.productName = QStringLiteral(FAXWERK_PRODUCT_NAME);
    config.sip.account = m_settings.account(location);
    config.destination = normalizeNumber(m_lastRequest.destination);
    config.pdfFiles = m_lastRequest.documents;
    config.fax.stationId = location.ident;
    config.fax.headerInfo = location.header;
    config.fax.maxBitRate = m_settings.maxRate;
    config.fax.ecm = m_settings.ecm;
    config.raster.mode = m_lastRequest.dither ? PdfRasterizer::Mode::Dither : PdfRasterizer::Mode::Threshold;
    config.timeoutSeconds = m_settings.timeoutSeconds;

    auto *job = new FaxJob(config, this);
    m_job = job;
    m_sending->attach(job, m_settings.locations.size() > 1 ? location.displayName() : QString());
    connect(job, &FaxJob::phaseChanged, this, [this](FaxPhase phase) {
        if (phase == FaxPhase::Dialing)
            setConnection(Connection::Connected, m_jobLocation.id);
    });
    connect(job, &FaxJob::finished, this, &MainWindow::onJobFinished, Qt::QueuedConnection);
    showPage(SendingIndex);
    job->start();
}

void MainWindow::finishWithoutJob(FaxStatus status, const QString &message, const OutcomeText &text)
{
    m_sending->showFailure(text, message);
    FaxOutcome outcome;
    outcome.status = status;
    outcome.message = message;
    addJournalEntry(outcome, QString());
    showPage(SendingIndex);
    if (m_closeAfterJob)
        close();
}

void MainWindow::addJournalEntry(const FaxOutcome &outcome, const QString &location)
{
    JournalEntry entry;
    entry.time = QDateTime::currentDateTime();
    entry.destination = m_lastRequest.destination;
    entry.recipientName = m_lastRequest.recipientName;
    entry.location = location;
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
    m_journal.add(entry);
}

void MainWindow::onJobFinished()
{
    if (!m_job)
        return;
    const FaxOutcome outcome = m_job->outcome();
    if (outcome.status != FaxStatus::InvalidRequest)
        addJournalEntry(outcome, m_settings.locations.size() > 1 ? m_jobLocation.displayName() : QString());

    if (outcome.status == FaxStatus::AuthenticationFailed || outcome.status == FaxStatus::NetworkError
        || outcome.status == FaxStatus::RegistrationFailed)
        setConnection(Connection::Failed, m_jobLocation.id);

    m_job->deleteLater();
    m_job.clear();
    showPage(SendingIndex);

    if (m_closeAfterJob)
        close();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    // Keep the navigation and the connection state visible in narrow windows.
    m_productName->setVisible(width() >= 1040);
    updateStatusChip();
    QMainWindow::resizeEvent(event);
}

void MainWindow::updateStatusChip()
{
    const bool compact = width() < 900;
    m_statusChip->setText(compact ? QString() : m_statusText);
    m_statusChip->setToolTip(compact ? QStringLiteral("%1. Klicken, um erneut zu suchen.").arg(m_statusText)
                                     : QStringLiteral("Erreichbaren Standort erneut suchen"));
    m_statusChip->setAccessibleName(m_statusText);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!busy()) {
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
        if (m_job) {
            m_job->abort(QStringLiteral("application closed"));
        } else {
            m_sendProbe.cancel();
            close();
        }
    }
}

} // namespace faxwerk::gui
