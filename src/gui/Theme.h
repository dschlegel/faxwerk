#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

class QApplication;
class QWidget;

namespace faxwerk::gui {

// Color roles of the interface in three sets: light, dark ("Aubergine": the
// aubergine and lilac of the brand on dark, slightly violet surfaces) and high
// contrast (white text on near black, bright lilac actions with dark text).
// The values are in resources/colors.ini, one section per set; faxwerk.qss
// and inline style sheets name them as @role (see themed()).
struct ThemeColors
{
    QColor window;          // page background
    QColor surface;         // cards, inputs, lists
    QColor text;
    QColor muted;           // secondary text
    QColor accentText;      // navigation, field labels, secondary buttons
    QColor deep;            // dark cards (sending progress, setup help)
    QColor onDark;          // text on deep
    QColor onDarkMuted;     // secondary text on deep
    QColor lilac;           // chips, selected navigation, info banners
    QColor lilacHover;
    QColor lilacSoft;       // hover and selection backgrounds
    QColor border;
    QColor divider;
    QColor scroll;          // scroll bars, sheet outlines
    QColor primary;         // main action
    QColor primaryHover;
    QColor primaryPressed;
    QColor primaryDisabled;
    QColor primaryDisabledText;
    QColor onPrimary;       // text on primary
    QColor link;
    QColor focus;           // focus ring of the main action
    QColor focusBorder;     // focus ring of other controls
    QColor dashed;          // "add PDF" tile
    QColor disabledBg;
    QColor disabledText;
    QColor navDisabled;
    QColor pillBg;          // current step on deep
    QColor pillText;
    QColor okBg, okText;
    QColor warnBg, warnText;
    QColor failBg, failText;
    QColor success;         // status dots and check marks
    QColor warning;
    QColor error;
    QColor searching;
};

enum class Appearance { System, Light, Dark, HighContrast };

const ThemeColors &lightColors();
const ThemeColors &darkColors();
const ThemeColors &contrastColors();
// Problems in the text of a colors.ini: lines that cannot be read and roles a
// section leaves out. Empty when the file is complete.
QStringList checkColorFile(const QString &text);
// The colors in use now.
const ThemeColors &colors();
bool isDarkTheme(); // dark or high contrast

// Replaces @role names in a style sheet with the current colors.
QString themed(const QString &styleSheet);
// Sets a style sheet that uses @role names; it is themed again whenever the
// theme changes.
void setThemedStyleSheet(QWidget *widget, const QString &styleSheet);

// Sets the application font, palette and style sheet. Appearance::System
// follows the light or dark mode of the operating system, also when it
// changes while the app runs, and uses high contrast where the system asks
// for more contrast in dark mode (Qt 6.10+).
void applyTheme(QApplication &app, Appearance appearance = Appearance::System);
// Switches between system, light and dark at runtime.
void setAppearance(Appearance appearance);

QString appearanceName(Appearance appearance); // "system", "light", "dark", "contrast"
Appearance appearanceFromName(const QString &name);         // unknown: System

} // namespace faxwerk::gui
