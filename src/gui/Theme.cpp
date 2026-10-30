#include "Theme.h"

#include <QApplication>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace faxwerk::gui {

void applyTheme(QApplication &app)
{
    // Fusion renders style sheets the same way on every platform.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QFont font = app.font();
    font.setFamilies({QStringLiteral("DM Sans"), QStringLiteral("Segoe UI"), QStringLiteral("SF Pro Text"),
                      QStringLiteral("Helvetica Neue"), QStringLiteral("Cantarell"), QStringLiteral("Noto Sans")});
    font.setPixelSize(15);
    app.setFont(font);

    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, color::cream);
    palette.setColor(QPalette::WindowText, color::text);
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, color::lilacSoft);
    palette.setColor(QPalette::Text, color::text);
    palette.setColor(QPalette::PlaceholderText, color::textMuted);
    palette.setColor(QPalette::Button, Qt::white);
    palette.setColor(QPalette::ButtonText, color::primaryDeep);
    palette.setColor(QPalette::Highlight, color::primary);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, color::primaryHover);
    app.setPalette(palette);

    QFile qss(QStringLiteral(":/faxwerk/faxwerk.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
}

} // namespace faxwerk::gui
