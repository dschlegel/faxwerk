#pragma once

#include "transport/FaxTransport.h"

#include <QDateTime>
#include <QString>

namespace faxwerk::gui {

enum class Severity { Ok, Warning, Failure };

// User-facing German text for a fax outcome: a short title for lists and a
// sentence that says what happened and what to do next.
struct OutcomeText
{
    QString title;
    QString detail;
    Severity severity = Severity::Failure;
};

OutcomeText describeOutcome(FaxStatus status, int sipStatus, int pagesTransferred, int pages);

// Removes the separators people type into fax numbers ("0761 / 45-67 890").
QString normalizeNumber(const QString &number);

// "14:21", "gestern" or "28.10." relative to now.
QString shortTime(const QDateTime &time, const QDateTime &now = QDateTime::currentDateTime());

// "1:52 min" or "42 s".
QString formatDuration(qint64 ms);

QString pagesText(int pages);

} // namespace faxwerk::gui
