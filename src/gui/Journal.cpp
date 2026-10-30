#include "Journal.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>

namespace faxwerk::gui {

namespace {

constexpr int kMaxEntries = 500;

const FaxStatus kAllStatuses[] = {
    FaxStatus::Success,        FaxStatus::InvalidRequest, FaxStatus::DocumentError, FaxStatus::NetworkError,
    FaxStatus::AuthenticationFailed, FaxStatus::RegistrationFailed, FaxStatus::CallFailed, FaxStatus::FaxFailed,
    FaxStatus::Timeout,        FaxStatus::Aborted,        FaxStatus::InternalError,
};

FaxStatus statusFromName(const QString &name)
{
    for (FaxStatus status : kAllStatuses) {
        if (faxStatusName(status) == name)
            return status;
    }
    return FaxStatus::InternalError;
}

} // namespace

QJsonObject JournalEntry::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), id);
    o.insert(QStringLiteral("time"), time.toString(Qt::ISODate));
    o.insert(QStringLiteral("destination"), destination);
    o.insert(QStringLiteral("name"), recipientName);
    o.insert(QStringLiteral("documents"), QJsonArray::fromStringList(documents));
    o.insert(QStringLiteral("pages"), pages);
    o.insert(QStringLiteral("status"), faxStatusName(status));
    o.insert(QStringLiteral("sipStatus"), sipStatus);
    o.insert(QStringLiteral("message"), message);
    o.insert(QStringLiteral("pagesTransferred"), pagesTransferred);
    o.insert(QStringLiteral("bitRate"), bitRate);
    o.insert(QStringLiteral("ecm"), ecm);
    o.insert(QStringLiteral("remoteIdent"), remoteIdent);
    o.insert(QStringLiteral("durationMs"), double(durationMs));
    return o;
}

JournalEntry JournalEntry::fromJson(const QJsonObject &o)
{
    JournalEntry e;
    e.id = o.value(QStringLiteral("id")).toString();
    e.time = QDateTime::fromString(o.value(QStringLiteral("time")).toString(), Qt::ISODate);
    e.destination = o.value(QStringLiteral("destination")).toString();
    e.recipientName = o.value(QStringLiteral("name")).toString();
    for (const QJsonValue &doc : o.value(QStringLiteral("documents")).toArray())
        e.documents.append(doc.toString());
    e.pages = o.value(QStringLiteral("pages")).toInt();
    e.status = statusFromName(o.value(QStringLiteral("status")).toString());
    e.sipStatus = o.value(QStringLiteral("sipStatus")).toInt();
    e.message = o.value(QStringLiteral("message")).toString();
    e.pagesTransferred = o.value(QStringLiteral("pagesTransferred")).toInt();
    e.bitRate = o.value(QStringLiteral("bitRate")).toInt();
    e.ecm = o.value(QStringLiteral("ecm")).toBool();
    e.remoteIdent = o.value(QStringLiteral("remoteIdent")).toString();
    e.durationMs = qint64(o.value(QStringLiteral("durationMs")).toDouble());
    return e;
}

Journal::Journal(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
{
}

QString Journal::defaultPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
        .filePath(QStringLiteral("journal.json"));
}

bool Journal::load(QString *error)
{
    m_entries.clear();
    QFile file(m_path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = parseError.errorString();
        return false;
    }
    for (const QJsonValue &value : doc.object().value(QStringLiteral("entries")).toArray())
        m_entries.append(JournalEntry::fromJson(value.toObject()));
    emit changed();
    return true;
}

bool Journal::save(QString *error) const
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QSaveFile file(m_path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonArray entries;
    for (const JournalEntry &entry : m_entries)
        entries.append(entry.toJson());
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), entries);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

const JournalEntry *Journal::find(const QString &id) const
{
    for (const JournalEntry &entry : m_entries) {
        if (entry.id == id)
            return &entry;
    }
    return nullptr;
}

void Journal::add(const JournalEntry &entry)
{
    JournalEntry copy = entry;
    if (copy.id.isEmpty())
        copy.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (!copy.time.isValid())
        copy.time = QDateTime::currentDateTime();
    m_entries.prepend(copy);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
    save();
    emit changed();
}

void Journal::remove(const QString &id)
{
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries.at(i).id == id) {
            m_entries.removeAt(i);
            save();
            emit changed();
            return;
        }
    }
}

QList<Recipient> Journal::recentRecipients(int max) const
{
    QList<Recipient> result;
    QSet<QString> seen;
    for (const JournalEntry &entry : m_entries) {
        if (result.size() >= max)
            break;
        if (entry.destination.isEmpty() || seen.contains(entry.destination))
            continue;
        seen.insert(entry.destination);
        result.append({entry.destination, entry.recipientName});
    }
    return result;
}

} // namespace faxwerk::gui
