// Desktop application.
//
//   faxwerk-gui [file.pdf ...]        (Faxwerk.app on macOS)

#include "MainWindow.h"
#include "Theme.h"
#include "Widgets.h"

#include "ProductInfo.h"

#include <QApplication>
#include <QFileOpenEvent>
#include <QPointer>

using namespace faxwerk::gui;

namespace {

// macOS hands documents opened with the app (Finder, "Open with", dropping on
// the Dock icon) over as QFileOpenEvent instead of command line arguments.
class Application : public QApplication
{
public:
    using QApplication::QApplication;

    void setWindow(MainWindow *window)
    {
        m_window = window;
        if (m_window && !m_pending.isEmpty()) {
            m_window->openDocuments(m_pending);
            m_pending.clear();
        }
    }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::FileOpen) {
            const QString file = static_cast<QFileOpenEvent *>(event)->file();
            if (m_window)
                m_window->openDocuments({file});
            else
                m_pending.append(file);
            return true;
        }
        return QApplication::event(event);
    }

private:
    QPointer<MainWindow> m_window;
    QStringList m_pending;
};

} // namespace

int main(int argc, char **argv)
{
    Application app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral(FAXWERK_PRODUCT_NAME));
    QCoreApplication::setApplicationVersion(QStringLiteral(FAXWERK_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral("faxwerk"));
    QApplication::setWindowIcon(appIcon());
    applyTheme(app);

    MainWindow window;
    window.resize(1180, 780);
    window.show();
    window.openDocuments(QCoreApplication::arguments().mid(1));
    app.setWindow(&window);
    return QApplication::exec();
}
