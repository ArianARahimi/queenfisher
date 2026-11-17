#pragma once

#include <QColor>
#include <QPixmap>

class QApplication;
class QPainter;
class QRect;

// Windows Vista era look (glossy gradients, rounded tabs) in night colours: dark grey gloss with
// soft lime text for highlights. Named colours live here; the style sheet refers to them by name.
namespace Theme {

inline const QColor window{0x1d, 0x1f, 0x24};
inline const QColor panel{0x24, 0x26, 0x2c};
inline const QColor field{0x16, 0x17, 0x1b};    // text boxes, lists
inline const QColor fieldAlt{0x1b, 0x1c, 0x21}; // every other list row
inline const QColor outline{0x0b, 0x0c, 0x0f};
inline const QColor edge{0x3c, 0x3f, 0x48};     // inner highlight line
inline const QColor text{0xe6, 0xe6, 0xe6};
inline const QColor dim{0x9a, 0xa0, 0xab};
inline const QColor label{0xcf, 0xd3, 0xda};    // tab and column header text
inline const QColor muted{0x6c, 0x70, 0x7a};    // disabled text
inline const QColor faint{0x4c, 0x50, 0x58};    // greyed-out lists and ticks
inline const QColor accent{0x9f, 0xd4, 0x5a};   // soft lime: highlighted text, focus, outlines
inline const QColor accentDim{0x55, 0x73, 0x30}; // quiet lime outline (the link box)
inline const QColor surface{0x34, 0x37, 0x3e};   // dark grey gloss: headers, selection, Download
inline const QColor onSelect{0x1c, 0x1d, 0x20};  // dark text on lime (text selection, bar label)
inline const QColor hover{0x3a, 0x3e, 0x46};     // grey wash behind hovered buttons and tabs
inline const QColor running{0x7d, 0xb3, 0x3c};   // progress bar fill, a muted lime
inline const QColor done = running;              // finished
inline const QColor failed{0xe0, 0x4a, 0x4a};   // something went wrong
inline const QColor busy{0xe3, 0xb3, 0x41};      // status light while yt-dlp updates

void apply(QApplication &app);   // style, palette, style sheet

// An icon redrawn in one colour: flat (a silhouette), or keeping its light and shade.
QPixmap recolour(const QString &file, const QColor &colour, bool shaded);

enum class BarState { Waiting, Running, Done, Failed };
void drawBar(QPainter &p, const QRect &r, double fraction, BarState state);   // glossy progress bar
void drawGloss(QPainter &p, const QRect &r, const QColor &base, int radius);  // Vista two-tone fill

}
