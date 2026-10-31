#pragma once

#include <QObject>
#include <QString>

namespace faxwerk::gui {

// Looks for a new version of the application and installs it. One
// implementation per kind of package:
//   AppImageUpdater       Linux AppImage, updated in place with libappimageupdate
//   GitHubReleaseUpdater  Windows and macOS, downloads the installer of the
//                         latest GitHub release
class UpdateService : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // The service for this platform and package, or nullptr if the program
    // cannot update itself (e.g. Linux outside an AppImage, or the
    // environment variable FAXWERK_NO_UPDATE_CHECK is set).
    static UpdateService *create(QObject *parent = nullptr);

    virtual bool isBusy() const = 0;
    // Asks the update server in the background; emits checkFinished().
    virtual void checkForUpdate() = 0;
    // Downloads the new version; emits progress() while running and
    // updateFinished() at the end.
    virtual void startUpdate() = 0;
    // The update was downloaded and can be launched.
    virtual bool isReady() const = 0;
    // After a successful update: starts the new version or its installer.
    // The caller then closes the application.
    virtual bool launchNewVersion() = 0;

    // Version number of the available update, if the server tells it.
    virtual QString availableVersion() const { return QString(); }
    // Shown before the download starts, explains what will happen.
    virtual QString updateDescription() const = 0;
    // Asked after the download: quit and start the new version / installer?
    virtual QString launchQuestion() const = 0;

signals:
    // error is empty when the check worked.
    void checkFinished(bool updateAvailable, const QString &error);
    void progress(double fraction, const QString &message);
    void updateFinished(bool ok, const QString &error);
};

} // namespace faxwerk::gui
