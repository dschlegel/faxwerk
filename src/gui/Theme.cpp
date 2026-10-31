#include "Theme.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QPalette>
#include <QStyleFactory>

namespace faxwerk::gui {

namespace {

// Large type reads too loose at the default letter spacing: tighten it in
// proportion to the size (about -2 % from 24 px up), leave body text alone.
void tightenLargeText(QLabel *label)
{
    QFont font = label->font();
    const int size = font.pixelSize() > 0 ? font.pixelSize() : qRound(font.pointSizeF() * 96.0 / 72.0);
    const qreal spacing = size >= 24 ? -0.02 * size : 0.0;
    if (qFuzzyCompare(font.letterSpacing() + 1.0, spacing + 1.0)
        && (spacing == 0.0 || font.letterSpacingType() == QFont::AbsoluteSpacing))
        return;
    font.setLetterSpacing(QFont::AbsoluteSpacing, spacing);
    label->setFont(font);
}

// Applies the parts of the theme a style sheet cannot express.
class ThemeFilter : public QObject
{
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::Polish) {
            // Buttons take the focus only from the keyboard, so the focus
            // ring of the style sheet appears for keyboard use and not after
            // every click (Qt has no :focus-visible).
            if (auto *button = qobject_cast<QAbstractButton *>(watched)) {
                if (button->focusPolicy() == Qt::StrongFocus)
                    button->setFocusPolicy(Qt::TabFocus);
            }
        }
        if (event->type() == QEvent::Polish || event->type() == QEvent::FontChange) {
            if (auto *label = qobject_cast<QLabel *>(watched))
                tightenLargeText(label);
        }
        return false;
    }
};

} // namespace

void applyTheme(QApplication &app)
{
    // Fusion renders style sheets the same way on every platform.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // The system font (SF Pro on macOS, Segoe UI on Windows, the desktop's
    // font on Linux) at the size the layout is designed for.
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
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

    app.installEventFilter(new ThemeFilter(&app));

    QFile qss(QStringLiteral(":/faxwerk/faxwerk.qss"));
    if (qss.open(QIODevice::ReadOnly))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));
}

} // namespace faxwerk::gui
