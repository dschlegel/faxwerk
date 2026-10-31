#include "Theme.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
#include <QAccessibilityHints>
#endif
#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QLabel>
#include <QPalette>
#include <QPointer>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace faxwerk::gui {

namespace {

constexpr char kThemedStyleProperty[] = "faxwerkThemedStyle";

ThemeColors makeLight()
{
    ThemeColors c;
    c.window = QColor(0xfa, 0xf5, 0xf0);
    c.surface = QColor(0xff, 0xff, 0xff);
    c.text = QColor(0x1f, 0x1d, 0x24);
    c.muted = QColor(0x6b, 0x60, 0x75);
    c.accentText = QColor(0x4f, 0x36, 0x61);
    c.deep = QColor(0x4f, 0x36, 0x61);
    c.onDark = QColor(0xff, 0xff, 0xff);
    c.onDarkMuted = QColor(0xe2, 0xd2, 0xec);
    c.lilac = QColor(0xeb, 0xe0, 0xf0);
    c.lilacHover = QColor(0xe2, 0xd2, 0xec);
    c.lilacSoft = QColor(0xf5, 0xec, 0xf2);
    c.border = QColor(0xe2, 0xd2, 0xec);
    c.divider = QColor(0xef, 0xe6, 0xf3);
    c.scroll = QColor(0xd9, 0xcf, 0xe0);
    c.primary = QColor(0x7a, 0x5a, 0x8b);
    c.primaryHover = QColor(0x6a, 0x4a, 0x7b);
    c.primaryPressed = QColor(0x4f, 0x36, 0x61);
    c.primaryDisabled = QColor(0xcb, 0xbc, 0xd3);
    c.primaryDisabledText = QColor(0xff, 0xff, 0xff);
    c.onPrimary = QColor(0xff, 0xff, 0xff);
    c.link = QColor(0x6a, 0x4a, 0x7b);
    c.focus = QColor(0x1f, 0x1d, 0x24);
    c.focusBorder = QColor(0x4f, 0x36, 0x61);
    c.dashed = QColor(0xb9, 0xa0, 0xc9);
    c.disabledBg = QColor(0xf7, 0xf2, 0xf5);
    c.disabledText = QColor(0xa9, 0x9f, 0xb1);
    c.navDisabled = QColor(0xb3, 0xa8, 0xbb);
    c.pillBg = QColor(0xff, 0xff, 0xff);
    c.pillText = QColor(0x4f, 0x36, 0x61);
    c.okBg = QColor(0xe4, 0xf1, 0xea);
    c.okText = QColor(0x1f, 0x5c, 0x41);
    c.warnBg = QColor(0xfb, 0xee, 0xe4);
    c.warnText = QColor(0x8a, 0x3f, 0x0c);
    c.failBg = QColor(0xf8, 0xe1, 0xe1);
    c.failText = QColor(0x8e, 0x26, 0x26);
    c.success = QColor(0x2f, 0x7d, 0x5b);
    c.warning = QColor(0x9a, 0x4a, 0x12);
    c.error = QColor(0x8e, 0x26, 0x26);
    c.searching = QColor(0xb9, 0xa0, 0xc9);
    return c;
}

ThemeColors makeDark()
{
    ThemeColors c;
    c.window = QColor(0x17, 0x13, 0x1c);
    c.surface = QColor(0x22, 0x1c, 0x29);
    c.text = QColor(0xf1, 0xec, 0xf4);
    c.muted = QColor(0xa4, 0x97, 0xae);
    c.accentText = QColor(0xd9, 0xc6, 0xe6);
    c.deep = QColor(0x2c, 0x21, 0x38);
    c.onDark = QColor(0xff, 0xff, 0xff);
    c.onDarkMuted = QColor(0xc8, 0xb8, 0xd4);
    c.lilac = QColor(0x3a, 0x2d, 0x47);
    c.lilacHover = QColor(0x45, 0x36, 0x54);
    c.lilacSoft = QColor(0x2e, 0x25, 0x37);
    c.border = QColor(0x3a, 0x2f, 0x45);
    c.divider = QColor(0x34, 0x2a, 0x3d);
    c.scroll = QColor(0x4a, 0x3d, 0x55);
    c.primary = QColor(0x9d, 0x7b, 0xb3);
    c.primaryHover = QColor(0xb0, 0x92, 0xc4);
    c.primaryPressed = QColor(0x86, 0x68, 0x9c);
    c.primaryDisabled = QColor(0x4b, 0x3d, 0x57);
    c.primaryDisabledText = QColor(0xc8, 0xb8, 0xd4);
    c.onPrimary = QColor(0xff, 0xff, 0xff);
    c.link = QColor(0xc9, 0xb0, 0xda);
    c.focus = QColor(0xf1, 0xec, 0xf4);
    c.focusBorder = QColor(0xd9, 0xc6, 0xe6);
    c.dashed = QColor(0x6b, 0x56, 0x80);
    c.disabledBg = QColor(0x2a, 0x23, 0x30);
    c.disabledText = QColor(0x6f, 0x63, 0x78);
    c.navDisabled = QColor(0x5f, 0x54, 0x68);
    c.pillBg = QColor(0xe9, 0xdc, 0xf2);
    c.pillText = QColor(0x2c, 0x21, 0x38);
    c.okBg = QColor(0x1b, 0x33, 0x28);
    c.okText = QColor(0x8f, 0xd5, 0xad);
    c.warnBg = QColor(0x3a, 0x28, 0x18);
    c.warnText = QColor(0xf2, 0xb2, 0x7a);
    c.failBg = QColor(0x3d, 0x1d, 0x21);
    c.failText = QColor(0xf2, 0xa0, 0xa0);
    c.success = QColor(0x5c, 0xc0, 0x8f);
    c.warning = QColor(0xe3, 0x9a, 0x5c);
    c.error = QColor(0xec, 0x7f, 0x7f);
    c.searching = QColor(0x8e, 0x74, 0xa3);
    return c;
}

ThemeColors makeContrast()
{
    // Variant C: as much contrast as possible, borders that stay visible,
    // and disabled text that is still readable.
    ThemeColors c;
    c.window = QColor(0x0b, 0x0a, 0x0d);
    c.surface = QColor(0x16, 0x13, 0x1a);
    c.text = QColor(0xff, 0xff, 0xff);
    c.muted = QColor(0xc4, 0xb9, 0xcd);
    c.accentText = QColor(0xec, 0xdf, 0xf6);
    c.deep = QColor(0x1f, 0x17, 0x27);
    c.onDark = QColor(0xff, 0xff, 0xff);
    c.onDarkMuted = QColor(0xdd, 0xd0, 0xe8);
    c.lilac = QColor(0x2d, 0x24, 0x36);
    c.lilacHover = QColor(0x3a, 0x2f, 0x45);
    c.lilacSoft = QColor(0x24, 0x1d, 0x2b);
    c.border = QColor(0x6a, 0x5a, 0x7a);
    c.divider = QColor(0x4a, 0x3e, 0x56);
    c.scroll = QColor(0x6a, 0x5a, 0x7a);
    c.primary = QColor(0xc9, 0xa8, 0xe0);
    c.primaryHover = QColor(0xd9, 0xbf, 0xeb);
    c.primaryPressed = QColor(0xb8, 0x93, 0xd1);
    c.primaryDisabled = QColor(0x3a, 0x2f, 0x45);
    c.primaryDisabledText = QColor(0xc4, 0xb9, 0xcd);
    c.onPrimary = QColor(0x1a, 0x12, 0x20);
    c.link = QColor(0xe0, 0xc8, 0xf0);
    c.focus = QColor(0xff, 0xff, 0xff);
    c.focusBorder = QColor(0xff, 0xff, 0xff);
    c.dashed = QColor(0x9a, 0x84, 0xb0);
    c.disabledBg = QColor(0x1a, 0x16, 0x1e);
    c.disabledText = QColor(0x8f, 0x84, 0x99);
    c.navDisabled = QColor(0x7a, 0x6f, 0x84);
    c.pillBg = QColor(0xff, 0xff, 0xff);
    c.pillText = QColor(0x1f, 0x17, 0x27);
    c.okBg = QColor(0x0f, 0x2b, 0x1d);
    c.okText = QColor(0xa6, 0xf0, 0xc4);
    c.warnBg = QColor(0x33, 0x20, 0x0d);
    c.warnText = QColor(0xff, 0xc5, 0x8f);
    c.failBg = QColor(0x3a, 0x12, 0x15);
    c.failText = QColor(0xff, 0xb3, 0xb3);
    c.success = QColor(0x6f, 0xe0, 0xa5);
    c.warning = QColor(0xff, 0xad, 0x66);
    c.error = QColor(0xff, 0x8a, 0x8a);
    c.searching = QColor(0xa9, 0x8b, 0xc4);
    return c;
}

const QHash<QString, QColor ThemeColors::*> &roles()
{
    static const QHash<QString, QColor ThemeColors::*> map = {
        {QStringLiteral("window"), &ThemeColors::window},
        {QStringLiteral("surface"), &ThemeColors::surface},
        {QStringLiteral("text"), &ThemeColors::text},
        {QStringLiteral("muted"), &ThemeColors::muted},
        {QStringLiteral("accentText"), &ThemeColors::accentText},
        {QStringLiteral("deep"), &ThemeColors::deep},
        {QStringLiteral("onDark"), &ThemeColors::onDark},
        {QStringLiteral("onDarkMuted"), &ThemeColors::onDarkMuted},
        {QStringLiteral("lilac"), &ThemeColors::lilac},
        {QStringLiteral("lilacHover"), &ThemeColors::lilacHover},
        {QStringLiteral("lilacSoft"), &ThemeColors::lilacSoft},
        {QStringLiteral("border"), &ThemeColors::border},
        {QStringLiteral("divider"), &ThemeColors::divider},
        {QStringLiteral("scroll"), &ThemeColors::scroll},
        {QStringLiteral("primary"), &ThemeColors::primary},
        {QStringLiteral("primaryHover"), &ThemeColors::primaryHover},
        {QStringLiteral("primaryPressed"), &ThemeColors::primaryPressed},
        {QStringLiteral("primaryDisabled"), &ThemeColors::primaryDisabled},
        {QStringLiteral("primaryDisabledText"), &ThemeColors::primaryDisabledText},
        {QStringLiteral("onPrimary"), &ThemeColors::onPrimary},
        {QStringLiteral("link"), &ThemeColors::link},
        {QStringLiteral("focus"), &ThemeColors::focus},
        {QStringLiteral("focusBorder"), &ThemeColors::focusBorder},
        {QStringLiteral("dashed"), &ThemeColors::dashed},
        {QStringLiteral("disabledBg"), &ThemeColors::disabledBg},
        {QStringLiteral("disabledText"), &ThemeColors::disabledText},
        {QStringLiteral("navDisabled"), &ThemeColors::navDisabled},
        {QStringLiteral("pillBg"), &ThemeColors::pillBg},
        {QStringLiteral("pillText"), &ThemeColors::pillText},
        {QStringLiteral("okBg"), &ThemeColors::okBg},
        {QStringLiteral("okText"), &ThemeColors::okText},
        {QStringLiteral("warnBg"), &ThemeColors::warnBg},
        {QStringLiteral("warnText"), &ThemeColors::warnText},
        {QStringLiteral("failBg"), &ThemeColors::failBg},
        {QStringLiteral("failText"), &ThemeColors::failText},
        {QStringLiteral("success"), &ThemeColors::success},
        {QStringLiteral("warning"), &ThemeColors::warning},
        {QStringLiteral("error"), &ThemeColors::error},
        {QStringLiteral("searching"), &ThemeColors::searching},
    };
    return map;
}

struct State
{
    QPointer<QApplication> app;
    Appearance appearance = Appearance::System;
    bool dark = false;
    bool contrast = false; // high contrast colors (dark as well)
    bool systemPaletteDark = false; // fallback before Qt 6.5
    QString styleTemplate;
    bool applying = false;
};

State &state()
{
    static State s;
    return s;
}

bool systemPrefersDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
#endif
    return state().systemPaletteDark;
}

bool systemPrefersContrast()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    return QGuiApplication::styleHints()->accessibility()->contrastPreference() == Qt::ContrastPreference::HighContrast;
#else
    return false;
#endif
}

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

void apply()
{
    State &s = state();
    if (!s.app || s.applying)
        return;
    const QScopedValueRollback<bool> guard(s.applying, true);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Native parts (title bar, file dialogs) follow a forced choice too.
    // Unset it first, so that "System" reads the system's own scheme.
    QGuiApplication::styleHints()->setColorScheme(
        s.appearance == Appearance::Dark || s.appearance == Appearance::HighContrast ? Qt::ColorScheme::Dark
        : s.appearance == Appearance::Light                                          ? Qt::ColorScheme::Light
                                                                                     : Qt::ColorScheme::Unknown);
#endif
    const bool systemDark = s.appearance == Appearance::System && systemPrefersDark();
    // High contrast when chosen, or when the system asks for more contrast
    // in dark mode (the light colors already have strong contrast).
    s.contrast = s.appearance == Appearance::HighContrast || (systemDark && systemPrefersContrast());
    s.dark = s.contrast || s.appearance == Appearance::Dark || systemDark;
    const ThemeColors &c = colors();
    QPalette palette = s.app->palette();
    palette.setColor(QPalette::Window, c.window);
    palette.setColor(QPalette::WindowText, c.text);
    palette.setColor(QPalette::Base, c.surface);
    palette.setColor(QPalette::AlternateBase, c.lilacSoft);
    palette.setColor(QPalette::Text, c.text);
    palette.setColor(QPalette::PlaceholderText, c.muted);
    palette.setColor(QPalette::Button, c.surface);
    palette.setColor(QPalette::ButtonText, c.accentText);
    palette.setColor(QPalette::Highlight, c.primary);
    palette.setColor(QPalette::HighlightedText, c.onPrimary);
    palette.setColor(QPalette::Link, c.link);
    palette.setColor(QPalette::ToolTipBase, c.surface);
    palette.setColor(QPalette::ToolTipText, c.text);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, c.disabledText);
    s.app->setPalette(palette);
    s.app->setStyleSheet(themed(s.styleTemplate));

    // Inline style sheets set with setThemedStyleSheet(), and widgets that
    // paint with colors() directly.
    for (QWidget *widget : QApplication::allWidgets()) {
        const QVariant style = widget->property(kThemedStyleProperty);
        if (style.isValid())
            widget->setStyleSheet(themed(style.toString()));
        widget->update();
    }
}

} // namespace

const ThemeColors &lightColors()
{
    static const ThemeColors c = makeLight();
    return c;
}

const ThemeColors &darkColors()
{
    static const ThemeColors c = makeDark();
    return c;
}

const ThemeColors &contrastColors()
{
    static const ThemeColors c = makeContrast();
    return c;
}

const ThemeColors &colors()
{
    return state().contrast ? contrastColors() : state().dark ? darkColors() : lightColors();
}

bool isDarkTheme()
{
    return state().dark;
}

QString themed(const QString &styleSheet)
{
    static const QRegularExpression token(QStringLiteral("@([A-Za-z]+)"));
    const ThemeColors &c = colors();
    QString out;
    qsizetype last = 0;
    auto it = token.globalMatch(styleSheet);
    while (it.hasNext()) {
        const auto match = it.next();
        const auto role = roles().constFind(match.captured(1));
        if (role == roles().constEnd())
            continue;
        out += styleSheet.mid(last, match.capturedStart() - last) + (c.*(*role)).name();
        last = match.capturedEnd();
    }
    out += styleSheet.mid(last);
    return out;
}

void setThemedStyleSheet(QWidget *widget, const QString &styleSheet)
{
    widget->setProperty(kThemedStyleProperty, styleSheet);
    widget->setStyleSheet(themed(styleSheet));
}

void applyTheme(QApplication &app, Appearance appearance)
{
    State &s = state();
    // The platform's own palette tells light or dark where Qt cannot report
    // the color scheme; read it before the theme replaces it.
    s.systemPaletteDark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
    s.app = &app;
    s.appearance = appearance;

    // Fusion renders style sheets the same way on every platform.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    // The system font (SF Pro on macOS, Segoe UI on Windows, the desktop's
    // font on Linux) at the size the layout is designed for.
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    font.setPixelSize(15);
    app.setFont(font);

    app.installEventFilter(new ThemeFilter(&app));

    QFile qss(QStringLiteral(":/faxwerk/faxwerk.qss"));
    if (qss.open(QIODevice::ReadOnly))
        s.styleTemplate = QString::fromUtf8(qss.readAll());

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &app, [] {
        if (state().appearance == Appearance::System)
            apply();
    });
#endif
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    QObject::connect(QGuiApplication::styleHints()->accessibility(), &QAccessibilityHints::contrastPreferenceChanged,
                     &app, [] {
                         if (state().appearance == Appearance::System)
                             apply();
                     });
#endif
    apply();
}

void setAppearance(Appearance appearance)
{
    if (state().appearance == appearance)
        return;
    state().appearance = appearance;
    apply();
}

QString appearanceName(Appearance appearance)
{
    switch (appearance) {
    case Appearance::Light: return QStringLiteral("light");
    case Appearance::Dark: return QStringLiteral("dark");
    case Appearance::HighContrast: return QStringLiteral("contrast");
    case Appearance::System: break;
    }
    return QStringLiteral("system");
}

Appearance appearanceFromName(const QString &name)
{
    if (name == QLatin1String("light"))
        return Appearance::Light;
    if (name == QLatin1String("dark"))
        return Appearance::Dark;
    if (name == QLatin1String("contrast"))
        return Appearance::HighContrast;
    return Appearance::System;
}

} // namespace faxwerk::gui
