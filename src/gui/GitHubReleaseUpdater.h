#pragma once

#include "UpdateService.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QStringList>
#include <QUrl>

#include <memory>
#include <optional>

class QNetworkReply;
class QSaveFile;

namespace faxwerk::gui {

// Update for Windows and macOS: asks the GitHub API for the latest release of
// the repository (FAXWERK_UPDATE_REPOSITORY, e.g. "dschlegel/faxwerk"),
// compares its tag (v1.2.3) with the version of this build and downloads the
// installer of this platform from the release:
//   Windows  Faxwerk-<version>-windows-x64-setup.exe
//   macOS    Faxwerk-<version>-macos-<arch>.pkg (or the .dmg)
// launchNewVersion() opens it; the installer replaces the installed copy.
class GitHubReleaseUpdater : public UpdateService
{
    Q_OBJECT

public:
    struct Asset
    {
        QString name;
        QUrl url;
        qint64 size = 0;
        QByteArray sha256; // hex, from the "digest" GitHub computes; may be empty
    };
    struct Release
    {
        QString version; // tag without the leading "v"
        QUrl page;
        Asset asset;
    };

    explicit GitHubReleaseUpdater(QObject *parent = nullptr);
    GitHubReleaseUpdater(const QString &repository, const QString &currentVersion, QObject *parent = nullptr);
    ~GitHubReleaseUpdater() override;

    // The repository is set and there is an installer type for this platform.
    static bool isSupported();

    // File name endings of the installers for this platform, preferred first.
    static QStringList assetSuffixes();
    // Compares dotted version numbers ("v0.10.1" > "0.9"); text after a "-" is
    // ignored. False if either is no version number.
    static bool isNewerVersion(const QString &candidate, const QString &current);
    // Reads a /releases/latest answer. Fails if the JSON is invalid or the
    // release has no asset with one of the suffixes.
    static std::optional<Release> parseRelease(const QByteArray &json, const QStringList &suffixes,
                                               QString *error = nullptr);

    bool isBusy() const override;
    void checkForUpdate() override;
    void startUpdate() override;
    bool isReady() const override { return !m_downloaded.isEmpty(); }
    bool launchNewVersion() override;

    QString availableVersion() const override { return m_release ? m_release->version : QString(); }
    QString updateDescription() const override;
    QString launchQuestion() const override;
    QString downloadedFile() const { return m_downloaded; }

private:
    void onCheckFinished();
    void onDownloadData();
    void onDownloadFinished();
    void failDownload(const QString &error);

    QString m_repository;
    QString m_currentVersion;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    std::optional<Release> m_release;
    std::unique_ptr<QSaveFile> m_file;
    QCryptographicHash m_hash{QCryptographicHash::Sha256};
    QString m_downloaded;
};

} // namespace faxwerk::gui
