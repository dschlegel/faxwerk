#include "SetupPage.h"

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

const QString kConnectionHelp = QStringLiteral(
    "<p style='line-height: 150%'>Faxwerk meldet sich wie ein IP-Telefon an. Legen Sie dafür einmal ein Gerät an:</p>"
    "<ol style='margin-left: -20px; line-height: 150%'>"
    "<li>Telefonie › Telefoniegeräte › Neues Gerät einrichten</li>"
    "<li>Telefon, dann LAN/WLAN (IP-Telefon) wählen</li>"
    "<li>Benutzername und ein starkes Kennwort vergeben</li>"
    "<li>Ausgehende Rufnummer für Faxe wählen, eingehende Nummern weglassen</li>"
    "</ol>");

const QString kSenderHelp = QStringLiteral(
    "<p style='line-height: 150%'>Die Kopfzeile steht oben auf jeder Seite beim Empfänger, zusammen mit Datum "
    "und Seitenzahl.</p>"
    "<p style='line-height: 150%'>Die Kennung ist Ihre Faxnummer im internationalen Format. Das Faxgerät des "
    "Empfängers zeigt sie an.</p>"
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
    centering->setContentsMargins(24, 32, 24, 32);
    centering->addStretch(1);

    auto *card = new QFrame;
    card->setProperty("role", QStringLiteral("card"));
    card->setMaximumWidth(1040);
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
    m_stepPills[0] = new QPushButton(QStringLiteral("1  Verbindung"));
    m_stepPills[1] = new QPushButton(QStringLiteral("2  Absender"));
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

    // --- Right: steps ---------------------------------------------------------------------
    m_steps = new QStackedWidget;
    m_steps->setStyleSheet(QStringLiteral("QStackedWidget { background: transparent; }"));
    cardRow->addWidget(m_steps, 1);

    // Step 1: connection
    auto *connection = new QWidget;
    auto *c = new QVBoxLayout(connection);
    c->setContentsMargins(38, 36, 38, 34);
    c->setSpacing(18);
    auto *cTitle = new QLabel(QStringLiteral("Mit der FRITZ!Box verbinden"));
    cTitle->setProperty("role", QStringLiteral("title"));
    cTitle->setStyleSheet(QStringLiteral("font-size: 32px;"));
    cTitle->setWordWrap(true);
    auto *cSub = new QLabel(QStringLiteral("Die Zugangsdaten des IP-Telefons, das Sie für Faxwerk angelegt haben."));
    cSub->setProperty("role", QStringLiteral("subtitle"));
    cSub->setWordWrap(true);
    c->addWidget(cTitle);
    c->addWidget(cSub);
    m_server = new QLineEdit;
    m_server->setPlaceholderText(QStringLiteral("fritz.box"));
    c->addLayout(field(QStringLiteral("Adresse der Box"), m_server));
    auto *credentials = new QHBoxLayout;
    credentials->setSpacing(16);
    m_user = new QLineEdit;
    m_user->setPlaceholderText(QStringLiteral("z. B. faxwerk620"));
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    credentials->addLayout(field(QStringLiteral("Benutzername"), m_user));
    credentials->addLayout(field(QStringLiteral("Kennwort"), m_password));
    c->addLayout(credentials);
    auto *storageNote = new QLabel(QStringLiteral(
        "Das Kennwort wird in der Faxwerk-Konfiguration auf diesem Computer gespeichert."));
    storageNote->setProperty("role", QStringLiteral("muted"));
    storageNote->setWordWrap(true);
    c->addWidget(storageNote);
    m_checkResult = new QLabel;
    m_checkResult->setProperty("banner", QStringLiteral("info"));
    m_checkResult->setContentsMargins(16, 12, 16, 12);
    m_checkResult->setWordWrap(true);
    m_checkResult->hide();
    c->addWidget(m_checkResult);
    c->addStretch(1);
    auto *cButtons = new QHBoxLayout;
    cButtons->setSpacing(10);
    m_cancel = new QPushButton(QStringLiteral("Abbrechen"));
    m_check = new QPushButton(QStringLiteral("Verbindung prüfen"));
    m_next = new QPushButton(QStringLiteral("Weiter zu Absender"));
    m_next->setProperty("role", QStringLiteral("primary"));
    cButtons->addWidget(m_cancel);
    cButtons->addStretch(1);
    cButtons->addWidget(m_check);
    cButtons->addWidget(m_next);
    c->addLayout(cButtons);
    m_steps->addWidget(connection);

    // Step 2: sender
    auto *sender = new QWidget;
    auto *s = new QVBoxLayout(sender);
    s->setContentsMargins(38, 36, 38, 34);
    s->setSpacing(18);
    auto *sTitle = new QLabel(QStringLiteral("Absender"));
    sTitle->setProperty("role", QStringLiteral("title"));
    sTitle->setStyleSheet(QStringLiteral("font-size: 32px;"));
    auto *sSub = new QLabel(QStringLiteral("So erscheinen Ihre Faxe beim Empfänger."));
    sSub->setProperty("role", QStringLiteral("subtitle"));
    s->addWidget(sTitle);
    s->addWidget(sSub);
    m_header = new QLineEdit;
    m_header->setMaxLength(50);
    m_header->setPlaceholderText(QStringLiteral("z. B. Praxis für Logopädie"));
    s->addLayout(field(QStringLiteral("Kopfzeile"), m_header, QStringLiteral("Höchstens 50 Zeichen")));
    m_ident = new QLineEdit;
    m_ident->setMaxLength(20);
    m_ident->setPlaceholderText(QStringLiteral("+49 761 1234567"));
    s->addLayout(field(QStringLiteral("Kennung (Ihre Faxnummer)"), m_ident, QStringLiteral("Höchstens 20 Zeichen")));
    m_rate = new QComboBox;
    m_rate->addItem(QStringLiteral("Bis 9600 bit/s (empfohlen, robuster)"), 9600);
    m_rate->addItem(QStringLiteral("Bis 14400 bit/s (schneller)"), 14400);
    s->addLayout(field(QStringLiteral("Übertragung"), m_rate));
    m_ecm = new QCheckBox(QStringLiteral("Fehlerkorrektur verwenden (empfohlen)"));
    m_dither = new QCheckBox(QStringLiteral("Fotos standardmäßig weich rastern"));
    s->addWidget(m_ecm);
    s->addWidget(m_dither);
    s->addStretch(1);
    auto *sButtons = new QHBoxLayout;
    sButtons->setSpacing(10);
    auto *back = new QPushButton(QStringLiteral("Zurück"));
    auto *save = new QPushButton(QStringLiteral("Speichern"));
    save->setProperty("role", QStringLiteral("primary"));
    sButtons->addWidget(back);
    sButtons->addStretch(1);
    sButtons->addWidget(save);
    s->addLayout(sButtons);
    m_steps->addWidget(sender);

    m_checkTimeout.setSingleShot(true);
    connect(&m_checkTimeout, &QTimer::timeout, this, [this] {
        finishCheck(false, QStringLiteral("Die FRITZ!Box hat nicht geantwortet. Stimmt die Adresse?"));
    });
    for (QLineEdit *edit : {m_server, m_user, m_password}) {
        connect(edit, &QLineEdit::textChanged, this, [this] {
            m_checkResult->hide();
            updateButtons();
        });
    }
    connect(m_check, &QPushButton::clicked, this, &SetupPage::checkConnection);
    connect(m_next, &QPushButton::clicked, this, [this] { showStep(1); });
    connect(m_cancel, &QPushButton::clicked, this, &SetupPage::cancelled);
    connect(back, &QPushButton::clicked, this, [this] { showStep(0); });
    connect(save, &QPushButton::clicked, this, [this] { emit saved(collect()); });

    showStep(0);
}

SetupPage::~SetupPage() = default;

void SetupPage::load(const AppSettings &settings, bool firstRun)
{
    m_base = settings;
    m_server->setText(settings.server);
    m_user->setText(settings.user);
    m_password->setText(settings.password);
    m_header->setText(settings.header);
    m_ident->setText(settings.ident);
    m_rate->setCurrentIndex(settings.maxRate == 14400 ? 1 : 0);
    m_ecm->setChecked(settings.ecm);
    m_dither->setChecked(settings.dither);
    m_cancel->setVisible(!firstRun);
    m_checkResult->hide();
    showStep(0);
    updateButtons();
}

void SetupPage::showStep(int step)
{
    m_steps->setCurrentIndex(step);
    for (int i = 0; i < 2; ++i) {
        m_stepPills[i]->setStyleSheet(
            i == step ? QStringLiteral("QPushButton { background: #ffffff; color: #4f3661; border: none; "
                                       "border-radius: 14px; min-height: 28px; max-height: 28px; "
                                       "padding: 0 13px; font-size: 13px; font-weight: 600; }")
                      : QStringLiteral("QPushButton { background: transparent; color: #ffffff; "
                                       "border: 1px solid rgba(255, 255, 255, 0.4); border-radius: 14px; "
                                       "min-height: 26px; max-height: 26px; padding: 0 12px; "
                                       "font-size: 13px; font-weight: 400; }"));
    }
    m_helpTitle->setText(step == 0 ? QStringLiteral("Vorher in der FRITZ!Box") : QStringLiteral("Gut zu wissen"));
    m_helpText->setText(step == 0 ? kConnectionHelp : kSenderHelp);
}

void SetupPage::updateButtons()
{
    const bool complete = !m_server->text().trimmed().isEmpty() && !m_user->text().trimmed().isEmpty()
                          && !m_password->text().isEmpty();
    const bool checking = m_ua != nullptr;
    m_check->setEnabled(complete && !checking);
    m_next->setEnabled(complete && !checking);
}

void SetupPage::checkConnection()
{
    const AppSettings settings = collect();
    m_ua = std::make_unique<SipUserAgent>(settings.account());
    SipUserAgent *ua = m_ua.get();
    setStyleProperty(m_checkResult, "banner", QStringLiteral("info"));
    m_checkResult->setText(QStringLiteral("Verbindung wird geprüft …"));
    m_checkResult->show();
    m_check->setText(QStringLiteral("Wird geprüft …"));
    updateButtons();

    connect(ua, &SipUserAgent::registered, this, [this, settings](int) {
        finishCheck(true, QStringLiteral("Angemeldet als %1@%2").arg(settings.user, settings.account().effectiveDomain()));
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
    emit connectionChecked(ok, m_server->text().trimmed());
}

AppSettings SetupPage::collect() const
{
    AppSettings settings = m_base;
    settings.server = m_server->text().trimmed();
    settings.user = m_user->text().trimmed();
    settings.password = m_password->text();
    settings.header = m_header->text().trimmed();
    settings.ident = m_ident->text().trimmed();
    settings.maxRate = m_rate->currentData().toInt();
    settings.ecm = m_ecm->isChecked();
    settings.dither = m_dither->isChecked();
    return settings;
}

} // namespace faxwerk::gui
