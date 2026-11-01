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
#include <QSet>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace faxwerk::gui {

namespace {

constexpr char kThemedStyleProperty[] = "faxwerkThemedStyle";

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

// Reads the [light], [dark] and [contrast] sections of colors.ini: lines
// "name = #rrggbb", comments after ";". Unknown names, colors that cannot be
// read and roles a section leaves out are reported in errors.
QHash<QString, ThemeColors> parseColorFile(const QString &text, QStringList *errors)
{
    QHash<QString, ThemeColors> sets;
    QHash<QString, QSet<QString>> seen;
    QString section;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).section(QLatin1Char(';'), 0, 0).trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2).trimmed();
            sets[section];
            continue;
        }
        const QString name = line.section(QLatin1Char('='), 0, 0).trimmed();
        const QString value = line.section(QLatin1Char('='), 1).trimmed();
        const auto role = roles().constFind(name);
        const QColor color(value);
        if (section.isEmpty() || role == roles().constEnd() || !value.startsWith(QLatin1Char('#'))
            || !color.isValid()) {
            errors->append(QStringLiteral("colors.ini:%1: %2").arg(i + 1).arg(lines.at(i).trimmed()));
            continue;
        }
        sets[section].*(*role) = color;
        seen[section].insert(name);
    }
    for (const QString &name : {QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("contrast")}) {
        for (auto it = roles().constBegin(); it != roles().constEnd(); ++it) {
            if (!seen.value(name).contains(it.key()))
                errors->append(QStringLiteral("colors.ini: [%1] has no %2").arg(name, it.key()));
        }
    }
    return sets;
}

const QHash<QString, ThemeColors> &colorSets()
{
    static const QHash<QString, ThemeColors> sets = [] {
        QFile file(QStringLiteral(":/faxwerk/colors.ini"));
        QStringList errors;
        QHash<QString, ThemeColors> parsed;
        if (file.open(QIODevice::ReadOnly))
            parsed = parseColorFile(QString::fromUtf8(file.readAll()), &errors);
        else
            errors.append(QStringLiteral("colors.ini is missing from the resources"));
        for (const QString &error : std::as_const(errors))
            qWarning("%s", qUtf8Printable(error));
        return parsed;
    }();
    return sets;
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
    static const ThemeColors c = colorSets().value(QStringLiteral("light"));
    return c;
}

const ThemeColors &darkColors()
{
    static const ThemeColors c = colorSets().value(QStringLiteral("dark"));
    return c;
}

const ThemeColors &contrastColors()
{
    static const ThemeColors c = colorSets().value(QStringLiteral("contrast"));
    return c;
}

QStringList checkColorFile(const QString &text)
{
    QStringList errors;
    parseColorFile(text, &errors);
    return errors;
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
