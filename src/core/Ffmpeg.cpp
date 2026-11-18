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
    const QString release = "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/";
    s->asset = "ffmpeg-master-latest-win64-gpl-shared.zip";
    s->sums = QUrl(release + "checksums.sha256");
    s->zip = QUrl(release + s->asset);
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
