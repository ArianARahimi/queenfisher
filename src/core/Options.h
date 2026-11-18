#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// What the user picked for one download.
struct Options
{
    // Video formats first, then sound only; the order is the order of the Format list.
    enum Format { Mp4, Mkv, Webm, Mov, Avi, Flv, Gif, Mp3, Wav, Flac, Vorbis, M4a, Alac, AudioOriginal, FormatCount };
    static bool isAudio(Format f) { return f >= Mp3; }
    static bool hasQuality(Format f) { return !isAudio(f) && f != Gif; }   // GIF has its own fixed size
    static bool takesSubtitles(Format f) { return f == Mp4 || f == Mkv || f == Webm; }   // what yt-dlp can embed into
    static QString name(Format f);                  // "MP4", "Original audio"...
    static QString key(Format f);                   // for settings: "mp4", "original"...
    static Format fromKey(const QString &key);      // unknown: MP4
    static bool isAudioLabel(const QString &label); // a label() of a sound-only download: "FLAC", "Audio"

    QString url;
    QString folder;
    QString tempFolder;   // where pieces wait until the file is finished; empty = the folder itself
    Format format = Mp4;
    int maxHeight = 0;   // 0 = best available
    bool subtitles = false;
    bool playlist = false;
    bool skipSponsors = false;   // cut sponsor segments out (SponsorBlock, YouTube only)
    bool overwrite = false;   // replace a file that's already there instead of keeping it
    QList<qint64> expectedParts;   // the preview's picture and sound sizes, for the bar; not for yt-dlp
    bool browserSignIn = true;     // may use the default browser's sign-in when a site asks for one
    QString cookies;     // --cookies-from-browser value, only set after a site asks for a sign-in
    QString deno;        // Queenfisher's own Deno for YouTube; empty = yt-dlp finds an installed one
    QString ffmpeg;      // folder with Queenfisher's own ffmpeg (Windows); empty = yt-dlp finds one

    bool audioOnly() const { return isAudio(format); }
    QString label() const;   // short text for the list, e.g. "MP4 1080p"
};

// Progress lines start with this marker so they can be told apart from yt-dlp's own output.
inline const QString progressMarker = "QFP|";

// Arguments are passed one by one, never through a shell or config file.
QStringList ytDlpArgs(const Options &o);
