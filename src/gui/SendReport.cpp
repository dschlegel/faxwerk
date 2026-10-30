#include "SendReport.h"

#include "Messages.h"
#include "Theme.h"

#include "ProductInfo.h"

#include <QFileInfo>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

namespace faxwerk::gui {

bool writeSendReport(const JournalEntry &entry, const QString &path, QString *error)
{
    QPdfWriter writer(path);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setResolution(300);
    writer.setPageMargins(QMarginsF(20, 20, 20, 20), QPageLayout::Millimeter);
    writer.setTitle(QStringLiteral("Sendebericht"));
    writer.setCreator(QStringLiteral("%1 %2").arg(QStringLiteral(FAXWERK_PRODUCT_NAME), QStringLiteral(FAXWERK_VERSION)));

    QPainter painter;
    if (!painter.begin(&writer)) {
        if (error)
            *error = QStringLiteral("Datei kann nicht geschrieben werden: %1").arg(path);
        return false;
    }

    const int width = writer.width();
    auto font = [](int points, bool bold) {
        QFont f(QStringLiteral("DejaVu Sans"));
        f.setFamilies({QStringLiteral("DM Sans"), QStringLiteral("Segoe UI"), QStringLiteral("Helvetica Neue"),
                       QStringLiteral("DejaVu Sans")});
        f.setPointSize(points);
        f.setBold(bold);
        return f;
    };

    int y = 0;
    painter.setPen(color::primaryDeep);
    painter.setFont(font(10, false));
    painter.drawText(QRect(0, y, width, 120), Qt::AlignLeft | Qt::AlignTop, QStringLiteral(FAXWERK_PRODUCT_NAME));
    y += 140;

    painter.setPen(color::text);
    painter.setFont(font(22, true));
    painter.drawText(QRect(0, y, width, 160), Qt::AlignLeft | Qt::AlignTop, QStringLiteral("Sendebericht"));
    y += 200;

    const OutcomeText outcome = describeOutcome(entry.status, entry.sipStatus, entry.pagesTransferred, entry.pages);
    painter.setFont(font(14, true));
    painter.setPen(outcome.severity == Severity::Ok ? color::success : color::error);
    painter.drawText(QRect(0, y, width, 120), Qt::AlignLeft | Qt::AlignTop, outcome.title);
    y += 120;
    painter.setPen(color::text);
    painter.setFont(font(11, false));
    QRect detailRect(0, y, width, 400);
    QRect used = painter.boundingRect(detailRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, outcome.detail);
    painter.drawText(detailRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, outcome.detail);
    y += used.height() + 120;

    QStringList documents;
    for (const QString &doc : entry.documents)
        documents.append(QFileInfo(doc).fileName());

    const QList<QPair<QString, QString>> rows = {
        {QStringLiteral("Empfänger"), entry.recipientName.isEmpty() ? QStringLiteral("–") : entry.recipientName},
        {QStringLiteral("Faxnummer"), entry.destination},
        {QStringLiteral("Zeitpunkt"), entry.time.toString(QStringLiteral("dd.MM.yyyy, HH:mm:ss"))},
        {QStringLiteral("Dokumente"), documents.join(QStringLiteral(", "))},
        {QStringLiteral("Seiten"), QStringLiteral("%1 von %2 übertragen").arg(entry.pagesTransferred).arg(entry.pages)},
        {QStringLiteral("Dauer"), formatDuration(entry.durationMs)},
        {QStringLiteral("Geschwindigkeit"),
         entry.bitRate > 0 ? QStringLiteral("%1 bit/s").arg(entry.bitRate) : QStringLiteral("–")},
        {QStringLiteral("Fehlerkorrektur"), entry.ecm ? QStringLiteral("ja") : QStringLiteral("nein")},
        {QStringLiteral("Kennung Gegenstelle"), entry.remoteIdent.isEmpty() ? QStringLiteral("–") : entry.remoteIdent},
        {QStringLiteral("Technische Meldung"), entry.message},
    };

    const int labelWidth = width / 3;
    painter.setPen(QPen(color::border, 3));
    painter.drawLine(0, y, width, y);
    y += 50;
    for (const auto &row : rows) {
        painter.setFont(font(10, false));
        painter.setPen(color::textMuted);
        painter.drawText(QRect(0, y, labelWidth, 200), Qt::AlignLeft | Qt::AlignTop, row.first);
        painter.setFont(font(11, false));
        painter.setPen(color::text);
        const QRect valueRect(labelWidth, y, width - labelWidth, 600);
        const QRect valueUsed =
            painter.boundingRect(valueRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, row.second);
        painter.drawText(valueRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, row.second);
        y += std::max(valueUsed.height(), 60) + 50;
    }

    painter.end();
    return true;
}

} // namespace faxwerk::gui
