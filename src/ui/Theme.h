#pragma once

#include <QColor>

class QApplication;
class QPainter;
class QRect;

// Windows 3.1 look: grey face, white/grey bevels, black outlines, navy title strips.
namespace Theme {

inline const QColor face{0xc0, 0xc0, 0xc0};
inline const QColor shadow{0x80, 0x80, 0x80};
inline const QColor navy{0x00, 0x00, 0x80};
inline const QColor empty{0xff, 0xff, 0xff};

void apply(QApplication &app);                // style, palette, fonts, style sheet
void drawSunken(QPainter &p, const QRect &r, const QColor &fill);
void drawBlocks(QPainter &p, const QRect &r, double fraction);  // segmented progress bar

}
