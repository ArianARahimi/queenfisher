#pragma once

#include <QColor>
#include <QFont>

class QApplication;

// Colours and fonts for the retro look. The green, yellow and blue match composersoftware.net.
namespace Theme {

inline const QColor lcdBack{5, 9, 5};
inline const QColor lcdLit{0x55, 0xff, 0x55};
inline const QColor lcdUnlit{0x0b, 0x1a, 0x0b};
inline const QColor lcdHot{0xff, 0xff, 0x55};
inline const QColor bevelLight{0x86, 0x8b, 0xa0};
inline const QColor bevelDark{0x12, 0x13, 0x18};

void apply(QApplication &app);   // loads the font, palette and style sheet
QFont pixelFont();               // IBM VGA 8x16, only crisp at 16 px
void drawInset(class QPainter &p, const QRect &r);  // sunken black panel

}
