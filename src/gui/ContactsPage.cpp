#include "ContactsPage.h"

#include "Widgets.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace faxwerk::gui {

namespace {

constexpr int kMaxListed = 500;

QWidget *makeRow(const Contact &contact)
{
    auto *row = new QWidget;
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(14);
    auto *text = new QVBoxLayout;
    text->setSpacing(1);
    auto *name = new QLabel(contact.name.isEmpty() ? contact.number : contact.name);
    name->setStyleSheet(QStringLiteral("font-weight: 500; background: transparent;"));
    auto *number = new QLabel(QStringLiteral("%1 · %2").arg(contact.number, contact.typeLabel()));
    number->setProperty("role", QStringLiteral("muted"));
    number->setStyleSheet(QStringLiteral("background: transparent;"));
    text->addWidget(name);
    text->addWidget(number);
    layout->addLayout(text, 1);
    auto *source = new QLabel(contact.locationName);
    source->setProperty("role", QStringLiteral("muted"));
    source->setStyleSheet(QStringLiteral("background: transparent;"));
    layout->addWidget(source, 0, Qt::AlignVCenter);
    return row;
}

QLabel *fieldLabel(const QString &text, QWidget *buddy)
{
    auto *label = new QLabel(text);
    label->setProperty("role", QStringLiteral("fieldLabel"));
    label->setBuddy(buddy);
    return label;
}

} // namespace

ContactsPage::ContactsPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(28, 32, 28, 32);
    auto *inner = new QWidget;
    inner->setMaximumWidth(1180);
    outer->addWidget(inner);
    m_stack = new QStackedWidget;
    auto *innerLayout = new QVBoxLayout(inner);
    innerLayout->setContentsMargins(0, 0, 0, 0);
    innerLayout->addWidget(m_stack);

    // --- No phonebook access set up --------------------------------------------------------
    auto *empty = new QWidget;
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->addStretch(1);
    auto *emptyTitle = new QLabel(QStringLiteral("Telefonbuch der FRITZ!Box verbinden"));
    emptyTitle->setProperty("role", QStringLiteral("sectionTitle"));
    emptyTitle->setAlignment(Qt::AlignHCenter);
    auto *emptyText = new QLabel(QStringLiteral(
        "Faxwerk zeigt die Kontakte aus dem Telefonbuch Ihrer FRITZ!Box und schlägt sie beim Eintippen vor. "
        "Neue und geänderte Kontakte speichert es dort. Dafür braucht Faxwerk einen FRITZ!Box-Benutzer."));
    emptyText->setProperty("role", QStringLiteral("subtitle"));
    emptyText->setAlignment(Qt::AlignHCenter);
    emptyText->setWordWrap(true);
    // Centered by stretches, not by an alignment flag: with an alignment the
    // layout ignores the height a wrapped label needs and cuts off the text.
    emptyText->setFixedWidth(520);
    auto *emptyTextRow = new QHBoxLayout;
    emptyTextRow->addStretch(1);
    emptyTextRow->addWidget(emptyText);
    emptyTextRow->addStretch(1);
    auto *emptyButton = new QPushButton(QStringLiteral("Zugang einrichten"));
    emptyButton->setProperty("role", QStringLiteral("primary"));
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addLayout(emptyTextRow);
    emptyLayout->addSpacing(12);
    emptyLayout->addWidget(emptyButton, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(2);
    m_stack->addWidget(empty);

    // --- List and editor -------------------------------------------------------------------
    auto *main = new QWidget;
    m_columns = new QBoxLayout(QBoxLayout::LeftToRight, main);
    m_columns->setContentsMargins(0, 0, 0, 0);
    m_columns->setSpacing(24);

    auto *listColumn = new QVBoxLayout;
    listColumn->setSpacing(16);
    auto *header = new QHBoxLayout;
    header->setSpacing(10);
    auto *title = new QLabel(QStringLiteral("Kontakte"));
    title->setProperty("role", QStringLiteral("title"));
    header->addWidget(title, 1);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Name oder Nummer"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(200);
    m_search->setStyleSheet(QStringLiteral("QLineEdit { border-radius: 23px; }"));
    header->addWidget(m_search);
    m_add = new QPushButton(QStringLiteral("Kontakt hinzufügen"));
    header->addWidget(m_add);
    listColumn->addLayout(header);
    m_list = new QListWidget;
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    listColumn->addWidget(m_list, 1);
    auto *footer = new QHBoxLayout;
    m_status = new QLabel;
    m_status->setProperty("role", QStringLiteral("muted"));
    m_status->setWordWrap(true);
    footer->addWidget(m_status, 1);
    auto *refresh = new QPushButton(QStringLiteral("Telefonbuch neu laden"));
    refresh->setProperty("role", QStringLiteral("link"));
    footer->addWidget(refresh, 0, Qt::AlignTop);
    listColumn->addLayout(footer);
    m_columns->addLayout(listColumn, 3);

    auto *card = new QFrame;
    card->setProperty("role", QStringLiteral("card"));
    card->setMinimumWidth(320);
    card->setMaximumWidth(420);
    m_editor = card;
    auto *editor = new QVBoxLayout(card);
    editor->setContentsMargins(26, 26, 26, 26);
    editor->setSpacing(10);
    m_editorTitle = new QLabel;
    m_editorTitle->setProperty("role", QStringLiteral("sectionTitle"));
    m_editorSource = new QLabel;
    m_editorSource->setProperty("role", QStringLiteral("muted"));
    m_editorSource->setWordWrap(true);
    editor->addWidget(m_editorTitle);
    editor->addWidget(m_editorSource);
    editor->addSpacing(6);
    m_name = new QLineEdit;
    m_name->setPlaceholderText(QStringLiteral("z. B. Praxis Dr. Berger, HNO"));
    m_number = new QLineEdit;
    m_number->setPlaceholderText(QStringLiteral("z. B. 0761 4567 890"));
    m_number->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9+ /()*#-]{0,40}")), m_number));
    editor->addWidget(fieldLabel(QStringLiteral("Name"), m_name));
    editor->addWidget(m_name);
    editor->addSpacing(4);
    editor->addWidget(fieldLabel(QStringLiteral("Faxnummer"), m_number));
    editor->addWidget(m_number);
    m_result = new QLabel;
    m_result->setContentsMargins(16, 12, 16, 12);
    m_result->setWordWrap(true);
    m_result->hide();
    editor->addSpacing(4);
    editor->addWidget(m_result);
    editor->addSpacing(8);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    m_save = new QPushButton(QStringLiteral("Speichern"));
    m_save->setToolTip(QStringLiteral("Im Telefonbuch der FRITZ!Box speichern"));
    m_save->setProperty("role", QStringLiteral("primary"));
    m_fax = new QPushButton(QStringLiteral("Fax senden"));
    buttons->addWidget(m_save, 1);
    buttons->addWidget(m_fax, 1);
    editor->addLayout(buttons);
    editor->addStretch(1);
    m_columns->addWidget(card, 2, Qt::AlignTop);
    m_stack->addWidget(main);

    connect(emptyButton, &QPushButton::clicked, this, &ContactsPage::settingsRequested);
    connect(refresh, &QPushButton::clicked, this, &ContactsPage::refreshRequested);
    connect(m_search, &QLineEdit::textChanged, this, &ContactsPage::rebuild);
    connect(m_add, &QPushButton::clicked, this, [this] { startNew(QString(), QString()); });
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0)
            return;
        const int index = m_list->item(row)->data(Qt::UserRole).toInt();
        if (index >= 0 && index < m_contacts.size())
            edit(m_contacts.at(index));
    });
    for (QLineEdit *field : {m_name, m_number}) {
        connect(field, &QLineEdit::textChanged, this, [this] {
            m_result->hide();
            updateSaveButton();
        });
    }
    connect(m_save, &QPushButton::clicked, this, [this] {
        m_save->setEnabled(false);
        setStyleProperty(m_result, "banner", QStringLiteral("info"));
        m_result->setText(QStringLiteral("Wird in der FRITZ!Box gespeichert …"));
        m_result->show();
        emit saveRequested(m_editing, m_name->text().trimmed(), m_number->text().trimmed());
    });
    connect(m_fax, &QPushButton::clicked, this,
            [this] { emit faxRequested(m_name->text().trimmed(), m_number->text().trimmed()); });

    setContacts({}, false);
}

void ContactsPage::setContacts(const QList<Contact> &contacts, bool configured)
{
    m_contacts = contacts;
    m_stack->setCurrentIndex(configured ? 1 : 0);
    rebuild();
}

void ContactsPage::setTarget(const QString &locationName)
{
    m_target = locationName;
    m_add->setEnabled(!locationName.isEmpty());
    if (m_isNew)
        edit(m_editing);
}

void ContactsPage::startNew(const QString &name, const QString &number)
{
    Contact contact;
    contact.name = name;
    contact.number = number;
    m_isNew = true;
    {
        const QSignalBlocker blocker(m_list);
        m_list->setCurrentRow(-1);
    }
    edit(contact);
    m_name->setFocus();
}

void ContactsPage::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("color: #8e2626;") : QString());
}

void ContactsPage::showSaveResult(bool ok, const QString &message)
{
    setStyleProperty(m_result, "banner", ok ? QStringLiteral("ok") : QStringLiteral("fail"));
    m_result->setText(message);
    m_result->show();
    if (ok && m_isNew) {
        // Further edits change the entry just created once the phonebook is read again.
        m_isNew = false;
        m_editing.name = m_name->text().trimmed();
        m_editing.number = m_number->text().trimmed();
    }
    updateSaveButton();
}

void ContactsPage::rebuild()
{
    const QString filter = m_search->text().trimmed();
    const QList<Contact> shown =
        filter.isEmpty() ? m_contacts.mid(0, kMaxListed) : searchContacts(m_contacts, filter, kMaxListed);

    const QSignalBlocker blocker(m_list);
    m_list->clear();
    int selectedRow = -1;
    for (const Contact &contact : shown) {
        const int index = int(m_contacts.indexOf(contact));
        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, index);
        item->setSizeHint(QSize(0, 62));
        m_list->setItemWidget(item, makeRow(contact));
        if (!m_isNew && contact.uniqueId == m_editing.uniqueId && contact.locationId == m_editing.locationId
            && comparableNumber(contact.number) == comparableNumber(m_editing.number))
            selectedRow = m_list->count() - 1;
    }
    if (m_isNew)
        return;
    if (selectedRow < 0 && m_list->count() > 0)
        selectedRow = 0;
    if (selectedRow >= 0) {
        m_list->setCurrentRow(selectedRow);
        edit(m_contacts.at(m_list->item(selectedRow)->data(Qt::UserRole).toInt()));
    } else {
        startNew(QString(), QString());
    }
}

void ContactsPage::edit(const Contact &contact)
{
    const bool sameEntry = contact.uniqueId == m_editing.uniqueId && contact.locationId == m_editing.locationId
                           && contact.number == m_editing.number && !m_isNew;
    if (!contact.uniqueId.isEmpty())
        m_isNew = false;
    m_editing = contact;
    if (m_isNew) {
        m_editorTitle->setText(QStringLiteral("Neuer Kontakt"));
        m_editorSource->setText(m_target.isEmpty()
                                    ? QStringLiteral("Keine FRITZ!Box mit Telefonbuch-Zugang erreichbar.")
                                    : QStringLiteral("Wird im Telefonbuch der FRITZ!Box „%1“ gespeichert.").arg(m_target));
    } else {
        m_editorTitle->setText(QStringLiteral("Kontakt"));
        m_editorSource->setText(QStringLiteral("%1 im Telefonbuch der FRITZ!Box „%2“.")
                                    .arg(contact.typeLabel(), contact.locationName));
    }
    if (!sameEntry) {
        const QSignalBlocker nameBlocker(m_name);
        const QSignalBlocker numberBlocker(m_number);
        m_name->setText(contact.name);
        m_number->setText(contact.number);
        m_result->hide();
    }
    updateSaveButton();
}

void ContactsPage::updateSaveButton()
{
    const QString name = m_name->text().trimmed();
    const QString number = m_number->text().trimmed();
    const bool changed = m_isNew || name != m_editing.name || number != m_editing.number;
    const bool target = !m_isNew || !m_target.isEmpty();
    m_save->setEnabled(!name.isEmpty() && comparableNumber(number).size() >= 3 && changed && target);
    m_fax->setEnabled(comparableNumber(number).size() >= 3);
}

void ContactsPage::resizeEvent(QResizeEvent *event)
{
    m_columns->setDirection(width() < 860 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    QWidget::resizeEvent(event);
}

} // namespace faxwerk::gui
