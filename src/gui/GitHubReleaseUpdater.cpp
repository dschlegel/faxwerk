#include "GitHubReleaseUpdater.h"

#include "ProductInfo.h"

#include <QDesktopServices>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>

#ifndef FAXWERK_UPDATE_REPOSITORY
#define FAXWERK_UPDATE_REPOSITORY ""
#endif

namespace faxwerk::gui {

namespace {

QString userAgent()
{
    return QStringLiteral("%1/%2").arg(QStringLiteral(FAXWERK_PRODUCT_NAME), QStringLiteral(FAXWERK_VERSION));
}

// "v1.2.3-beta" -> {1, 2, 3}; empty if it does not start with a number.
QList<int> versionParts(QString version)
{
    if (version.startsWith(QLatin1Char('v')) || version.startsWith(QLatin1Char('V')))
        version.remove(0, 1);
    version = version.section(QLatin1Char('-'), 0, 0);
    QList<int> parts;
    for (const QString &part : version.split(QLatin1Char('.'))) {
        bool ok = false;
        const int number = part.toInt(&ok);
        if (!ok || number < 0)
            return {};
        parts.append(number);
    }
    return parts;
}

QString megabytes(qint64 bytes)
{
    return QString::number(double(bytes) / (1024.0 * 1024.0), 'f', 1);
}

bool isDiskImage(const QString &file)
{
    return file.endsWith(QLatin1String(".dmg"), Qt::CaseInsensitive);
}

} // namespace

GitHubReleaseUpdater::GitHubReleaseUpdater(QObject *parent)
    : GitHubReleaseUpdater(QStringLiteral(FAXWERK_UPDATE_REPOSITORY), QStringLiteral(FAXWERK_VERSION), parent)
{
}

GitHubReleaseUpdater::GitHubReleaseUpdater(const QString &repository, const QString &currentVersion,
                                           QObject *parent)
    : UpdateService(parent)
    , m_repository(repository)
    , m_currentVersion(currentVersion)
{
}

GitHubReleaseUpdater::~GitHubReleaseUpdater()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
    }
}

bool GitHubReleaseUpdater::isBusy() const
{
    return m_reply != nullptr;
}

bool GitHubReleaseUpdater::isSupported()
{
    return !QStringLiteral(FAXWERK_UPDATE_REPOSITORY).isEmpty() && !assetSuffixes().isEmpty();
}

QStringList GitHubReleaseUpdater::assetSuffixes()
{
#if defined(Q_OS_WIN)
    return {QStringLiteral("-windows-x64-setup.exe")};
#elif defined(Q_OS_MACOS)
    // The architecture names match uname -m in scripts/package-macos.sh.
    const QString arch = QSysInfo::buildCpuArchitecture() == QLatin1String("arm64") ? QStringLiteral("arm64")
                                                                                     : QStringLiteral("x86_64");
    return {QStringLiteral("-macos-%1.pkg").arg(arch), QStringLiteral("-macos-%1.dmg").arg(arch)};
#else
    return {};
#endif
}

bool GitHubReleaseUpdater::isNewerVersion(const QString &candidate, const QString &current)
{
    QList<int> a = versionParts(candidate);
    QList<int> b = versionParts(current);
    if (a.isEmpty() || b.isEmpty())
        return false;
    while (a.size() < b.size())
        a.append(0);
    while (b.size() < a.size())
        b.append(0);
    return a > b;
}

std::optional<GitHubReleaseUpdater::Release> GitHubReleaseUpdater::parseRelease(const QByteArray &json,
                                                                                const QStringList &suffixes,
                                                                                QString *error)
{
    auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return std::nullopt;
    };
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (!doc.isObject())
        return fail(QStringLiteral("Ungültige Antwort von GitHub: %1").arg(parseError.errorString()));
    const QJsonObject root = doc.object();
    if (root.value(QLatin1String("draft")).toBool() || root.value(QLatin1String("prerelease")).toBool())
        return fail(QStringLiteral("Das Release ist ein Entwurf oder eine Vorabversion."));

    Release release;
    release.version = root.value(QLatin1String("tag_name")).toString();
    if (release.version.startsWith(QLatin1Char('v')) || release.version.startsWith(QLatin1Char('V')))
        release.version.remove(0, 1);
    if (versionParts(release.version).isEmpty())
        return fail(QStringLiteral("Das Release hat keine Versionsnummer."));
    release.page = QUrl(root.value(QLatin1String("html_url")).toString());

    const QJsonArray assets = root.value(QLatin1String("assets")).toArray();
    for (const QString &suffix : suffixes) {
        for (const QJsonValue &value : assets) {
            const QJsonObject asset = value.toObject();
            const QString name = asset.value(QLatin1String("name")).toString();
            if (!name.endsWith(suffix, Qt::CaseInsensitive) || name.contains(QLatin1Char('/'))
                || name.contains(QLatin1Char('\\')))
                continue;
            const QUrl url(asset.value(QLatin1String("browser_download_url")).toString());
            if (url.scheme() != QLatin1String("https"))
                continue;
            release.asset.name = name;
            release.asset.url = url;
            release.asset.size = qint64(asset.value(QLatin1String("size")).toDouble());
            const QString digest = asset.value(QLatin1String("digest")).toString();
            if (digest.startsWith(QLatin1String("sha256:")))
                release.asset.sha256 = digest.mid(7).toLatin1().toLower();
            return release;
        }
    }
    return fail(QStringLiteral("Das Release %1 enthält kein Installationspaket für dieses System.").arg(release.version));
}

void GitHubReleaseUpdater::checkForUpdate()
{
    if (isBusy() || m_repository.isEmpty())
        return;
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(m_repository)));
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setTransferTimeout(30000);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::finished, this, &GitHubReleaseUpdater::onCheckFinished);
}

void GitHubReleaseUpdater::onCheckFinished()
{
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 404) {
        // No release published yet.
        m_release.reset();
        emit checkFinished(false, QString());
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        emit checkFinished(false, reply->errorString());
        return;
    }
    QString error;
    m_release = parseRelease(reply->readAll(), assetSuffixes(), &error);
    if (!m_release) {
        emit checkFinished(false, error);
        return;
    }
    if (!isNewerVersion(m_release->version, m_currentVersion)) {
        m_release.reset();
        emit checkFinished(false, QString());
        return;
    }
    emit checkFinished(true, QString());
}

void GitHubReleaseUpdater::startUpdate()
{
    if (isBusy())
        return;
    if (!m_release) {
        emit updateFinished(false, QStringLiteral("Es ist kein Update bekannt."));
        return;
    }
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                            .filePath(QStringLiteral("%1-update").arg(QStringLiteral(FAXWERK_PRODUCT_NAME)));
    if (!QDir().mkpath(dir)) {
        emit updateFinished(false, QStringLiteral("Der Ordner %1 kann nicht angelegt werden.").arg(dir));
        return;
    }
    const QString path = QDir(dir).filePath(m_release->asset.name);
    m_file = std::make_unique<QSaveFile>(path);
    if (!m_file->open(QIODevice::WriteOnly)) {
        const QString error = m_file->errorString();
        m_file.reset();
        emit updateFinished(false, QStringLiteral("%1 kann nicht geschrieben werden: %2").arg(path, error));
        return;
    }
    m_hash.reset();
    m_downloaded.clear();

    QNetworkRequest request(m_release->asset.url);
    request.setHeader(QNetworkRequest::UserAgentHeader, userAgent());
    request.setRawHeader("Accept", "application/octet-stream");
    // GitHub redirects release downloads to its storage host.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    m_reply = m_network.get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &GitHubReleaseUpdater::onDownloadData);
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        if (total <= 0 && m_release)
            total = m_release->asset.size;
        const double fraction = total > 0 ? double(received) / double(total) : 0.0;
        emit progress(qBound(0.0, fraction, 1.0),
                      QStringLiteral("%1 von %2 MB").arg(megabytes(received), megabytes(total)));
    });
    connect(m_reply, &QNetworkReply::finished, this, &GitHubReleaseUpdater::onDownloadFinished);
    emit progress(0, QString());
}

void GitHubReleaseUpdater::onDownloadData()
{
    if (!m_reply || !m_file)
        return;
    const QByteArray data = m_reply->readAll();
    m_hash.addData(data);
    if (m_file->write(data) != data.size()) {
        const QString error = m_file->errorString();
        // Forget the reply first: abort() emits finished() right away.
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
        reply->deleteLater();
        failDownload(QStringLiteral("Die Datei kann nicht geschrieben werden: %1").arg(error));
    }
}

void GitHubReleaseUpdater::onDownloadFinished()
{
    if (!m_reply)
        return; // already failed
    onDownloadData();
    if (!m_reply)
        return;
    QNetworkReply *reply = m_reply;
    m_reply = nullptr;
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        failDownload(reply->errorString());
        return;
    }
    if (m_release->asset.size > 0 && m_file->size() != m_release->asset.size) {
        failDownload(QStringLiteral("Der Download ist unvollständig (%1 statt %2 Bytes).")
                         .arg(m_file->size())
                         .arg(m_release->asset.size));
        return;
    }
    if (!m_release->asset.sha256.isEmpty() && m_hash.result().toHex() != m_release->asset.sha256) {
        failDownload(QStringLiteral("Die Prüfsumme des Downloads stimmt nicht."));
        return;
    }
    const QString path = m_file->fileName();
    if (!m_file->commit()) {
        const QString error = m_file->errorString();
        m_file.reset();
        failDownload(QStringLiteral("Die Datei kann nicht gespeichert werden: %1").arg(error));
        return;
    }
    m_file.reset();
    m_downloaded = path;
    emit progress(1, QString());
    emit updateFinished(true, QString());
}

void GitHubReleaseUpdater::failDownload(const QString &error)
{
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
    }
    if (m_file) {
        m_file->cancelWriting();
        m_file.reset();
    }
    emit updateFinished(false, error);
}

bool GitHubReleaseUpdater::launchNewVersion()
{
    if (m_downloaded.isEmpty())
        return false;
    // Through the shell: the Windows installer asks for administrator rights
    // (UAC), the .pkg opens in the macOS installer, the .dmg in the Finder.
    return QDesktopServices::openUrl(QUrl::fromLocalFile(m_downloaded));
}

QString GitHubReleaseUpdater::updateDescription() const
{
    if (!m_release)
        return QString();
    const QString size = m_release->asset.size > 0 ? QStringLiteral(" (%1 MB)").arg(megabytes(m_release->asset.size))
                                                   : QString();
    if (isDiskImage(m_release->asset.name))
        return QStringLiteral("Das Disk-Image der Version %1%2 wird heruntergeladen und anschließend geöffnet.")
            .arg(m_release->version, size);
    return QStringLiteral("Das Installationsprogramm der Version %1%2 wird heruntergeladen und anschließend "
                          "gestartet.")
        .arg(m_release->version, size);
}

QString GitHubReleaseUpdater::launchQuestion() const
{
    const QString product = QStringLiteral(FAXWERK_PRODUCT_NAME);
    if (isDiskImage(m_downloaded))
        return QStringLiteral("Die neue Version ist heruntergeladen. %1 jetzt beenden und das Disk-Image öffnen? "
                              "Ziehe %1 daraus in den Ordner „Programme“.")
            .arg(product);
    return QStringLiteral("Die neue Version ist heruntergeladen. %1 jetzt beenden und das Installationsprogramm "
                          "starten?")
        .arg(product);
}

} // namespace faxwerk::gui
