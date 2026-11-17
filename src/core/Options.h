#pragma once

#include <QString>
#include <QStringList>

// What the user picked for one download.
struct Options
{
    enum Format { Mp4, Mkv, Webm, Mp3, AudioOriginal };

    QString url;
    QString folder;
    Format format = Mp4;
    int maxHeight = 0;   // 0 = best available
    bool subtitles = false;
    bool playlist = false;
    bool skipSponsors = false;   // cut sponsor segments out (SponsorBlock, YouTube only)
    bool overwrite = false;   // replace a file that's already there instead of keeping it
    QString cookies;     // --cookies-from-browser value, only set after a site asks for a sign-in
    QString ffmpeg;      // folder with Queenfisher's own ffmpeg (Windows); empty = yt-dlp finds one

    bool audioOnly() const { return format == Mp3 || format == AudioOriginal; }
    QString label() const;   // short text for the list, e.g. "MP4 1080p"
};

// Progress lines start with this marker so they can be told apart from yt-dlp's own output.
inline const QString progressMarker = "QFP|";

// Arguments are passed one by one, never through a shell or config file.
QStringList ytDlpArgs(const Options &o);
