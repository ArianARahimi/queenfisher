#pragma once

#include <QByteArray>

class QWindow;

// X11 lets a window take the front when it carries the launch's own time, taken from the
// launcher's startup ID. Ending that launch also stops the launcher's busy cursor.
namespace X11Activation {
void bringToFront(QWindow *window, const QByteArray &startupId);
}
