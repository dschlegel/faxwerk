#include "SetupPage.h"

#include "FritzPhonebook.h"
#include "Messages.h"
#include "Widgets.h"

#include "sip/SipUserAgent.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace faxwerk::gui {

namespace {

constexpr int kCheckTimeoutMs = 45000;

QLabel *fieldLabel(const QString &text, QWidget *buddy)
{
    auto *label = new QLabel(text);
    label->setProperty("role", QStringLiteral("fieldLabel"));
    label->setBuddy(buddy);
    return label;
}

QVBoxLayout *field(const QString &label, QWidget *input, const QString &hint = QString())
{
    auto *layout = new QVBoxLayout;
    layout->setSpacing(6);
    layout->addWidget(fieldLabel(label, input));
    layout->addWidget(input);
    if (!hint.isEmpty()) {
        auto *note = new QLabel(hint);
        note->setProperty("role", QStringLiteral("muted"));
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    return layout;
}

QHBoxLayout *row(QLayout *a, QLayout *b)
{
    auto *layout = new QHBoxLayout;
    layout->setSpacing(16);
    layout->addLayout(a, 1);
    layout->addLayout(b, 1);
    return layout;
}

const QString kLocationsHelp = QStringLiteral(
    "<p style='line-height: 150%'>Faxwerk meldet sich wie ein IP-Telefon an. Legen Sie dafür in jeder "
    "FRITZ!Box einmal ein Gerät an:</p>"
    "<ol style='margin-left: -20px; line-height: 150%'>"
    "<li>Telefonie › Telefoniegeräte › Neues Gerät einrichten</li>"
    "<li>Telefon, dann LAN/WLAN (IP-Telefon) wählen</li>"
    "<li>Benutzername und ein starkes Kennwort vergeben</li>"
    "<li>Ausgehende Rufnummer für Faxe wählen, eingehende Nummern weglassen</li>"
    "</ol>"
    "<p style='line-height: 150%'><b>Mehrere Standorte:</b> ein Standort pro FRITZ!Box. Faxwerk nimmt vor "
    "jedem Fax den ersten Standort, der erreichbar ist, zum Beispiel über das gerade verbundene VPN. Geben Sie "
    "jeder Box dafür ihre eigene Adresse (IP-Adresse statt fritz.box).</p>");

const QString kPhonebookHelp = QStringLiteral(
    "<p style='line-height: 150%'>Mit einem FRITZ!Box-Benutzer zeigt Faxwerk die Kontakte aus dem Telefonbuch "
    "der Box, schlägt sie beim Eintippen vor und speichert neue und geänderte Kontakte dort. Das ist "
    "freiwillig.</p>"
    "<ol style='margin-left: -20px; line-height: 150%'>"
    "<li>System › FRITZ!Box-Benutzer › Benutzer hinzufügen</li>"
    "<li>Name und Kennwort vergeben, „Zugang auch aus dem Internet“ ausschalten</li>"
    "<li>Recht „Sprachnachrichten, Faxnachrichten, FRITZ!App Fon und Anrufliste“ zum Lesen, zusätzlich "
    "„FRITZ!Box Einstellungen“ zum Speichern von Kontakten</li>"
    "</ol>"
    "<p style='line-height: 150%'>Unter Heimnetz › Netzwerk › Netzwerkeinstellungen muss „Zugriff für "
    "Anwendungen zulassen“ eingeschaltet sein (Standard).</p>");

const QString kTransferHelp = QStringLiteral(
    "<p style='line-height: 150%'>Bei automatischer Wahl prüft Faxwerk die Standorte in ihrer Reihenfolge und "
    "sendet über den ersten, der antwortet. Ohne automatische Wahl wählen Sie den Standort beim Fax aus.</p>"
    "<p style='line-height: 150%'>Übertragen Faxe über Ihren Anschluss unzuverlässig, bleiben Sie bei "
    "9600 bit/s.</p>");

} // namespace

SetupPage::SetupPage(QWidget *parent)
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
    auto *centering = new QVBoxLayout(content);
    centering->setContentsMargins(24, 28, 24, 28);
    centering->addStretch(1);

    auto *card = new QFrame;
    card->setProperty("role", QStringLiteral("card"));
    card->setMaximumWidth(1080);
    card->setStyleSheet(QStringLiteral("QFrame[role=\"card\"] { border-radius: 30px; }"));
    auto *cardRow = new QHBoxLayout(card);
    cardRow->setContentsMargins(0, 0, 0, 0);
    cardRow->setSpacing(0);
    auto *hCenter = new QHBoxLayout;
    hCenter->addWidget(card);
    centering->addLayout(hCenter);
    centering->addStretch(1);

    // --- Left: help ---------------------------------------------------------------------
    auto *help = new QFrame;
    help->setProperty("role", QStringLiteral("darkCard"));
    help->setStyleSheet(QStringLiteral("QFrame[role=\"darkCard\"] { border-radius: 0; border-top-left-radius: 29px; "
                                       "border-bottom-left-radius: 29px; }"));
    help->setMinimumWidth(320);
    help->setMaximumWidth(380);
    auto *helpLayout = new QVBoxLayout(help);
    helpLayout->setContentsMargins(34, 34, 34, 34);
    helpLayout->setSpacing(18);
    auto *pills = new QHBoxLayout;
    pills->setSpacing(8);
    for (QPushButton *&pill : m_stepPills)
        pill = new QPushButton;
    for (QPushButton *pill : m_stepPills) {
        // Indicators only, not clickable.
        pill->setFocusPolicy(Qt::NoFocus);
        pill->setAttribute(Qt::WA_TransparentForMouseEvents);
        pills->addWidget(pill);
    }
    pills->addStretch(1);
    helpLayout->addLayout(pills);
    helpLayout->addSpacing(8);
    m_helpTitle = new QLabel;
    m_helpTitle->setStyleSheet(QStringLiteral("font-size: 21px; font-weight: 600;"));
    m_helpText = new QLabel;
    m_helpText->setWordWrap(true);
    m_helpText->setTextFormat(Qt::RichText);
    m_helpText->setProperty("role", QStringLiteral("onDarkMuted"));
    helpLayout->addWidget(m_helpTitle);
    helpLayout->addWidget(m_helpText);
    helpLayout->addStretch(1);
    cardRow->addWidget(help);

    m_steps = new QStackedWidget;
    m_steps->setStyleSheet(QStringLiteral("QStackedWidget { background: transparent; }"));
    cardRow->addWidget(m_steps, 1);

    // --- Step 1: locations ----------------------------------------------------------------
    auto *locations = new QWidget;
    auto *l = new QVBoxLayout(locations);
    l->setContentsMargins(38, 34, 38, 32);
    l->setSpacing(16);
    auto *lTitle = new QLabel(QStringLiteral("FRITZ!Box-Standorte"));
    lTitle->setProperty("role", QStringLiteral("title"));
    lTitle->setStyleSheet(QStringLiteral("font-size: 32px;"));
    lTitle->setWordWrap(true);
    l->addWidget(lTitle);

    auto *picker = new QHBoxLayout;
    picker->setSpacing(10);
    m_locationBox = new QComboBox;
    m_locationBox->setMinimumWidth(240);
    m_locationBox->setAccessibleName(QStringLiteral("Standort"));
    m_addLocation = new QPushButton(QStringLiteral("Standort hinzufügen"));
    m_removeLocation = new QPushButton(QStringLiteral("Entfernen"));
    m_removeLocation->setProperty("role", QStringLiteral("link"));
    picker->addWidget(m_locationBox, 1);
    picker->addWidget(m_addLocation);
    picker->addWidget(m_removeLocation);
    l->addLayout(picker);

    m_name = new QLineEdit;
    m_name->setPlaceholderText(QStringLiteral("z. B. Praxis Freiburg"));
    m_server = new QLineEdit;
    m_server->setPlaceholderText(QStringLiteral("fritz.box oder 192.168.10.1"));
    l->addLayout(row(field(QStringLiteral("Name"), m_name), field(QStringLiteral("Adresse der Box"), m_server)));
    m_user = new QLineEdit;
    m_user->setPlaceholderText(QStringLiteral("z. B. faxwerk620"));
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    l->addLayout(row(field(QStringLiteral("Benutzername"), m_user), field(QStringLiteral("Kennwort"), m_password)));
    m_header = new QLineEdit;
    m_header->setMaxLength(50);
    m_header->setPlaceholderText(QStringLiteral("z. B. Praxis für Logopädie"));
    m_ident = new QLineEdit;
    m_ident->setMaxLength(20);
    m_ident->setPlaceholderText(QStringLiteral("+49 761 1234567"));
    l->addLayout(row(field(QStringLiteral("Kopfzeile"), m_header),
                     field(QStringLiteral("Kennung (Faxnummer)"), m_ident)));
    auto *note = new QLabel(QStringLiteral(
        "Kopfzeile und Kennung stehen beim Empfänger auf jeder Seite. Das Kennwort wird in der "
        "Faxwerk-Konfiguration auf diesem Computer gespeichert."));
    note->setProperty("role", QStringLiteral("muted"));
    note->setWordWrap(true);
    l->addWidget(note);
    m_checkResult = new QLabel;
    m_checkResult->setProperty("banner", QStringLiteral("info"));
    m_checkResult->setContentsMargins(16, 12, 16, 12);
    m_checkResult->setWordWrap(true);
    m_checkResult->hide();
    l->addWidget(m_checkResult);
    l->addStretch(1);
    auto *lButtons = new QHBoxLayout;
    lButtons->setSpacing(10);
    m_cancel = new QPushButton(QStringLiteral("Abbrechen"));
    m_check = new QPushButton(QStringLiteral("Verbindung prüfen"));
    m_next = new QPushButton(QStringLiteral("Weiter"));
    m_next->setProperty("role", QStringLiteral("primary"));
    lButtons->addWidget(m_cancel);
    lButtons->addStretch(1);
    lButtons->addWidget(m_check);
    lButtons->addWidget(m_next);
    l->addLayout(lButtons);
    m_steps->addWidget(locations);

    // --- Step 2: phonebook ------------------------------------------------------------------
    auto *book = new QWidget;
    auto *b = new QVBoxLayout(book);
    b->setContentsMargins(38, 34, 38, 32);
    b->setSpacing(16);
    auto *bTitle = new QLabel(QStringLiteral("Telefonbuch"));
    bTitle->setProperty("role", QStringLiteral("title"));
    bTitle->setStyleSheet(QStringLiteral("font-size: 32px;"));
    b->addWidget(bTitle);
    m_bookLocationRow = new QWidget;
    auto *bookLocationLayout = new QVBoxLayout(m_bookLocationRow);
    bookLocationLayout->setContentsMargins(0, 0, 0, 0);
    m_bookLocation = new QComboBox;
    m_bookLocation->setAccessibleName(QStringLiteral("Standort"));
    bookLocationLayout->addLayout(field(QStringLiteral("Standort"), m_bookLocation));
    b->addWidget(m_bookLocationRow);
    m_bookUser = new QLineEdit;
    m_bookUser->setPlaceholderText(QStringLiteral("z. B. faxwerk"));
    m_bookPassword = new QLineEdit;
    m_bookPassword->setEchoMode(QLineEdit::Password);
    b->addLayout(row(field(QStringLiteral("FRITZ!Box-Benutzer"), m_bookUser),
                     field(QStringLiteral("Kennwort"), m_bookPassword)));
    auto *bookNote = new QLabel(QStringLiteral(
        "Nicht der Benutzername des IP-Telefons, sondern ein Benutzer, mit dem man sich an der FRITZ!Box "
        "anmeldet. Leer lassen, wenn Faxwerk das Telefonbuch nicht verwenden soll."));
    bookNote->setProperty("role", QStringLiteral("muted"));
    bookNote->setWordWrap(true);
    b->addWidget(bookNote);
    m_bookResult = new QLabel;
    m_bookResult->setContentsMargins(16, 12, 16, 12);
    m_bookResult->setWordWrap(true);
    m_bookResult->hide();
    b->addWidget(m_bookResult);
    b->addStretch(1);
    auto *bButtons = new QHBoxLayout;
    bButtons->setSpacing(10);
    auto *bookBack = new QPushButton(QStringLiteral("Zurück"));
    m_bookCheck = new QPushButton(QStringLiteral("Telefonbuch prüfen"));
    auto *bookNext = new QPushButton(QStringLiteral("Weiter"));
    bookNext->setProperty("role", QStringLiteral("primary"));
    bButtons->addWidget(bookBack);
    bButtons->addStretch(1);
    bButtons->addWidget(m_bookCheck);
    bButtons->addWidget(bookNext);
    b->addLayout(bButtons);
    m_steps->addWidget(book);
    m_phonebook = new FritzPhonebook(this);

    // --- Step 3: transfer -------------------------------------------------------------------
    auto *transfer = new QWidget;
    auto *t = new QVBoxLayout(transfer);
    t->setContentsMargins(38, 34, 38, 32);
    t->setSpacing(18);
    auto *tTitle = new QLabel(QStringLiteral("Übertragung"));
    tTitle->setProperty("role", QStringLiteral("title"));
    tTitle->setStyleSheet(QStringLiteral("font-size: 32px;"));
    t->addWidget(tTitle);
    m_autoSelect = new QCheckBox(QStringLiteral("Standort automatisch wählen (den erreichbaren)"));
    t->addWidget(m_autoSelect);
    m_rate = new QComboBox;
    m_rate->addItem(QStringLiteral("Bis 9600 bit/s (empfohlen, robuster)"), 9600);
    m_rate->addItem(QStringLiteral("Bis 14400 bit/s (schneller)"), 14400);
    t->addLayout(field(QStringLiteral("Geschwindigkeit"), m_rate));
    m_ecm = new QCheckBox(QStringLiteral("Fehlerkorrektur verwenden (empfohlen)"));
    m_dither = new QCheckBox(QStringLiteral("Fotos standardmäßig weich rastern"));
    t->addWidget(m_ecm);
    t->addWidget(m_dither);
    t->addStretch(1);
    auto *tButtons = new QHBoxLayout;
    tButtons->setSpacing(10);
    auto *back = new QPushButton(QStringLiteral("Zurück"));
    auto *save = new QPushButton(QStringLiteral("Speichern"));
    save->setProperty("role", QStringLiteral("primary"));
    tButtons->addWidget(back);
    tButtons->addStretch(1);
    tButtons->addWidget(save);
    t->addLayout(tButtons);
    m_steps->addWidget(transfer);

    m_checkTimeout.setSingleShot(true);
    connect(&m_checkTimeout, &QTimer::timeout, this, [this] {
        finishCheck(false, QStringLiteral("Die FRITZ!Box hat nicht geantwortet. Stimmt die Adresse, und ist das "
                                          "VPN verbunden?"));
    });
    for (QLineEdit *edit : {m_server, m_user, m_password}) {
        connect(edit, &QLineEdit::textChanged, this, [this] {
            m_checkResult->hide();
            updateButtons();
        });
    }
    connect(m_name, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (m_current >= 0)
            m_locationBox->setItemText(m_current, text.trimmed().isEmpty() ? m_server->text() : text.trimmed());
    });
    connect(m_locationBox, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == m_current || index < 0)
            return;
        storeForm();
        loadForm(index);
    });
    connect(m_addLocation, &QPushButton::clicked, this, &SetupPage::addLocation);
    connect(m_removeLocation, &QPushButton::clicked, this, &SetupPage::removeLocation);
    connect(m_check, &QPushButton::clicked, this, &SetupPage::checkConnection);
    connect(m_next, &QPushButton::clicked, this, [this] {
        storeForm();
        if (validateLocations()) {
            const QSignalBlocker blocker(m_bookLocation);
            m_bookLocation->clear();
            for (const Location &location : std::as_const(m_locations))
                m_bookLocation->addItem(location.displayName());
            m_bookLocationRow->setVisible(m_locations.size() > 1);
            m_bookCurrent = -1;
            loadBookForm(std::max(0, m_current));
            showStep(1);
        }
    });
    connect(m_cancel, &QPushButton::clicked, this, &SetupPage::cancelled);
    connect(back, &QPushButton::clicked, this, [this] { showStep(1); });
    connect(bookBack, &QPushButton::clicked, this, [this] {
        storeBookForm();
        showStep(0);
    });
    connect(bookNext, &QPushButton::clicked, this, [this] {
        storeBookForm();
        showStep(2);
    });
    connect(m_bookLocation, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == m_bookCurrent || index < 0)
            return;
        storeBookForm();
        loadBookForm(index);
    });
    for (QLineEdit *edit : {m_bookUser, m_bookPassword}) {
        connect(edit, &QLineEdit::textChanged, this, [this] {
            m_bookResult->hide();
            m_bookCheck->setEnabled(!m_bookPassword->text().isEmpty() && !m_phonebook->isRunning());
        });
    }
    connect(m_bookCheck, &QPushButton::clicked, this, &SetupPage::checkPhonebook);
    connect(m_phonebook, &FritzPhonebook::fetched, this,
            [this](bool ok, const QList<Contact> &contacts, const QString &error) {
                setStyleProperty(m_bookResult, "banner", ok ? QStringLiteral("ok") : QStringLiteral("fail"));
                m_bookResult->setText(ok ? QStringLiteral("✓  Telefonbuch gelesen: %1").arg(contactCountText(contacts.size()))
                                         : error);
                m_bookResult->show();
                m_bookCheck->setText(QStringLiteral("Telefonbuch prüfen"));
                m_bookCheck->setEnabled(true);
                if (ok)
                    emit phonebookRead(m_bookCheckingId, contacts);
            });
    connect(save, &QPushButton::clicked, this, [this] { emit saved(collect()); });

    showStep(0);
}

SetupPage::~SetupPage() = default;

void SetupPage::load(const AppSettings &settings, bool firstRun)
{
    m_base = settings;
    m_locations = settings.locations;
    if (m_locations.isEmpty())
        m_locations.append(AppSettings::newLocation(QStringLiteral("Standort 1")));
    m_current = -1;
    refreshLocationBox();
    loadForm(0);

    m_autoSelect->setChecked(settings.autoSelect);
    m_rate->setCurrentIndex(settings.maxRate == 14400 ? 1 : 0);
    m_ecm->setChecked(settings.ecm);
    m_dither->setChecked(settings.dither);
    m_bookCurrent = -1;
    m_phonebook->cancel();
    m_cancel->setVisible(!firstRun);
    showStep(0);
}

void SetupPage::refreshLocationBox()
{
    const QSignalBlocker blocker(m_locationBox);
    m_locationBox->clear();
    for (const Location &location : std::as_const(m_locations))
        m_locationBox->addItem(location.displayName());
}

void SetupPage::storeForm()
{
    if (m_current < 0 || m_current >= m_locations.size())
        return;
    Location &location = m_locations[m_current];
    location.name = m_name->text().trimmed();
    location.server = m_server->text().trimmed();
    location.user = m_user->text().trimmed();
    location.password = m_password->text();
    location.header = m_header->text().trimmed();
    location.ident = m_ident->text().trimmed();
}

void SetupPage::loadForm(int index)
{
    m_current = index;
    const Location &location = m_locations.at(index);
    {
        const QSignalBlocker blocker(m_name);
        m_name->setText(location.name);
    }
    m_server->setText(location.server);
    m_user->setText(location.user);
    m_password->setText(location.password);
    m_header->setText(location.header);
    m_ident->setText(location.ident);
    {
        const QSignalBlocker blocker(m_locationBox);
        m_locationBox->setCurrentIndex(index);
    }
    m_checkResult->hide();
    updateButtons();
}

void SetupPage::addLocation()
{
    storeForm();
    Location location = AppSettings::newLocation(QStringLiteral("Standort %1").arg(m_locations.size() + 1));
    // Most practices use the same IP phone user name everywhere.
    if (!m_locations.isEmpty()) {
        location.user = m_locations.last().user;
        location.header = m_locations.last().header;
    }
    location.server.clear();
    m_locations.append(location);
    refreshLocationBox();
    loadForm(int(m_locations.size()) - 1);
    m_name->setFocus();
    m_name->selectAll();
}

void SetupPage::removeLocation()
{
    if (m_locations.size() <= 1 || m_current < 0)
        return;
    const int index = m_current;
    m_locations.removeAt(index);
    m_current = -1;
    refreshLocationBox();
    loadForm(std::min(index, int(m_locations.size()) - 1));
}

bool SetupPage::validateLocations()
{
    for (int i = 0; i < m_locations.size(); ++i) {
        if (!m_locations.at(i).isComplete()) {
            loadForm(i);
            setStyleProperty(m_checkResult, "banner", QStringLiteral("fail"));
            m_checkResult->setText(QStringLiteral("Für „%1“ fehlen noch Adresse, Benutzername oder Kennwort.")
                                       .arg(m_locations.at(i).displayName()));
            m_checkResult->show();
            return false;
        }
    }
    return true;
}

void SetupPage::showStep(int step)
{
    m_steps->setCurrentIndex(step);
    // The current step shows its name, the others only their number.
    static const QString names[] = {QStringLiteral("Standorte"), QStringLiteral("Telefonbuch"),
                                    QStringLiteral("Übertragung")};
    for (int i = 0; i < 3; ++i) {
        m_stepPills[i]->setText(i == step ? QStringLiteral("%1  %2").arg(i + 1).arg(names[i]) : QString::number(i + 1));
        m_stepPills[i]->setAccessibleName(QStringLiteral("Schritt %1: %2").arg(i + 1).arg(names[i]));
        m_stepPills[i]->setStyleSheet(
            i == step ? QStringLiteral("QPushButton { background: #ffffff; color: #4f3661; border: none; "
                                       "border-radius: 14px; min-height: 28px; max-height: 28px; "
                                       "padding: 0 13px; font-size: 13px; font-weight: 600; }")
                      : QStringLiteral("QPushButton { background: transparent; color: #ffffff; "
                                       "border: 1px solid rgba(255, 255, 255, 0.4); border-radius: 14px; "
                                       "min-height: 26px; max-height: 26px; padding: 0 12px; "
                                       "font-size: 13px; font-weight: 400; }"));
    }
    m_helpTitle->setText(step == 2 ? QStringLiteral("Gut zu wissen") : QStringLiteral("Vorher in der FRITZ!Box"));
    m_helpText->setText(step == 0 ? kLocationsHelp : step == 1 ? kPhonebookHelp : kTransferHelp);
}

void SetupPage::updateButtons()
{
    const bool complete = !m_server->text().trimmed().isEmpty() && !m_user->text().trimmed().isEmpty()
                          && !m_password->text().isEmpty();
    const bool checking = m_ua != nullptr;
    m_check->setEnabled(complete && !checking);
    m_next->setEnabled(!checking);
    m_locationBox->setEnabled(!checking);
    m_addLocation->setEnabled(!checking);
    m_removeLocation->setVisible(m_locations.size() > 1);
    m_removeLocation->setEnabled(!checking);
}

void SetupPage::checkConnection()
{
    storeForm();
    const Location location = m_locations.at(m_current);
    m_checkingId = location.id;
    const SipAccount account = m_base.account(location);
    m_ua = std::make_unique<SipUserAgent>(account);
    SipUserAgent *ua = m_ua.get();
    setStyleProperty(m_checkResult, "banner", QStringLiteral("info"));
    m_checkResult->setText(QStringLiteral("Verbindung wird geprüft …"));
    m_checkResult->setToolTip(QString());
    m_checkResult->show();
    m_check->setText(QStringLiteral("Wird geprüft …"));
    updateButtons();

    connect(ua, &SipUserAgent::registered, this, [this, account](int) {
        finishCheck(true, QStringLiteral("Angemeldet als %1@%2").arg(account.user, account.effectiveDomain()));
    });
    connect(ua, &SipUserAgent::registrationFailed, this,
            [this](SipUserAgent::Failure failure, int, const QString &reason) {
                FaxStatus status = FaxStatus::RegistrationFailed;
                if (failure == SipUserAgent::Failure::ResolveFailed || failure == SipUserAgent::Failure::NetworkError)
                    status = FaxStatus::NetworkError;
                else if (failure == SipUserAgent::Failure::AuthenticationFailed)
                    status = FaxStatus::AuthenticationFailed;
                const OutcomeText text = describeOutcome(status, 0, 0, 0);
                m_checkResult->setToolTip(reason);
                finishCheck(false, text.detail);
            });
    m_checkTimeout.start(kCheckTimeoutMs);
    ua->start();
}

void SetupPage::finishCheck(bool ok, const QString &message)
{
    m_checkTimeout.stop();
    if (m_ua) {
        // Sign off again; the agent is released once that is done.
        SipUserAgent *ua = m_ua.release();
        ua->disconnect(this);
        if (ok) {
            connect(ua, &SipUserAgent::unregistered, ua, &QObject::deleteLater);
            QTimer::singleShot(5000, ua, &QObject::deleteLater);
            ua->unregister();
        } else {
            ua->deleteLater();
        }
    }
    setStyleProperty(m_checkResult, "banner", ok ? QStringLiteral("ok") : QStringLiteral("fail"));
    m_checkResult->setText(ok ? QStringLiteral("✓  %1").arg(message) : message);
    m_checkResult->show();
    m_check->setText(QStringLiteral("Verbindung prüfen"));
    updateButtons();
    emit connectionChecked(ok, m_checkingId);
}

void SetupPage::storeBookForm()
{
    if (m_bookCurrent < 0 || m_bookCurrent >= m_locations.size())
        return;
    Location &location = m_locations[m_bookCurrent];
    location.phonebookUser = m_bookUser->text().trimmed();
    location.phonebookPassword = m_bookPassword->text();
}

void SetupPage::loadBookForm(int index)
{
    if (index < 0 || index >= m_locations.size())
        return;
    m_bookCurrent = index;
    const Location &location = m_locations.at(index);
    {
        const QSignalBlocker blocker(m_bookLocation);
        m_bookLocation->setCurrentIndex(index);
    }
    m_bookUser->setText(location.phonebookUser);
    m_bookPassword->setText(location.phonebookPassword);
    m_bookResult->hide();
    m_bookCheck->setEnabled(!location.phonebookPassword.isEmpty() && !m_phonebook->isRunning());
}

void SetupPage::checkPhonebook()
{
    storeBookForm();
    const Location &location = m_locations.at(m_bookCurrent);
    m_bookCheckingId = location.id;
    setStyleProperty(m_bookResult, "banner", QStringLiteral("info"));
    m_bookResult->setText(QStringLiteral("Telefonbuch wird gelesen …"));
    m_bookResult->show();
    m_bookCheck->setText(QStringLiteral("Wird geprüft …"));
    m_bookCheck->setEnabled(false);
    m_phonebook->setBox(location.server, location.phonebookUser, location.phonebookPassword);
    m_phonebook->fetch();
}

AppSettings SetupPage::collect()
{
    storeForm();
    storeBookForm();
    AppSettings settings = m_base;
    settings.locations = m_locations;
    settings.autoSelect = m_autoSelect->isChecked();
    if (!settings.find(settings.preferredId))
        settings.preferredId = m_locations.isEmpty() ? QString() : m_locations.first().id;
    settings.maxRate = m_rate->currentData().toInt();
    settings.ecm = m_ecm->isChecked();
    settings.dither = m_dither->isChecked();
    return settings;
}

} // namespace faxwerk::gui
