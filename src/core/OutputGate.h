#pragma once

#include "Options.h"

#include <QString>

// The last check before a download counts as done: the finished file must really be the
// format the user picked, container and codecs, as ffprobe reads it.
namespace OutputGate {

// Empty if the file is the format, else what it is instead, e.g. "WebM with vp8 and vorbis, not MP4".
QString problem(const QString &file, Options::Format format, const QString &ffprobe);

// ffprobe next to Queenfisher's own ffmpeg (Windows), else the one on the computer; empty if none.
QString ffprobe(const QString &ffmpegFolder);

}
