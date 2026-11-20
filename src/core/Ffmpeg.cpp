#include "Ffmpeg.h"

#include <QDir>

Ffmpeg::Ffmpeg(const QString &dataDir, QObject *parent)
    : FetchedTool(dataDir + "/ffmpeg", "ffmpeg", "about 90 MB", parent)
{
}

bool Ffmpeg::wanted() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return false;   // installed with the system's package manager
#endif
}

bool Ffmpeg::source(Source *s) const
{
    // A month-end build: yt-dlp keeps those for years, daily ones only for about two weeks.
    s->zip = QUrl("https://github.com/yt-dlp/FFmpeg-Builds/releases/download/autobuild-2026-10-31-18-18/"
                  "ffmpeg-N-128251-g6d1bfd7609-win64-gpl-shared.zip");
    s->sha256 = "de0839788c3f9a649dfb49f6f800a58532c5055ad9af7a75c0aa02043b880591";
    s->limit = 400LL << 20;   // the zip is about 90 MB
    return true;
}

// Only the programs and their libraries, from the bin folder; nothing else is kept.
bool Ffmpeg::keep(const QFileInfo &f) const
{
    const QString name = f.fileName();
    const bool wantedFile = name == exe("ffmpeg") || name == exe("ffprobe") || name.endsWith(".dll", Qt::CaseInsensitive);
    return wantedFile && f.absoluteDir().dirName() == "bin";
}
