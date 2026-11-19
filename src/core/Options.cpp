#include "Options.h"

namespace {

struct FormatInfo { const char *name, *key; };
constexpr FormatInfo formats[Options::FormatCount] = {
    {"MP4", "mp4"}, {"MKV", "mkv"}, {"WebM", "webm"}, {"MOV", "mov"}, {"AVI", "avi"}, {"FLV", "flv"}, {"GIF", "gif"},
    {"MP3", "mp3"}, {"WAV", "wav"}, {"FLAC", "flac"}, {"OGG", "vorbis"}, {"M4A", "m4a"}, {"ALAC", "alac"},
    {"Original audio", "original"}};   // OGG holds Vorbis sound; the key stays as saved

// The GIF recipe: at most 480 px wide, 10 frames a second, one palette for the whole clip and
// only the changed parts of each frame stored. Still big: GIF is an old, wasteful format.
const QString gifArgs = "VideoConvertor:-an -vf \"fps=10,scale='min(480,iw)':-2:flags=lanczos,split[a][b];"
                        "[a]palettegen=stats_mode=diff[p];[b][p]paletteuse=dither=bayer:bayer_scale=5:diff_mode=rectangle\" -loop 0";

}

QString Options::name(Format f) { return formats[f].name; }
QString Options::key(Format f) { return formats[f].key; }

Options::Format Options::fromKey(const QString &key)
{
    for (int f = 0; f < FormatCount; ++f)
        if (key == formats[f].key)
            return Format(f);
    return Mp4;
}

bool Options::isAudioLabel(const QString &label)
{
    const QString first = label.section(' ', 0, 0);
    Options o;
    for (int f = Mp3; f < FormatCount; ++f) {
        o.format = Format(f);
        if (first == o.label())
            return true;
    }
    return false;
}

QString Options::label() const
{
    QString text = format == AudioOriginal ? QString("Audio") : name(format);
    if (hasQuality(format))
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
        // Unfinished pieces stay out of the download folder, so download watchers (like
        // vt-quarantine) only ever see the finished file arrive.
        "-P", "temp:" + (o.tempFolder.isEmpty() ? o.folder : o.tempFolder),
        // The picture's height is in the name ("... [id] 1080p.mp4"; none for sound), so another
        // quality is another file instead of yt-dlp skipping it as already downloaded.
        // ALAC files end in .m4a too, so they're marked to stay apart from M4A ones.
        "-o", QString(o.playlist ? "%(playlist_index)03d - " : "") + "%(title)s [%(id)s]%(height& {}p|)s"
                  + (o.format == Options::Alac ? " ALAC" : "") + ".%(ext)s",
        o.playlist ? "--yes-playlist" : "--no-playlist",
    };

    // Format sorting picks the best stream within the limits instead of failing on exact matches.
    // The picture's size always outranks the sound's codec: otherwise a 360p file that happens
    // to include AAC sound beats the 1080p picture.
    const QString res = o.maxHeight ? QString("res:%1").arg(o.maxHeight) : QString("res");
    switch (o.format) {
    case Options::Mp4:   // H.264 so it plays everywhere (on YouTube that tops out at 1080p)
        a << "-S" << "vcodec:h264," + res + ",acodec:aac,ext:mp4:m4a" << "--merge-output-format" << "mp4";
        break;
    case Options::Mkv:
        if (o.maxHeight)
            a << "-S" << QString("res:%1").arg(o.maxHeight);
        a << "--merge-output-format" << "mkv";
        break;
    case Options::Webm:
        a << "-S" << "ext:webm:webm," + res << "--merge-output-format" << "webm";
        break;
    case Options::Mov:   // the MP4 picture and sound, repacked: nothing is re-encoded
    case Options::Flv:
        a << "-S" << "vcodec:h264," + res + ",acodec:aac,ext:mp4:m4a" << "--remux-video" << Options::key(o.format);
        break;
    case Options::Avi:   // re-encoded to what old players expect (MPEG-4 picture, MP3 sound): slow
        a << "-S" << res << "--recode-video" << "avi";
        break;
    case Options::Gif:   // picture only, small to start with
        a << "-S" << "res:480" << "-f" << "bv*/b" << "--recode-video" << "gif" << "--postprocessor-args" << gifArgs;
        break;
    case Options::Mp3:
        a << "-f" << "ba/b" << "-x" << "--audio-format" << "mp3" << "--audio-quality" << "0";
        break;
    case Options::M4a:   // AAC as it is when the site has it, converted when it doesn't
        a << "-f" << "ba[acodec^=mp4a]/ba/b" << "-x" << "--audio-format" << "m4a";
        break;
    case Options::AudioOriginal:
        a << "-f" << "ba/b" << "-x";
        break;
    case Options::Wav:
    case Options::Flac:
    case Options::Vorbis:
        a << "-f" << "ba/b" << "-x" << "--audio-format" << Options::key(o.format);
        break;
    case Options::Alac:   // yt-dlp doesn't name the codec itself, and ffmpeg would make AAC
        a << "-f" << "ba/b" << "-x" << "--audio-format" << "alac" << "--postprocessor-args" << "ExtractAudio:-c:a alac";
        break;
    case Options::FormatCount:
        break;
    }

    if (o.subtitles && Options::takesSubtitles(o.format))
        a << "--write-subs" << "--embed-subs" << "--sub-langs" << "en.*,en";

    if (o.skipSponsors)
        a << "--sponsorblock-remove" << "sponsor";   // needs ffmpeg, which every video format needs anyway
    if (o.overwrite)
        a << "--force-overwrites";
    if (!o.cookies.isEmpty())
        a << "--cookies-from-browser" << o.cookies;
    if (!o.ffmpeg.isEmpty())
        a << "--ffmpeg-location" << o.ffmpeg;
    if (!o.deno.isEmpty())
        a << "--js-runtimes" << "deno:" + o.deno;

    a << "--" << o.url;  // the link can never be read as an option
    return a;
}
