#pragma once

#include "transport/FaxTransport.h"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QStringList>

namespace faxwerk::gui {

// One send attempt, as shown in the "Gesendet" list.
struct JournalEntry
{
    QString id;
    QDateTime time;
    QString destination;   // number as dialed
    QString recipientName; // optional label typed by the user
    QStringList documents; // absolute PDF paths
    int pages = 0;
    FaxStatus status = FaxStatus::InternalError;
    int sipStatus = 0;
    QString message; // technical outcome text from the core
    int pagesTransferred = 0;
    int bitRate = 0;
    bool ecm = false;
    QString remoteIdent;
    qint64 durationMs = 0;

    QString displayName() const { return recipientName.isEmpty() ? destination : recipientName; }

    QJsonObject toJson() const;
    static JournalEntry fromJson(const QJsonObject &object);
};

struct Recipient
{
    QString number;
    QString name;
};

// Persistent list of sent faxes (JSON file), newest first.
class Journal : public QObject
{
    Q_OBJECT

public:
    explicit Journal(const QString &path, QObject *parent = nullptr);

    static QString defaultPath();

    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;

    const QList<JournalEntry> &entries() const { return m_entries; }
    const JournalEntry *find(const QString &id) const;

    void add(const JournalEntry &entry);
    void remove(const QString &id);

    // Distinct numbers, most recently used first.
    QList<Recipient> recentRecipients(int max) const;

signals:
    void changed();

private:
    QString m_path;
    QList<JournalEntry> m_entries;
};

} // namespace faxwerk::gui
