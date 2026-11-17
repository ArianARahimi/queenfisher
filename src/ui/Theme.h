#pragma once

#include <QColor>

class QApplication;
class QPainter;
class QRect;

// Windows 3.1 shapes (bevels, black outlines, navy title strips) in night colours.
// Every colour lives here; the style sheet refers to them by name.
namespace Theme {

inline const QColor face{0x2d, 0x2f, 0x36};     // panels and buttons
inline const QColor light{0x55, 0x58, 0x64};    // raised edge
inline const QColor shadow{0x16, 0x17, 0x1b};   // sunken edge
inline const QColor outline{0x00, 0x00, 0x00};
inline const QColor text{0xdc, 0xdc, 0xdc};
inline const QColor dim{0x8e, 0x91, 0x9c};
inline const QColor field{0x17, 0x18, 0x1c};    // text boxes, list
inline const QColor fieldAlt{0x1d, 0x1e, 0x23}; // every other list row, read-only boxes
inline const QColor strip{0x10, 0x22, 0x8c};    // title strips
inline const QColor select{0x1f, 0x3a, 0xa8};
inline const QColor running{0x7c, 0xc4, 0xff}; // progress bar while downloading (light blue)
inline const QColor done{0x4c, 0xcf, 0x5a};     // finished
inline const QColor failed{0xe0, 0x4a, 0x4a};   // something went wrong

void apply(QApplication &app);                // style, palette, style sheet
void drawSunken(QPainter &p, const QRect &r, const QColor &fill);
enum class BarState { Waiting, Running, Done, Failed };
void drawBar(QPainter &p, const QRect &r, double fraction, BarState state);

}
