#include "Options.h"

QString Options::label() const
{
    static const char *names[] = {"MP4", "MKV", "WebM", "MP3", "Audio"};
    QString text = names[format];
    if (!audioOnly())
        text += maxHeight ? QString(" %1p").arg(maxHeight) : QString(" Best");
    return text;
}

QStringList ytDlpArgs(const Options &o)
{
    QStringList a = {
        "--ignore-config",   // a user's own yt-dlp config must not change what Queenfisher does
        "--newline", "--color", "never",
        "--progress", "--progress-template",
        "download:" + progressMarker
            + "%(progress.downloaded_bytes)s|%(progress.total_bytes)s|%(progress.total_bytes_estimate)s|"
              "%(progress.speed)s|%(progress.eta)s|%(progress.fragment_index)s|%(progress.fragment_count)s|"
              "%(info.title)s",
        "-P", o.folder,
        "-o", o.playlist ? "%(playlist_index)03d - %(title)s [%(id)s].%(ext)s" : "%(title)s [%(id)s].%(ext)s",
        o.playlist ? "--yes-playlist" : "--no-playlist",
    };

    // Format sorting picks the best stream within the limits instead of failing on exact matches.
    const QString res = o.maxHeight ? QString("res:%1,").arg(o.maxHeight) : QString();
    switch (o.format) {
    case Options::Mp4:
        a << "-S" << res + "vcodec:h264,acodec:aac,ext:mp4:m4a" << "--merge-output-format" << "mp4";
        break;
    case Options::Mkv:
        if (o.maxHeight)
            a << "-S" << QString("res:%1").arg(o.maxHeight);
        a << "--merge-output-format" << "mkv";
        break;
    case Options::Webm:
        a << "-S" << res + "ext:webm:webm" << "--merge-output-format" << "webm";
        break;
    case Options::Mp3:
        a << "-f" << "ba/b" << "-x" << "--audio-format" << "mp3" << "--audio-quality" << "0";
        break;
    case Options::AudioOriginal:
        a << "-f" << "ba/b" << "-x";
        break;
    }

    if (o.subtitles && !o.audioOnly())
        a << "--write-subs" << "--embed-subs" << "--sub-langs" << "en.*,en";

    if (!o.cookies.isEmpty())
        a << "--cookies-from-browser" << o.cookies;

    a << "--" << o.url;  // the link can never be read as an option
    return a;
}
