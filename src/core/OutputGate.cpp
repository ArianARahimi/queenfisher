#include "OutputGate.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include <algorithm>

namespace {

// Codec lists: empty = any; "none" = no such streams at all; a trailing * matches a prefix.
struct Rule
{
    QStringList ext;         // allowed file extensions
    const char *container;   // a word of ffprobe's format_name; nullptr = any
    QStringList video, audio;
};

Rule ruleFor(Options::Format f)
{
    const QStringList any, none = {"none"};
    switch (f) {
    case Options::Mp4: return {{"mp4"}, "mp4", {"h264", "hevc", "av1", "vp9"}, {"aac", "opus", "mp3", "ac3", "eac3"}};
    case Options::Mkv: return {{"mkv"}, "matroska", any, any};
    case Options::Webm: return {{"webm"}, "webm", {"vp8", "vp9", "av1"}, {"opus", "vorbis"}};
    case Options::Mov: return {{"mov"}, "mov", {"h264", "hevc"}, {"aac", "mp3", "ac3"}};
    case Options::Avi: return {{"avi"}, "avi", any, any};
    case Options::Flv: return {{"flv"}, "flv", {"h264"}, {"aac", "mp3"}};
    case Options::Gif: return {{"gif"}, "gif", {"gif"}, none};
    case Options::Mp3: return {{"mp3"}, "mp3", none, {"mp3"}};
    case Options::Wav: return {{"wav"}, "wav", none, {"pcm_*"}};
    case Options::Flac: return {{"flac"}, "flac", none, {"flac"}};
    case Options::Vorbis: return {{"ogg"}, "ogg", none, {"vorbis"}};
    case Options::M4a: return {{"m4a"}, "m4a", none, {"aac"}};
    case Options::Alac: return {{"m4a"}, "m4a", none, {"alac"}};
    // The site's own sound, in whichever of these yt-dlp puts it: never an unknown file type.
    case Options::AudioOriginal: return {{"opus", "m4a", "mp3", "ogg", "flac", "wav"}, nullptr, none, any};
    case Options::FormatCount: break;
    }
    return {};
}

struct Probe
{
    bool read = false;
    QStringList containers;   // ffprobe's format_name, split: "mov", "mp4", "m4a"...
    QStringList video, audio;   // codec names; cover pictures aren't video
};

Probe probe(const QString &file, const QString &ffprobe)
{
    QProcess p;
    p.start(ffprobe, {"-v", "error", "-of", "json", "-show_entries",
                         "format=format_name:stream=codec_type,codec_name:stream_disposition=attached_pic", file});
    if (!p.waitForFinished(60000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return {};
    const QJsonObject o = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
    Probe r;
    r.read = o.contains("format");
    r.containers = o.value("format").toObject().value("format_name").toString().split(',', Qt::SkipEmptyParts);
    for (const auto &v : o.value("streams").toArray()) {
        const QJsonObject s = v.toObject();
        const QString type = s.value("codec_type").toString(), codec = s.value("codec_name").toString();
        if (type == "audio")
            r.audio << codec;
        else if (type == "video" && s.value("disposition").toObject().value("attached_pic").toInt() == 0)
            r.video << codec;
    }
    return r;
}

bool allowed(const QStringList &codecs, const QStringList &rule)
{
    if (rule == QStringList{"none"})
        return codecs.isEmpty();
    for (const QString &c : codecs)
        if (!rule.isEmpty() && !std::any_of(rule.cbegin(), rule.cend(), [&](const QString &r) {
                return r.endsWith('*') ? c.startsWith(r.chopped(1)) : c == r;
            }))
            return false;
    return true;
}

QString describe(const QString &file, const Probe &p)
{
    QString text = QFileInfo(file).suffix().toUpper();
    const QStringList codecs = p.video + p.audio;
    if (!codecs.isEmpty())
        text += " with " + codecs.join(" and ");
    return text;
}

}

namespace OutputGate {

QString problem(const QString &file, Options::Format format, const QString &ffprobe)
{
    const Rule rule = ruleFor(format);
    const QString wanted = "not " + Options::name(format);
    const Probe p = probe(file, ffprobe);
    if (!p.read)
        return "a file ffprobe can't read, " + wanted;
    const bool extOk = rule.ext.contains(QFileInfo(file).suffix(), Qt::CaseInsensitive);
    const bool containerOk = !rule.container || p.containers.contains(rule.container);
    const bool audioOnly = Options::isAudio(format);
    const bool hasWhatItNeeds = audioOnly ? !p.audio.isEmpty() : !p.video.isEmpty();
    if (extOk && containerOk && hasWhatItNeeds && allowed(p.video, rule.video) && allowed(p.audio, rule.audio))
        return {};
    return describe(file, p) + ", " + wanted;
}

QString ffprobe(const QString &ffmpegFolder)
{
#ifdef Q_OS_WIN
    const QString name = "ffprobe.exe";
#else
    const QString name = "ffprobe";
#endif
    if (!ffmpegFolder.isEmpty() && QFileInfo::exists(QDir(ffmpegFolder).filePath(name)))
        return QDir(ffmpegFolder).filePath(name);
    return QStandardPaths::findExecutable("ffprobe");
}

}
