#pragma once

#include <QColor>
#include <QPixmap>

class QApplication;
class QPainter;
class QRect;

// Windows Vista era look (glossy gradients, rounded tabs) in night colours with one dark alien
// green for every highlight. Every colour lives here; the style sheet refers to them by name.
namespace Theme {

inline const QColor window{0x1d, 0x1f, 0x24};
inline const QColor panel{0x24, 0x26, 0x2c};
inline const QColor field{0x16, 0x17, 0x1b};    // text boxes, lists
inline const QColor fieldAlt{0x1b, 0x1c, 0x21}; // every other list row
inline const QColor outline{0x0b, 0x0c, 0x0f};
inline const QColor edge{0x3c, 0x3f, 0x48};     // inner highlight line
inline const QColor text{0xe6, 0xe6, 0xe6};
inline const QColor dim{0x9a, 0xa0, 0xab};
inline const QColor accent{0x4e, 0x86, 0x1a};   // the green: focus, hover, selection, headers
inline const QColor onAccent{0xff, 0xff, 0xff}; // text on green bars and buttons
inline const QColor onSelect{0x1c, 0x1d, 0x20};  // text on a green selection (jet grey)
inline const QColor glow{0x8c, 0xc4, 0x44};     // green text on dark: wordmark, links, ticks
inline const QColor hover{0x3c, 0x56, 0x22};    // dark green wash behind hovered buttons and tabs
inline const QColor running = accent;           // progress while downloading
inline const QColor done = accent;              // finished
inline const QColor failed{0xe0, 0x4a, 0x4a};   // something went wrong

void apply(QApplication &app);   // style, palette, style sheet

// An icon redrawn in one colour: flat (a silhouette), or keeping its light and shade.
QPixmap recolour(const QString &file, const QColor &colour, bool shaded);

enum class BarState { Waiting, Running, Done, Failed };
void drawBar(QPainter &p, const QRect &r, double fraction, BarState state);   // glossy progress bar
void drawGloss(QPainter &p, const QRect &r, const QColor &base, int radius);  // Vista two-tone fill

}
