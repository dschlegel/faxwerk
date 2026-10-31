#pragma once

#include <QObject>
#include <QString>

#include <memory>

class QTimer;

namespace appimage::update {
class Updater;
}

namespace faxwerk::gui {

// Updates the running AppImage in place with libappimageupdate: reads the
// update information embedded in the AppImage, compares the file with the
// .zsync file of the latest release and downloads only the changed blocks.
//
// Only available in Linux builds with libappimageupdate (FAXWERK_APPIMAGEUPDATE)
// and only when the program runs from an AppImage ($APPIMAGE is set).
// Everywhere else isSupported() is false and the other functions do nothing.
class AppImageUpdater : public QObject
{
    Q_OBJECT

public:
    explicit AppImageUpdater(QObject *parent = nullptr);
    ~AppImageUpdater() override;

    static bool isSupported();
    // The AppImage file the program was started from, or empty.
    static QString appImagePath();

    bool isBusy() const { return m_checking || m_updater != nullptr; }

    // Asks the update server in the background; emits checkFinished().
    void checkForUpdate();
    // Downloads the new version next to the current AppImage; emits progress()
    // while running and updateFinished() at the end. On success the old file
    // is removed and newAppImagePath() is the file to restart.
    void startUpdate();
    QString newAppImagePath() const { return m_newPath; }

    // Starts the new AppImage. The caller then closes the application.
    bool launchNewVersion() const;

signals:
    // error is empty when the check worked.
    void checkFinished(bool updateAvailable, const QString &error);
    void progress(double fraction, const QString &message);
    void updateFinished(bool ok, const QString &error);

private:
    void poll();
    void finishUpdate();

    bool m_checking = false;
    std::unique_ptr<appimage::update::Updater> m_updater;
    QTimer *m_pollTimer = nullptr;
    QString m_lastMessage;
    QString m_newPath;
};

} // namespace faxwerk::gui
