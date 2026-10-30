#include "Messages.h"

namespace faxwerk::gui {

OutcomeText describeOutcome(FaxStatus status, int sipStatus, int pagesTransferred, int pages)
{
    switch (status) {
    case FaxStatus::Success:
        return {QStringLiteral("Zugestellt"),
                QStringLiteral("Alle %1 wurden übertragen.").arg(pagesText(pages > 0 ? pages : pagesTransferred)),
                Severity::Ok};
    case FaxStatus::InvalidRequest:
        return {QStringLiteral("Nicht gesendet"),
                QStringLiteral("Es fehlt eine Faxnummer oder ein Dokument."), Severity::Failure};
    case FaxStatus::DocumentError:
        return {QStringLiteral("PDF nicht lesbar"),
                QStringLiteral("Das PDF konnte nicht in Faxseiten umgewandelt werden. Ist es beschädigt oder "
                               "mit einem Kennwort geschützt?"),
                Severity::Failure};
    case FaxStatus::NetworkError:
        return {QStringLiteral("Box nicht erreichbar"),
                QStringLiteral("Die FRITZ!Box antwortet nicht. Ist der Computer im selben Netzwerk und die "
                               "Adresse in den Einstellungen richtig?"),
                Severity::Failure};
    case FaxStatus::AuthenticationFailed:
        return {QStringLiteral("Anmeldung abgelehnt"),
                QStringLiteral("Benutzername oder Kennwort stimmen nicht. Prüfen Sie die Zugangsdaten des "
                               "IP-Telefons in den Einstellungen."),
                Severity::Failure};
    case FaxStatus::RegistrationFailed:
        return {QStringLiteral("Anmeldung fehlgeschlagen"),
                QStringLiteral("Die FRITZ!Box hat die Anmeldung nicht angenommen. Ist das IP-Telefon in der "
                               "Box eingerichtet?"),
                Severity::Failure};
    case FaxStatus::CallFailed:
        switch (sipStatus) {
        case 486:
        case 600:
            return {QStringLiteral("Besetzt"),
                    QStringLiteral("Die Gegenstelle war besetzt. Versuchen Sie es in ein paar Minuten erneut."),
                    Severity::Warning};
        case 404:
        case 484:
            return {QStringLiteral("Nummer nicht gefunden"),
                    QStringLiteral("Unter dieser Nummer ist niemand erreichbar. Prüfen Sie die Faxnummer."),
                    Severity::Failure};
        case 603:
            return {QStringLiteral("Anruf abgelehnt"),
                    QStringLiteral("Die Gegenstelle hat den Anruf abgelehnt."), Severity::Failure};
        default:
            return {QStringLiteral("Niemand hat abgenommen"),
                    QStringLiteral("Das Faxgerät hat nicht geantwortet. Versuchen Sie es später erneut."),
                    Severity::Warning};
        }
    case FaxStatus::FaxFailed:
        if (pagesTransferred <= 0)
            return {QStringLiteral("Kein Faxgerät erreicht"),
                    QStringLiteral("Der Anruf kam an, aber es hat kein Faxgerät geantwortet. Ist die Nummer "
                                   "eine Faxnummer?"),
                    Severity::Failure};
        return {QStringLiteral("Unvollständig"),
                QStringLiteral("Die Übertragung brach nach %1 von %2 ab. Senden Sie das Fax erneut.")
                    .arg(pagesTransferred)
                    .arg(pagesText(pages)),
                Severity::Failure};
    case FaxStatus::Timeout:
        return {QStringLiteral("Zeit überschritten"),
                QStringLiteral("Das Senden hat zu lange gedauert und wurde beendet."), Severity::Warning};
    case FaxStatus::Aborted:
        return {QStringLiteral("Abgebrochen"), QStringLiteral("Sie haben das Senden abgebrochen."),
                Severity::Warning};
    case FaxStatus::InternalError:
        break;
    }
    return {QStringLiteral("Fehler"),
            QStringLiteral("Beim Senden ist ein interner Fehler aufgetreten."), Severity::Failure};
}

QString normalizeNumber(const QString &number)
{
    QString result;
    for (const QChar c : number) {
        if (c.isDigit() || (c == QLatin1Char('+') && result.isEmpty()) || c == QLatin1Char('*')
            || c == QLatin1Char('#'))
            result.append(c);
    }
    return result;
}

QString shortTime(const QDateTime &time, const QDateTime &now)
{
    const qint64 days = time.date().daysTo(now.date());
    if (days == 0)
        return time.toString(QStringLiteral("HH:mm"));
    if (days == 1)
        return QStringLiteral("gestern");
    return time.toString(QStringLiteral("dd.MM."));
}

QString formatDuration(qint64 ms)
{
    const qint64 seconds = (ms + 500) / 1000;
    if (seconds < 60)
        return QStringLiteral("%1 s").arg(seconds);
    return QStringLiteral("%1:%2 min").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString pagesText(int pages)
{
    return pages == 1 ? QStringLiteral("1 Seite") : QStringLiteral("%1 Seiten").arg(pages);
}

} // namespace faxwerk::gui
