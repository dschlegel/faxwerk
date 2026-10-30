// Desktop application.
//
//   Faxwerk [file.pdf ...]

#include "MainWindow.h"
#include "Theme.h"
#include "Widgets.h"

#include "ProductInfo.h"

#include <QApplication>

using namespace faxwerk::gui;

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral(FAXWERK_PRODUCT_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(FAXWERK_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("faxwerk"));
    QApplication::setWindowIcon(appIcon());
    applyTheme(app);

    MainWindow window;
    window.resize(1180, 780);
    window.show();
    window.openDocuments(QCoreApplication::arguments().mid(1));
    return QApplication::exec();
}
