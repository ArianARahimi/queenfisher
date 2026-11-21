#pragma once

#include <QString>

class QProcess;

// yt-dlp unpacks itself and runs a second copy, which then starts ffmpeg. Stopping only the
// process Queenfisher started would leave the rest running, so every run is stopped as a tree.
namespace ChildProcess {

void ownGroup(QProcess &p);              // before start(): on Linux and macOS, its own process group
void stop(QProcess &p, bool hard);       // the whole tree; hard = no chance to clean up
void stopAndWait(QProcess &p);           // hard stop, then wait briefly, for shutting down
QString taskkill();                      // Windows' own, by full path; public for tests
}
