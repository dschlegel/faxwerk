#include "UpdateService.h"

#include "AppImageUpdater.h"
#include "GitHubReleaseUpdater.h"

namespace faxwerk::gui {

UpdateService *UpdateService::create(QObject *parent)
{
    if (qEnvironmentVariableIsSet("FAXWERK_NO_UPDATE_CHECK"))
        return nullptr;
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    if (GitHubReleaseUpdater::isSupported())
        return new GitHubReleaseUpdater(parent);
#else
    if (AppImageUpdater::isSupported())
        return new AppImageUpdater(parent);
#endif
    return nullptr;
}

} // namespace faxwerk::gui
