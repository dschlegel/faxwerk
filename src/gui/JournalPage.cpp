#include "JournalPage.h"

#include "Messages.h"
#include "SendReport.h"
#include "Widgets.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace faxwerk::gui {

namespace {

QString pillFor(Severity severity)
{
    switch (severity) {
    case Severity::Ok: return QStringLiteral("ok");
    case Severity::Warning: return QStringLiteral("warn");
    case Severity::Failure: return QStringLiteral("fail");
    }
    return QStringLiteral("fail");
}

QStringList fileNames(const QStringList &paths)
{
    QStringList names;
    for (const QString &path : paths)
        names.append(QFileInfo(path).fileName());
    return names;
}

QWidget *makeRow(const JournalEntry &entry)
{
    const OutcomeText outcome = describeOutcome(entry.status, entry.sipStatus, entry.pagesTransferred, entry.pages);
    auto *row = new QWidget;
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(14);

    auto *text = new QVBoxLayout;
    text->setSpacing(1);
    auto *name = new QLabel(entry.displayName());
    name->setStyleSheet(QStringLiteral("font-weight: 500; background: transparent;"));
    auto *docs = new QLabel(QStringLiteral("%1, %2").arg(fileNames(entry.documents).join(QStringLiteral(", ")),
                                                         pagesText(entry.pages)));
    docs->setProperty("role", QStringLiteral("muted"));
    docs->setStyleSheet(QStringLiteral("background: transparent;"));
    text->addWidget(name);
    text->addWidget(docs);
    layout->addLayout(text, 1);

    auto *pill = new QLabel(outcome.title);
    pill->setProperty("pill", pillFor(outcome.severity));
    layout->addWidget(pill, 0, Qt::AlignVCenter);

    // The day is in the heading above the row.
    auto *time = new QLabel(entry.time.toString(QStringLiteral("HH:mm")));
    time->setProperty("role", QStringLiteral("muted"));
    time->setStyleSheet(QStringLiteral("background: transparent;"));
    time->setMinimumWidth(56);
    time->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(time);
    return row;
}

QWidget *makeDayHeading(const QDate &date)
{
    auto *label = new QLabel(dayHeading(date));
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->setContentsMargins(14, 12, 14, 2);
    label->setStyleSheet(QStringLiteral("color: #6b6075; font-size: 13px; font-weight: 600; background: transparent;"));
    return label;
}

} // namespace

JournalPage::JournalPage(Journal *journal, QWidget *parent)
    : QWidget(parent)
    , m_journal(journal)
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

    // --- Empty state -----------------------------------------------------------------
    auto *empty = new QWidget;
    auto *emptyLayout = new QVBoxLayout(empty);
    emptyLayout->addStretch(1);
    auto *emptyTitle = new QLabel(QStringLiteral("Noch keine Faxe gesendet"));
    emptyTitle->setProperty("role", QStringLiteral("sectionTitle"));
    emptyTitle->setAlignment(Qt::AlignHCenter);
    auto *emptyText = new QLabel(QStringLiteral("Hier erscheint jedes Fax mit seinem Ergebnis und Sendebericht."));
    emptyText->setProperty("role", QStringLiteral("subtitle"));
    emptyText->setAlignment(Qt::AlignHCenter);
    emptyText->setWordWrap(true);
    auto *emptyButton = new QPushButton(QStringLiteral("Neues Fax"));
    emptyButton->setProperty("role", QStringLiteral("primary"));
    emptyLayout->addWidget(emptyTitle);
    emptyLayout->addWidget(emptyText);
    emptyLayout->addSpacing(12);
    emptyLayout->addWidget(emptyButton, 0, Qt::AlignHCenter);
    emptyLayout->addStretch(2);
    m_stack->addWidget(empty);

    // --- List and details --------------------------------------------------------------
    auto *main = new QWidget;
    m_columns = new QBoxLayout(QBoxLayout::LeftToRight, main);
    m_columns->setContentsMargins(0, 0, 0, 0);
    m_columns->setSpacing(24);

    auto *listColumn = new QVBoxLayout;
    listColumn->setSpacing(16);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(QStringLiteral("Gesendet"));
    title->setProperty("role", QStringLiteral("title"));
    header->addWidget(title, 1);
    m_search = new QLineEdit;
    m_search->setPlaceholderText(QStringLiteral("Suchen"));
    m_search->setClearButtonEnabled(true);
    m_search->setMinimumWidth(200);
    m_search->setStyleSheet(QStringLiteral("QLineEdit { border-radius: 23px; }"));
    header->addWidget(m_search);
    listColumn->addLayout(header);
    m_list = new QListWidget;
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    listColumn->addWidget(m_list, 1);
    m_columns->addLayout(listColumn, 3);

    auto *detail = new QFrame;
    detail->setProperty("role", QStringLiteral("card"));
    detail->setMinimumWidth(320);
    detail->setMaximumWidth(420);
    m_detailCard = detail;
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(26, 26, 26, 26);
    detailLayout->setSpacing(14);
    // The recipient as the heading, the outcome below it.
    m_detailTitle = new QLabel;
    m_detailTitle->setProperty("role", QStringLiteral("sectionTitle"));
    m_detailTitle->setWordWrap(true);
    m_detailTitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailPill = new QLabel;
    m_detailText = new QLabel;
    m_detailText->setProperty("role", QStringLiteral("subtitle"));
    m_detailText->setWordWrap(true);
    detailLayout->addWidget(m_detailTitle);
    detailLayout->addWidget(m_detailPill, 0, Qt::AlignLeft);
    detailLayout->addWidget(m_detailText);
    auto *divider = new QFrame;
    divider->setProperty("role", QStringLiteral("divider"));
    detailLayout->addWidget(divider);
    m_facts = new QGridLayout;
    m_facts->setHorizontalSpacing(16);
    m_facts->setVerticalSpacing(12);
    detailLayout->addLayout(m_facts);
    auto *divider2 = new QFrame;
    divider2->setProperty("role", QStringLiteral("divider"));
    detailLayout->addWidget(divider2);
    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    m_resend = new QPushButton(QStringLiteral("Erneut senden"));
    m_resend->setProperty("role", QStringLiteral("primary"));
    m_report = new QPushButton(QStringLiteral("Sendebericht"));
    m_report->setToolTip(QStringLiteral("Sendebericht als PDF speichern"));
    buttons->addWidget(m_resend, 1);
    buttons->addWidget(m_report, 1);
    detailLayout->addLayout(buttons);
    m_remove = new QPushButton(QStringLiteral("Aus der Liste entfernen"));
    m_remove->setProperty("role", QStringLiteral("link"));
    detailLayout->addWidget(m_remove, 0, Qt::AlignLeft);
    detailLayout->addStretch(1);
    m_columns->addWidget(detail, 2, Qt::AlignTop);
    m_stack->addWidget(main);

    connect(emptyButton, &QPushButton::clicked, this, &JournalPage::newFaxRequested);
    connect(m_journal, &Journal::changed, this, &JournalPage::rebuild);
    connect(m_search, &QLineEdit::textChanged, this, &JournalPage::rebuild);
    connect(m_list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *item) {
        if (item)
            showDetails(item->data(Qt::UserRole).toString());
    });
    connect(m_resend, &QPushButton::clicked, this, [this] {
        if (const JournalEntry *entry = m_journal->find(m_selectedId))
            emit resendRequested(*entry);
    });
    connect(m_report, &QPushButton::clicked, this, &JournalPage::saveReport);
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const QString id = m_selectedId;
        m_selectedId.clear();
        m_journal->remove(id);
    });

    rebuild();
}

void JournalPage::selectEntry(const QString &id)
{
    m_selectedId = id;
    m_search->clear();
    rebuild();
}

void JournalPage::rebuild()
{
    const QString filter = m_search->text().trimmed();
    const QString keep = m_selectedId;
    m_stack->setCurrentIndex(m_journal->entries().isEmpty() ? 0 : 1);

    const QSignalBlocker blocker(m_list);
    m_list->clear();
    QListWidgetItem *selected = nullptr;
    QListWidgetItem *first = nullptr;
    QDate day;
    for (const JournalEntry &entry : m_journal->entries()) {
        if (!filter.isEmpty()) {
            const QString haystack = QStringLiteral("%1 %2 %3 %4").arg(
                entry.recipientName, entry.destination, fileNames(entry.documents).join(QLatin1Char(' ')),
                entry.location);
            if (!haystack.contains(filter, Qt::CaseInsensitive))
                continue;
        }
        // A heading whenever the day changes; headings cannot be selected.
        if (entry.time.date() != day) {
            day = entry.time.date();
            auto *heading = new QListWidgetItem(m_list);
            heading->setFlags(Qt::NoItemFlags);
            heading->setSizeHint(QSize(0, 36));
            m_list->setItemWidget(heading, makeDayHeading(day));
        }
        auto *item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, entry.id);
        item->setSizeHint(QSize(0, 64));
        m_list->setItemWidget(item, makeRow(entry));
        if (!first)
            first = item;
        if (entry.id == keep)
            selected = item;
    }
    if (!selected)
        selected = first;
    if (selected) {
        m_list->setCurrentItem(selected);
        showDetails(selected->data(Qt::UserRole).toString());
    } else {
        m_selectedId.clear();
    }
    m_detailCard->setVisible(selected != nullptr);
}

void JournalPage::showDetails(const QString &id)
{
    const JournalEntry *entry = m_journal->find(id);
    if (!entry)
        return;
    m_selectedId = id;
    const OutcomeText outcome =
        describeOutcome(entry->status, entry->sipStatus, entry->pagesTransferred, entry->pages);
    m_detailTitle->setText(entry->displayName());
    m_detailPill->setText(outcome.title);
    setStyleProperty(m_detailPill, "pill", pillFor(outcome.severity));
    const QString when = QStringLiteral("%1 um %2 Uhr")
                             .arg(entry->time.toString(QStringLiteral("dd.MM.yyyy")),
                                  entry->time.toString(QStringLiteral("HH:mm")));
    if (outcome.severity == Severity::Ok)
        m_detailText->setText(QStringLiteral("Zugestellt am %1.").arg(when));
    else
        m_detailText->setText(QStringLiteral("%1\nGesendet am %2.").arg(outcome.detail, when));
    m_detailText->setToolTip(entry->message);

    while (QLayoutItem *item = m_facts->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    // Only what is known; empty values are left out instead of shown as "–".
    QList<QPair<QString, QString>> facts = {{QStringLiteral("Nummer"), entry->destination}};
    if (!entry->location.isEmpty())
        facts.append({QStringLiteral("Standort"), entry->location});
    facts.append({QStringLiteral("Seiten"), QStringLiteral("%1 von %2").arg(entry->pagesTransferred).arg(entry->pages)});
    if (!entry->documents.isEmpty())
        facts.append({QStringLiteral("Dokument"), fileNames(entry->documents).join(QStringLiteral(", "))});
    if (entry->durationMs > 0)
        facts.append({QStringLiteral("Dauer"), formatDuration(entry->durationMs)});
    if (entry->bitRate > 0)
        facts.append({QStringLiteral("Geschwindigkeit"), QStringLiteral("%1 bit/s").arg(entry->bitRate)});
    if (!entry->remoteIdent.isEmpty())
        facts.append({QStringLiteral("Gegenstelle"), entry->remoteIdent});
    for (int i = 0; i < facts.size(); ++i) {
        auto *cell = new QWidget;
        auto *cellLayout = new QVBoxLayout(cell);
        cellLayout->setContentsMargins(0, 0, 0, 0);
        cellLayout->setSpacing(2);
        auto *label = new QLabel(facts.at(i).first);
        label->setProperty("role", QStringLiteral("muted"));
        auto *value = new QLabel(facts.at(i).second);
        value->setStyleSheet(QStringLiteral("font-weight: 500; font-size: 14px;"));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        cellLayout->addWidget(label);
        cellLayout->addWidget(value);
        m_facts->addWidget(cell, i / 2, i % 2);
    }
    m_resend->setText(outcome.severity == Severity::Ok ? QStringLiteral("Nochmals senden")
                                                       : QStringLiteral("Erneut senden"));
}

void JournalPage::saveReport()
{
    const JournalEntry *entry = m_journal->find(m_selectedId);
    if (!entry)
        return;
    const QString suggested =
        QStringLiteral("%1/Sendebericht_%2_%3.pdf")
            .arg(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                 entry->time.toString(QStringLiteral("yyyy-MM-dd_HHmm")), normalizeNumber(entry->destination));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Sendebericht speichern"), suggested,
                                                      QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!writeSendReport(*entry, path, &error))
        QMessageBox::warning(this, QStringLiteral("Sendebericht"), error);
}

void JournalPage::resizeEvent(QResizeEvent *event)
{
    m_columns->setDirection(width() < 860 ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    QWidget::resizeEvent(event);
}

} // namespace faxwerk::gui
