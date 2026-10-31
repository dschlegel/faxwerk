#pragma once

#include <QColor>

class QApplication;

namespace faxwerk::gui {

// Colors taken from the Sonara theme: aubergine primary on cream.
namespace color {
inline const QColor primary{0x7a, 0x5a, 0x8b};
inline const QColor primaryHover{0x6a, 0x4a, 0x7b};
inline const QColor primaryDeep{0x4f, 0x36, 0x61};
inline const QColor cream{0xfa, 0xf5, 0xf0};
inline const QColor lilac{0xeb, 0xe0, 0xf0};
inline const QColor lilacSoft{0xf5, 0xec, 0xf2};
inline const QColor border{0xe2, 0xd2, 0xec};
inline const QColor text{0x1f, 0x1d, 0x24};
inline const QColor textMuted{0x6b, 0x60, 0x75};
inline const QColor success{0x2f, 0x7d, 0x5b};
inline const QColor warning{0x9a, 0x4a, 0x12};
inline const QColor error{0x8e, 0x26, 0x26};
} // namespace color

// Sets the application font, palette and style sheet.
void applyTheme(QApplication &app);

} // namespace faxwerk::gui
