#include "ComposePage.h"

#include "Messages.h"
#include "Theme.h"
#include "Widgets.h"

#include "document/PdfRasterizer.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

namespace faxwerk::gui {

namespace {

QLabel *makeLabel(const QString &text, const char *role)
{
    auto *label = new QLabel(text);
    label->setProperty("role", QString::fromLatin1(role));
    return label;
}

constexpr int kMaxSuggestions = 8;
constexpr int kNameRole = Qt::UserRole + 1;
constexpr int kNumberRole = Qt::UserRole + 2;
constexpr int kDetailRole = Qt::UserRole + 3;

// Suggestion row: the name, below it the number, its type and the router.
class ContactDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        painter->save();
        const bool selected = option.state & QStyle::State_Selected;
        if (selected)
            painter->fillRect(option.rect, colors().lilacSoft);
        const QRect text = option.rect.adjusted(14, 7, -14, -7);
        QFont nameFont = option.font;
        nameFont.setWeight(QFont::Medium);
        painter->setFont(nameFont);
        painter->setPen(colors().text);
        const QString name = index.data(kNameRole).toString();
        painter->drawText(text, Qt::AlignLeft | Qt::AlignTop,
                          QFontMetrics(nameFont).elidedText(name, Qt::ElideRight, text.width()));
        QFont detailFont = option.font;
        detailFont.setPixelSize(13);
        painter->setFont(detailFont);
        painter->setPen(colors().muted);
        painter->drawText(text, Qt::AlignLeft | Qt::AlignBottom,
                          QFontMetrics(detailFont).elidedText(index.data(kDetailRole).toString(), Qt::ElideRight,
                                                             text.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        return QSize(QStyledItemDelegate::sizeHint(option, index).width(), 52);
    }
};

QStringList pdfsFrom(const QMimeData *mime)
{
    QStringList files;
    if (!mime || !mime->hasUrls())
        return files;
    for (const QUrl &url : mime->urls()) {
        const QString path = url.toLocalFile();
        if (path.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive))
            files.append(path);
    }
    return files;
}

} // namespace

ComposePage::ComposePage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("page"));
    setAcceptDrops(true);

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);
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

    // --- Left: documents ---------------------------------------------------------
    auto *left = new QVBoxLayout;
    left->setSpacing(18);
    left->addWidget(makeLabel(QStringLiteral("Neues Fax"), "title"));
    auto *subtitle = makeLabel(
        QStringLiteral("So kommt das Dokument beim Empfänger an: schwarz-weiß, in Fax-Auflösung."), "subtitle");
    subtitle->setWordWrap(true);
    left->addWidget(subtitle);

    auto *docCard = new QFrame;
    docCard->setProperty("role", QStringLiteral("card"));
    auto *docLayout = new QVBoxLayout(docCard);
    docLayout->setContentsMargins(24, 24, 24, 20);
    docLayout->setSpacing(16);

    auto *pagesScroll = new QScrollArea;
    pagesScroll->setFrameShape(QFrame::NoFrame);
    pagesScroll->setWidgetResizable(true);
    pagesScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    pagesScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pagesScroll->setFixedHeight(PageThumbnail::kHeight + 52);
    setThemedStyleSheet(pagesScroll,
                        QStringLiteral("QScrollArea, QScrollArea > QWidget > QWidget { background: @surface; }"));
    auto *pagesHost = new QWidget;
    m_pagesRow = new QHBoxLayout(pagesHost);
    m_pagesRow->setContentsMargins(0, 0, 0, 0);
    m_pagesRow->setSpacing(10);
    m_addTile = new QPushButton(QStringLiteral("+\nPDF hinzufügen"));
    m_addTile->setObjectName(QStringLiteral("addPdf"));
    m_addTile->setFixedSize(PageThumbnail::kWidth, PageThumbnail::kHeight);
    m_addTile->setToolTip(QStringLiteral("PDF auswählen oder hierher ziehen"));
    m_pagesRow->addWidget(m_addTile, 0, Qt::AlignTop);
    m_pagesRow->addStretch(1);
    pagesScroll->setWidget(pagesHost);
    docLayout->addWidget(pagesScroll);

    auto *divider = new QFrame;
    divider->setProperty("role", QStringLiteral("divider"));
    docLayout->addWidget(divider);

    auto *fileRow = new QHBoxLayout;
    fileRow->setSpacing(16);
    auto *fileText = new QVBoxLayout;
    fileText->setSpacing(2);
    m_fileTitle = new QLabel(QStringLiteral("Noch kein Dokument"));
    m_fileTitle->setStyleSheet(QStringLiteral("font-weight: 500;"));
    m_fileDetail = makeLabel(QStringLiteral("PDF hierher ziehen oder auswählen"), "muted");
    fileText->addWidget(m_fileTitle);
    fileText->addWidget(m_fileDetail);
    fileRow->addLayout(fileText, 1);
    m_removeAll = new QPushButton(QStringLiteral("Entfernen"));
    m_removeAll->setProperty("role", QStringLiteral("link"));
    m_removeAll->hide();
    fileRow->addWidget(m_removeAll);
    m_dither = new QCheckBox(QStringLiteral("Fotos weich rastern"));
    m_dither->setToolTip(QStringLiteral("Besser für Fotos und Graustufen, schlechter für reinen Text"));
    fileRow->addWidget(m_dither);
    docLayout->addLayout(fileRow);

    m_error = new QLabel;
    m_error->setProperty("banner", QStringLiteral("fail"));
    m_error->setContentsMargins(16, 12, 16, 12);
    m_error->setWordWrap(true);
    m_error->hide();
    docLayout->addWidget(m_error);

    left->addWidget(docCard);
    left->addStretch(1);
    m_columns->addLayout(left, 1);

    // --- Right: recipient and send -----------------------------------------------
    auto *sendCard = new QFrame;
    sendCard->setProperty("role", QStringLiteral("card"));
    sendCard->setMinimumWidth(340);
    sendCard->setMaximumWidth(400);
    m_sendCard = sendCard;
    auto *right = new QVBoxLayout(sendCard);
    right->setContentsMargins(26, 26, 26, 26);
    right->setSpacing(10);

    m_locationRow = new QWidget;
    auto *locationLayout = new QVBoxLayout(m_locationRow);
    locationLayout->setContentsMargins(0, 0, 0, 10);
    locationLayout->setSpacing(6);
    m_location = new QComboBox;
    m_location->setAccessibleName(QStringLiteral("Senden über"));
    auto *locationLabel = makeLabel(QStringLiteral("Senden über"), "fieldLabel");
    locationLabel->setBuddy(m_location);
    locationLayout->addWidget(locationLabel);
    locationLayout->addWidget(m_location);
    m_locationHint = makeLabel(QString(), "muted");
    m_locationHint->hide();
    locationLayout->addWidget(m_locationHint);
    m_locationRow->hide();
    right->addWidget(m_locationRow);

    right->addWidget(makeLabel(QStringLiteral("Empfänger"), "fieldLabel"));
    m_destination = new QLineEdit;
    m_destination->setObjectName(QStringLiteral("destination"));
    m_destination->setPlaceholderText(QStringLiteral("Faxnummer"));
    m_destination->setValidator(new QRegularExpressionValidator(
        QRegularExpression(QStringLiteral("[0-9+ /()*#-]{0,40}")), m_destination));
    m_destination->setAccessibleName(QStringLiteral("Faxnummer des Empfängers"));
    right->addWidget(m_destination);
    m_name = new QLineEdit;
    m_name->setPlaceholderText(QStringLiteral("Name (optional)"));
    m_name->setAccessibleName(QStringLiteral("Name des Empfängers"));
    right->addWidget(m_name);
    m_saveContact = new QPushButton(QStringLiteral("Als Kontakt speichern"));
    m_saveContact->setToolTip(QStringLiteral("Im Telefonbuch der FRITZ!Box speichern"));
    m_saveContact->setProperty("role", QStringLiteral("link"));
    m_saveContact->hide();
    right->addWidget(m_saveContact, 0, Qt::AlignLeft);

    // Phonebook suggestions for both fields; choosing one fills in both.
    m_suggestions = new QStandardItemModel(this);
    for (auto [edit, role] : {std::pair{m_destination, kNumberRole}, std::pair{m_name, kNameRole}}) {
        auto *completer = new QCompleter(m_suggestions, this);
        completer->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
        completer->setCompletionRole(role);
        completer->setMaxVisibleItems(kMaxSuggestions);
        completer->setWidget(edit);
        auto *popup = completer->popup();
        popup->setObjectName(QStringLiteral("contactPopup"));
        popup->setItemDelegate(new ContactDelegate(popup));
        connect(edit, &QLineEdit::textEdited, this, [this, completer](const QString &text) { suggest(completer, text); });
        connect(completer, qOverload<const QModelIndex &>(&QCompleter::activated), this, [this](const QModelIndex &index) {
            m_destination->setText(index.data(kNumberRole).toString());
            m_name->setText(index.data(kNameRole).toString());
            m_destination->setFocus();
        });
        (edit == m_destination ? m_numberCompleter : m_nameCompleter) = completer;
    }

    right->addSpacing(8);
    m_recentTitle = makeLabel(QStringLiteral("Zuletzt verwendet"), "muted");
    right->addWidget(m_recentTitle);
    m_recentList = new QVBoxLayout;
    m_recentList->setSpacing(0);
    right->addLayout(m_recentList);

    right->addSpacing(8);
    auto *divider2 = new QFrame;
    divider2->setProperty("role", QStringLiteral("divider"));
    right->addWidget(divider2);
    m_disclosure = new QToolButton;
    m_disclosure->setObjectName(QStringLiteral("disclosure"));
    m_disclosure->setText(QStringLiteral("Absender und Übertragung"));
    m_disclosure->setCheckable(true);
    m_disclosure->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_disclosure->setArrowType(Qt::RightArrow);
    right->addWidget(m_disclosure);
    m_senderBox = new QWidget;
    auto *senderLayout = new QVBoxLayout(m_senderBox);
    senderLayout->setContentsMargins(0, 0, 0, 0);
    senderLayout->setSpacing(4);
    m_senderSummary = makeLabel(QString(), "muted");
    m_senderSummary->setWordWrap(true);
    m_senderSummary->setStyleSheet(QStringLiteral("font-size: 14px;"));
    senderLayout->addWidget(m_senderSummary);
    auto *changeSettings = new QPushButton(QStringLiteral("In den Einstellungen ändern"));
    changeSettings->setProperty("role", QStringLiteral("link"));
    senderLayout->addWidget(changeSettings, 0, Qt::AlignLeft);
    m_senderBox->hide();
    right->addWidget(m_senderBox);

    right->addSpacing(12);
    // The send button with a line that says what is still missing. In narrow
    // windows it moves into a bar below the page (see resizeEvent), so it
    // never scrolls out of view.
    m_sendArea = new QWidget;
    auto *sendLayout = new QVBoxLayout(m_sendArea);
    sendLayout->setContentsMargins(0, 0, 0, 0);
    sendLayout->setSpacing(8);
    m_sendHint = makeLabel(QString(), "muted");
    m_sendHint->setAlignment(Qt::AlignHCenter);
    sendLayout->addWidget(m_sendHint);
    m_send = new QPushButton(QStringLiteral("Faxen"));
    m_send->setProperty("role", QStringLiteral("primary"));
    m_send->setProperty("size", QStringLiteral("big"));
    m_send->setDefault(true);
    sendLayout->addWidget(m_send);
    right->addWidget(m_sendArea);
    m_sendCardLayout = right;

    // Only as tall as its content, so no gap opens above the button.
    m_columns->addWidget(sendCard, 0, Qt::AlignTop);

    m_footer = new QFrame;
    m_footer->setObjectName(QStringLiteral("sendFooter"));
    m_footerLayout = new QHBoxLayout(m_footer);
    m_footerLayout->setContentsMargins(28, 12, 28, 14);
    m_footer->hide();
    outer->addWidget(m_footer);

    connect(m_addTile, &QPushButton::clicked, this, &ComposePage::chooseDocuments);
    connect(m_removeAll, &QPushButton::clicked, this, [this] {
        m_documents.clear();
        renderPreview();
    });
    connect(m_dither, &QCheckBox::toggled, this, [this] {
        if (!m_documents.isEmpty())
            renderPreview();
    });
    connect(m_destination, &QLineEdit::textChanged, this, &ComposePage::updateSendButton);
    connect(m_destination, &QLineEdit::textChanged, this, &ComposePage::updateSaveContact);
    connect(m_name, &QLineEdit::textChanged, this, &ComposePage::updateSaveContact);
    connect(m_saveContact, &QPushButton::clicked, this, [this] {
        emit saveContactRequested(m_name->text().trimmed(), m_destination->text().trimmed());
    });
    connect(m_destination, &QLineEdit::returnPressed, m_send, &QPushButton::click);
    connect(m_disclosure, &QToolButton::toggled, this, [this](bool open) {
        m_disclosure->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        m_senderBox->setVisible(open);
    });
    connect(changeSettings, &QPushButton::clicked, this, &ComposePage::settingsRequested);
    connect(m_location, &QComboBox::currentIndexChanged, this,
            [this] {
                m_locationHint->setVisible(m_location->currentIndex() == 0 && !m_locationHint->text().isEmpty());
                emit locationChanged(selectedLocationId());
            });
    connect(m_send, &QPushButton::clicked, this, [this] {
        if (!m_send->isEnabled())
            return;
        SendRequest request;
        request.destination = m_destination->text().trimmed();
        request.recipientName = m_name->text().trimmed();
        request.documents = m_documents;
        request.dither = m_dither->isChecked();
        request.pages = int(m_thumbnails.size());
        request.locationId = selectedLocationId();
        emit sendRequested(request);
    });

    renderPreview();
}

void ComposePage::addDocuments(const QStringList &files)
{
    for (const QString &file : files) {
        const QString path = QFileInfo(file).absoluteFilePath();
        if (!m_documents.contains(path))
            m_documents.append(path);
    }
    renderPreview();
}

void ComposePage::load(const SendRequest &request)
{
    m_destination->setText(request.destination);
    m_name->setText(request.recipientName);
    {
        const QSignalBlocker blocker(m_dither);
        m_dither->setChecked(request.dither);
    }
    m_documents = request.documents;
    const int location = m_location->findData(request.locationId);
    if (location >= 0)
        m_location->setCurrentIndex(location);
    renderPreview();
}

void ComposePage::setRecipient(const QString &name, const QString &number)
{
    m_destination->setText(number);
    m_name->setText(name);
}

void ComposePage::clear()
{
    m_destination->clear();
    m_name->clear();
    m_documents.clear();
    renderPreview();
}

void ComposePage::setContacts(const QList<Contact> &contacts, bool canSave)
{
    m_contacts = contacts;
    m_canSaveContacts = canSave;
    updateSaveContact();
}

void ComposePage::suggest(QCompleter *completer, const QString &text)
{
    const QList<Contact> found = text.trimmed().size() >= 2 ? searchContacts(m_contacts, text, kMaxSuggestions)
                                                            : QList<Contact>();
    m_suggestions->clear();
    for (const Contact &contact : found) {
        auto *item = new QStandardItem;
        item->setData(contact.name.isEmpty() ? contact.number : contact.name, kNameRole);
        item->setData(contact.number, kNumberRole);
        QStringList detail{contact.number, contact.typeLabel()};
        if (!contact.locationName.isEmpty())
            detail.append(contact.locationName);
        item->setData(detail.join(QStringLiteral(" · ")), kDetailRole);
        m_suggestions->appendRow(item);
    }
    if (found.isEmpty())
        completer->popup()->hide();
    else
        completer->complete();
}

void ComposePage::updateSaveContact()
{
    const QString number = comparableNumber(m_destination->text());
    bool known = false;
    for (const Contact &contact : std::as_const(m_contacts)) {
        if (comparableNumber(contact.number) == number) {
            known = true;
            break;
        }
    }
    m_saveContact->setVisible(m_canSaveContacts && number.size() >= 3 && !m_name->text().trimmed().isEmpty()
                              && !known);
}

void ComposePage::setRecentRecipients(const QList<Recipient> &recipients)
{
    while (QLayoutItem *item = m_recentList->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    m_recentTitle->setVisible(!recipients.isEmpty());
    for (const Recipient &recipient : recipients) {
        // Name in the text color, the number smaller and muted below it; rows
        // without a name show the number alone, at the same height.
        auto *button = new QPushButton;
        button->setProperty("role", QStringLiteral("recent"));
        button->setFixedHeight(54);
        button->setAccessibleName(recipient.name.isEmpty()
                                      ? recipient.number
                                      : QStringLiteral("%1, %2").arg(recipient.name, recipient.number));
        auto *rowLayout = new QVBoxLayout(button);
        rowLayout->setContentsMargins(14, 7, 14, 7);
        rowLayout->setSpacing(1);
        auto *main = new QLabel(recipient.name.isEmpty() ? recipient.number : recipient.name);
        main->setStyleSheet(QStringLiteral("font-weight: 500; background: transparent;"));
        main->setAttribute(Qt::WA_TransparentForMouseEvents);
        rowLayout->addWidget(main);
        if (!recipient.name.isEmpty()) {
            auto *number = makeLabel(recipient.number, "muted");
            number->setStyleSheet(QStringLiteral("background: transparent;"));
            number->setAttribute(Qt::WA_TransparentForMouseEvents);
            rowLayout->addWidget(number);
        }
        connect(button, &QPushButton::clicked, this, [this, recipient] {
            m_destination->setText(recipient.number);
            m_name->setText(recipient.name);
            m_destination->setFocus();
        });
        m_recentList->addWidget(button);
    }
}

void ComposePage::setSenderSummary(const QString &summary)
{
    m_senderSummary->setText(summary);
}

void ComposePage::setLocations(const QList<Location> &locations, const QString &selectedId)
{
    {
        const QSignalBlocker blocker(m_location);
        m_location->clear();
        m_location->addItem(QStringLiteral("Automatisch"), QString());
        for (const Location &location : locations)
            m_location->addItem(location.displayName(), location.id);
        const int index = m_location->findData(selectedId);
        m_location->setCurrentIndex(index >= 0 ? index : 0);
    }
    m_locationHint->setVisible(m_location->currentIndex() == 0 && !m_locationHint->text().isEmpty());
    m_locationRow->setVisible(locations.size() > 1);
    emit locationChanged(selectedLocationId());
}

void ComposePage::setAutoLocationHint(const QString &hint)
{
    m_locationHint->setText(hint);
    m_locationHint->setVisible(m_location->currentIndex() == 0 && !hint.isEmpty());
}

QString ComposePage::selectedLocationId() const
{
    return m_location->currentData().toString();
}

void ComposePage::setDitherDefault(bool dither)
{
    if (m_documents.isEmpty()) {
        const QSignalBlocker blocker(m_dither);
        m_dither->setChecked(dither);
    }
}

void ComposePage::focusRecipient()
{
    m_destination->setFocus();
    m_destination->selectAll();
}

void ComposePage::chooseDocuments()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("PDF auswählen"),
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        QStringLiteral("PDF-Dokumente (*.pdf)"));
    if (!files.isEmpty())
        addDocuments(files);
}

void ComposePage::renderPreview()
{
    // Remove the old thumbnails, keep the "add" tile and the stretch.
    while (m_pagesRow->count() > 2) {
        QLayoutItem *item = m_pagesRow->takeAt(0);
        delete item->widget();
        delete item;
    }
    m_thumbnails.clear();
    m_error->hide();

    if (!m_documents.isEmpty()) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        PdfRasterizer::Options options;
        options.mode = m_dither->isChecked() ? PdfRasterizer::Mode::Dither : PdfRasterizer::Mode::Threshold;
        QList<QImage> pages;
        QString error;
        if (PdfRasterizer(options).rasterize(m_documents, &pages, &error)) {
            for (const QImage &page : std::as_const(pages))
                m_thumbnails.append(PageThumbnail::makeThumbnail(page));
        } else {
            m_error->setText(QStringLiteral("Das PDF lässt sich nicht öffnen. Ist es beschädigt oder mit einem "
                                            "Kennwort geschützt?"));
            m_error->setToolTip(error);
            m_error->show();
            m_documents.clear();
        }
        QApplication::restoreOverrideCursor();
    }

    for (int i = 0; i < m_thumbnails.size(); ++i) {
        auto *thumb = new PageThumbnail(m_thumbnails.at(i), QStringLiteral("Seite %1").arg(i + 1));
        m_pagesRow->insertWidget(i, thumb, 0, Qt::AlignTop);
    }
    m_addTile->setText(m_documents.isEmpty() ? QStringLiteral("+\nPDF auswählen")
                                             : QStringLiteral("+\nWeitere PDF\nhinzufügen"));

    if (m_documents.isEmpty()) {
        m_fileTitle->setText(QStringLiteral("Noch kein Dokument"));
        m_fileDetail->setText(QStringLiteral("PDF hierher ziehen oder auswählen"));
    } else {
        m_fileTitle->setText(m_documents.size() == 1
                                 ? QFileInfo(m_documents.first()).fileName()
                                 : QStringLiteral("%1 Dokumente").arg(m_documents.size()));
        qint64 bytes = 0;
        for (const QString &doc : std::as_const(m_documents))
            bytes += QFileInfo(doc).size();
        m_fileDetail->setText(QStringLiteral("%1, %2 KB")
                                  .arg(pagesText(int(m_thumbnails.size())))
                                  .arg((bytes + 1023) / 1024));
    }
    m_removeAll->setVisible(!m_documents.isEmpty());
    updateSendButton();
}

void ComposePage::updateSendButton()
{
    const int pages = int(m_thumbnails.size());
    const bool hasNumber = !normalizeNumber(m_destination->text()).isEmpty();
    m_send->setEnabled(pages > 0 && hasNumber);
    if (pages == 0)
        m_send->setText(QStringLiteral("Faxen"));
    else
        m_send->setText(QStringLiteral("%1 faxen").arg(pagesText(pages)));
    // Say what is missing instead of only greying out the button.
    QString hint;
    if (pages == 0 && !hasNumber)
        hint = QStringLiteral("PDF hinzufügen und Faxnummer eingeben");
    else if (pages == 0)
        hint = QStringLiteral("PDF hinzufügen");
    else if (!hasNumber)
        hint = QStringLiteral("Faxnummer eingeben");
    m_sendHint->setText(hint);
    m_sendHint->setVisible(!hint.isEmpty());
}

void ComposePage::dragEnterEvent(QDragEnterEvent *event)
{
    if (!pdfsFrom(event->mimeData()).isEmpty())
        event->acceptProposedAction();
}

void ComposePage::dropEvent(QDropEvent *event)
{
    const QStringList files = pdfsFrom(event->mimeData());
    if (files.isEmpty())
        return;
    event->acceptProposedAction();
    addDocuments(files);
}

void ComposePage::resizeEvent(QResizeEvent *event)
{
    // Stack the send card under the documents in narrow windows.
    const bool narrow = width() < 860;
    m_columns->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_sendCard->setMaximumWidth(narrow ? QWIDGETSIZE_MAX : 400);
    // Narrow windows: the send button stays visible in a bar below the page.
    if (narrow != !m_footer->isHidden()) {
        if (narrow)
            m_footerLayout->addWidget(m_sendArea);
        else
            m_sendCardLayout->addWidget(m_sendArea);
        m_footer->setVisible(narrow);
    }
    QWidget::resizeEvent(event);
}

} // namespace faxwerk::gui
