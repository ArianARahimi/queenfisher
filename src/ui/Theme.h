#pragma once

#include <QColor>

class QApplication;
class QPainter;
class QRect;

// Windows Vista era look (glossy gradients, rounded tabs, blue selection, lime green section
// headers) in night colours. Every colour lives here; the style sheet refers to them by name.
namespace Theme {

inline const QColor window{0x1d, 0x1f, 0x24};
inline const QColor panel{0x24, 0x26, 0x2c};
inline const QColor field{0x16, 0x17, 0x1b};    // text boxes, lists
inline const QColor fieldAlt{0x1b, 0x1c, 0x21}; // every other list row
inline const QColor outline{0x0b, 0x0c, 0x0f};
inline const QColor edge{0x3c, 0x3f, 0x48};     // inner highlight line
inline const QColor text{0xe6, 0xe6, 0xe6};
inline const QColor dim{0x9a, 0xa0, 0xab};
inline const QColor accent{0x3c, 0x7f, 0xd9};   // Vista blue: focus, hover, selection
inline const QColor running{0x7c, 0xc4, 0xff};  // progress while downloading (light blue)
inline const QColor done{0x4c, 0xcf, 0x5a};     // finished
inline const QColor failed{0xe0, 0x4a, 0x4a};   // something went wrong

void apply(QApplication &app);   // style, palette, style sheet

enum class BarState { Waiting, Running, Done, Failed };
void drawBar(QPainter &p, const QRect &r, double fraction, BarState state);   // glossy progress bar
void drawGloss(QPainter &p, const QRect &r, const QColor &base, int radius);  // Vista two-tone fill

}
