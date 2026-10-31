#include "AppImageUpdater.h"

#include "ProductInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QThread>
#include <QTimer>

#ifdef FAXWERK_HAVE_APPIMAGEUPDATE
#include <appimage/update.h>

#include <exception>
#else
// Only needed so that std::unique_ptr<Updater> can be destroyed.
namespace appimage::update {
class Updater
{
};
} // namespace appimage::update
#endif

namespace faxwerk::gui {

#ifdef FAXWERK_HAVE_APPIMAGEUPDATE

using appimage::update::Updater;

namespace {

QString fromStd(const std::string &text)
{
    return QString::fromLocal8Bit(text.data(), int(text.size()));
}

QString lastStatusMessage(Updater &updater, const QString &previous = QString())
{
    QString last = previous;
    std::string message;
    while (updater.nextStatusMessage(message)) {
        if (!message.empty())
            last = fromStd(message);
    }
    return last;
}

} // namespace

#endif

AppImageUpdater::AppImageUpdater(QObject *parent)
    : UpdateService(parent)
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(250);
    connect(m_pollTimer, &QTimer::timeout, this, &AppImageUpdater::poll);
}

AppImageUpdater::~AppImageUpdater()
{
#ifdef FAXWERK_HAVE_APPIMAGEUPDATE
    // libappimageupdate cannot stop a running download, and its worker thread
    // uses the Updater object: leave it alive when the application quits
    // during an update.
    if (m_updater && !m_updater->isDone())
        (void)m_updater.release();
#endif
}

QString AppImageUpdater::appImagePath()
{
    // Set by the AppImage runtime to the path of the AppImage file.
    const QString path = qEnvironmentVariable("APPIMAGE");
    if (path.isEmpty() || !QFileInfo(path).isFile())
        return QString();
    return path;
}

bool AppImageUpdater::isSupported()
{
#ifdef FAXWERK_HAVE_APPIMAGEUPDATE
    return !appImagePath().isEmpty();
#else
    return false;
#endif
}

void AppImageUpdater::checkForUpdate()
{
#ifdef FAXWERK_HAVE_APPIMAGEUPDATE
    if (isBusy() || !isSupported())
        return;
    m_checking = true;

    // The check downloads the .zsync file: run it outside the GUI thread.
    struct Result
    {
        bool ok = false;
        bool available = false;
        QString error;
    };
    auto result = std::make_shared<Result>();
    const std::string path = QFile::encodeName(appImagePath()).toStdString();
    QThread *thread = QThread::create([result, path] {
        try {
            Updater updater(path);
            bool available = false;
            result->ok = updater.checkForChanges(available);
            result->available = available;
            const QString message = lastStatusMessage(updater);
            if (!result->ok)
                result->error = message;
        } catch (const std::exception &e) {
            result->error = QString::fromLocal8Bit(e.what());
        }
    });
    // finished is emitted in the worker thread and queued to this object; the
    // connection goes away if this object is destroyed first.
    connect(thread, &QThread::finished, this, [this, result] {
        m_checking = false;
        QString error;
        if (!result->ok)
            error = result->error.isEmpty() ? QStringLiteral("Die Update-Prüfung ist fehlgeschlagen.") : result->error;
        emit checkFinished(result->ok && result->available, error);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
#endif
}

void AppImageUpdater::startUpdate()
{
#ifdef FAXWERK_HAVE_APPIMAGEUPDATE
    if (isBusy())
        return;
    if (!isSupported()) {
        emit updateFinished(false, QStringLiteral("Das Programm läuft nicht als AppImage."));
        return;
    }
    m_lastMessage.clear();
    m_newPath.clear();
    try {
        // Not overwriting: the new version gets the file name from the release
        // (Faxwerk-<version>-x86_64.AppImage) next to the current file.
        m_updater = std::make_unique<Updater>(QFile::encodeName(appImagePath()).toStdString(), false);
    } catch (const std::exception &e) {
        emit updateFinished(false, QString::fromLocal8Bit(e.what()));
        return;
    }
    if (!m_updater->start()) {
        m_updater.reset();
        emit updateFinished(false, QStringLiteral("Das Update konnte nicht gestartet werden."));
        return;
    }
    emit progress(0, QString());
    m_pollTimer->start();
#else
    emit updateFinished(false, QStringLiteral("Updates werden in diesem Build nicht unterstützt."));
#endif
}

void AppImageUpdater::poll()
{
#ifdef FAXWERK_HAVE_APPIMAGEUPDATE
    if (!m_updater) {
        m_pollTimer->stop();
        return;
    }
    m_lastMessage = lastStatusMessage(*m_updater, m_lastMessage);
    double fraction = 0;
    m_updater->progress(fraction);
    emit progress(qBound(0.0, fraction, 1.0), m_lastMessage);
    if (m_updater->isDone()) {
        m_pollTimer->stop();
        finishUpdate();
    }
#endif
}

void AppImageUpdater::finishUpdate()
{
#ifdef FAXWERK_HAVE_APPIMAGEUPDATE
    const std::unique_ptr<Updater> updater = std::move(m_updater);
    m_lastMessage = lastStatusMessage(*updater, m_lastMessage);
    if (updater->hasError()) {
        emit updateFinished(false, m_lastMessage.isEmpty() ? QStringLiteral("Das Update ist fehlgeschlagen.")
                                                           : m_lastMessage);
        return;
    }

    std::string newFile;
    if (!updater->pathToNewFile(newFile)) {
        emit updateFinished(false, QStringLiteral("Die neue Version wurde nicht gefunden."));
        return;
    }
    const QString oldPath = QFileInfo(appImagePath()).absoluteFilePath();
    const QString newPath = QFileInfo(QDir(QFileInfo(oldPath).absolutePath()), QFile::decodeName(newFile.c_str()))
                                .absoluteFilePath();

    // Unsigned AppImages pass with a warning; a bad or changed signature
    // puts the old file back.
    const auto validation = updater->validateSignature();
    if (validation >= Updater::VALIDATION_FAILED) {
        const QString reason = fromStd(Updater::signatureValidationMessage(validation));
        try {
            updater->restoreOriginalFile();
        } catch (const std::exception &) {
        }
        emit updateFinished(false, QStringLiteral("Die Signatur der neuen Version ist ungültig (%1).").arg(reason));
        return;
    }
    try {
        updater->copyPermissionsToNewFile();
    } catch (const std::exception &) {
        QFile::setPermissions(newPath, QFile::permissions(newPath) | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    }

    // The running program keeps its mounted image, so the old file can go.
    if (QFileInfo(newPath).canonicalFilePath() != QFileInfo(oldPath).canonicalFilePath())
        QFile::remove(oldPath);
    QFile::remove(newPath + QStringLiteral(".zs-old"));

    m_newPath = newPath;
    emit progress(1, m_lastMessage);
    emit updateFinished(true, QString());
#endif
}

bool AppImageUpdater::launchNewVersion()
{
    if (m_newPath.isEmpty())
        return false;
    return QProcess::startDetached(m_newPath, {});
}

QString AppImageUpdater::updateDescription() const
{
    return QStringLiteral("Die neue Version ersetzt die AppImage-Datei, aus der %1 gestartet wurde.")
        .arg(QStringLiteral(FAXWERK_PRODUCT_NAME));
}

QString AppImageUpdater::launchQuestion() const
{
    return QStringLiteral("Die neue Version ist installiert. %1 jetzt neu starten?")
        .arg(QStringLiteral(FAXWERK_PRODUCT_NAME));
}

} // namespace faxwerk::gui
