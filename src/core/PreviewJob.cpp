#include "PreviewJob.h"
#include "ChildProcess.h"
#include "Cookies.h"
#include "Engine.h"
#include "ErrorRules.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <QUrlQuery>

namespace {

constexpr int timeoutMs = 60000;

// Largest JPEG or PNG thumbnail; WebP only as a last resort, since not every Qt can show it.
QString pickThumbnail(const QJsonObject &o)
{
    QString best;
    int bestScore = -1;
    for (const QJsonValue &v : o.value("thumbnails").toArray()) {
        const QJsonObject t = v.toObject();
        const QString url = t.value("url").toString();
        const QString path = QUrl(url).path().toLower();
        if (!url.startsWith("https://") || !(path.endsWith(".jpg") || path.endsWith(".jpeg") || path.endsWith(".png")))
            continue;
        const int score = t.value("width").toInt() * t.value("height").toInt() + t.value("preference").toInt();
        if (score > bestScore) {
            best = url;
            bestScore = score;
        }
    }
    if (best.isEmpty() && o.value("thumbnail").toString().startsWith("https://"))
        best = o.value("thumbnail").toString();
    return best;
}

}

QString youTubeId(const QString &link)
{
    const QUrl url(link.trimmed());
    const QString host = url.host().toLower();
    static const QRegularExpression valid("^[A-Za-z0-9_-]{11}$");
    QString id;
    if (url.scheme() != "https" && url.scheme() != "http")
        return {};
    if (host == "youtu.be") {
        id = url.path().section('/', 1, 1);
    } else if (host == "youtube.com" || host.endsWith(".youtube.com")) {
        const QString path = url.path();
        if (path == "/watch")
            id = QUrlQuery(url).queryItemValue("v");
        else if (path.startsWith("/shorts/") || path.startsWith("/embed/") || path.startsWith("/live/") || path.startsWith("/v/"))
            id = path.section('/', 2, 2);
    }
    return valid.match(id).hasMatch() ? id : QString();
}

PreviewInfo parsePreview(const QByteArray &json)
{
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    PreviewInfo info;
    info.title = o.value("title").toString();
    info.channel = o.value("channel").toString(o.value("uploader").toString());
    info.site = o.value("webpage_url_domain").toString(o.value("extractor_key").toString());
    info.live = o.value("is_live").toBool();
    if (o.contains("duration") && !info.live)
        info.seconds = int(o.value("duration").toDouble());
    if (o.value("_type").toString() == "playlist") {
        const QJsonArray entries = o.value("entries").toArray();
        info.playlistCount = o.contains("playlist_count") ? o.value("playlist_count").toInt() : int(entries.size());
        info.thumbnail = pickThumbnail(o);
        if (info.thumbnail.isEmpty() && !entries.isEmpty())
            info.thumbnail = pickThumbnail(entries.first().toObject());   // first video's picture
    } else {
        info.thumbnail = pickThumbnail(o);
    }
    const double seconds = o.value("duration").toDouble();
    for (const QJsonValue &v : o.value("formats").toArray()) {
        const QJsonObject f = v.toObject();
        Stream s;
        s.height = f.value("height").toInt();
        s.kbps = f.value("tbr").toDouble();
        s.ext = f.value("ext").toString();
        s.vcodec = f.value("vcodec").toString();
        s.acodec = f.value("acodec").toString();
        s.hls = f.value("protocol").toString().startsWith("m3u8");
        s.bytes = f.contains("filesize") && !f.value("filesize").isNull() ? qint64(f.value("filesize").toDouble())
                : f.contains("filesize_approx") && !f.value("filesize_approx").isNull() ? qint64(f.value("filesize_approx").toDouble())
                : s.kbps > 0 && seconds > 0 ? qint64(s.kbps * 1000 / 8 * seconds) : -1;
        if (s.picture() || s.sound())   // not storyboards and the like
            info.streams.append(s);
    }
    return info;
}

namespace {

// The pictures yt-dlp would choose from for this format, the way Options asks for them:
// MP4 wants H.264 and WebM wants WebM when the video has any; otherwise anything goes.
bool wantedPicture(const Stream &s, Options::Format format)
{
    const bool h264 = format == Options::Mp4 || format == Options::Mov || format == Options::Flv;
    return h264 ? s.vcodec.startsWith("avc1") : format == Options::Webm ? s.ext == "webm" : true;
}

}

namespace {

// yt-dlp's own order for sound codecs: higher is better.
int soundRank(const QString &codec)
{
    static const QStringList order = {"dts", "ac3", "eac3", "ac4", "mp3", "mp4a", "aac", "vorbis", "opus", "aiff", "wav", "alac", "flac"};
    for (qsizetype i = order.size() - 1; i >= 0; --i)
        if (codec.startsWith(order[i]))
            return int(i) + 1;
    return 0;
}

// The sound-only stream yt-dlp takes for "ba": best codec first, then bitrate.
const Stream *bestSound(const QList<Stream> &streams)
{
    const Stream *best = nullptr;
    for (const Stream &s : streams) {
        if (!s.sound() || s.picture())
            continue;
        if (!best || soundRank(s.acodec) > soundRank(best->acodec)
            || (soundRank(s.acodec) == soundRank(best->acodec) && s.kbps > best->kbps))
            best = &s;
    }
    return best;
}

}

QString originalAudioName(const PreviewInfo &info)
{
    const Stream *s = bestSound(info.streams);
    if (!s)
        return {};
    static const QList<std::pair<const char *, const char *>> names = {
        {"opus", "Opus"}, {"mp4a", "AAC"}, {"aac", "AAC"}, {"vorbis", "Vorbis"}, {"flac", "FLAC"},
        {"alac", "ALAC"}, {"mp3", "MP3"}, {"wav", "WAV"}, {"ac3", "AC-3"}, {"eac3", "E-AC-3"}};
    for (const auto &[codec, name] : names)
        if (s->acodec.startsWith(codec))
            return name;
    return s->acodec.section('.', 0, 0).toUpper();
}

int bestHeight(const PreviewInfo &info, Options::Format format)
{
    int best = 0, bestWanted = 0;
    for (const Stream &s : info.streams) {
        if (!s.picture())
            continue;
        best = qMax(best, s.height);
        if (wantedPicture(s, format))
            bestWanted = qMax(bestWanted, s.height);
    }
    return bestWanted ? bestWanted : best;
}

namespace {

// The best of the streams `fits` allows, preferring those `preferred` likes: tallest, then
// highest bitrate. Null if none fit.
const Stream *pick(const QList<Stream> &streams, const auto &fits, const auto &preferred)
{
    const Stream *best = nullptr;
    for (bool usePreference : {true, false}) {
        for (const Stream &s : streams) {
            if (!fits(s) || (usePreference && !preferred(s)))
                continue;
            if (!best || s.height > best->height || (s.height == best->height && s.kbps > best->kbps))
                best = &s;
        }
        if (best)
            return best;
    }
    return nullptr;
}

}

qint64 estimateSize(const PreviewInfo &info, Options::Format format, int maxHeight)
{
    // Like yt-dlp: plain downloads before streamed-in-pieces ones, when there are any.
    QList<Stream> streams;
    for (const Stream &s : info.streams)
        if (!s.hls)
            streams << s;
    if (streams.isEmpty())
        streams = info.streams;
    const auto isSound = [](const Stream &s) { return s.sound() && !s.picture(); };
    if (format == Options::Mp3) {
        double seconds = 0;   // MP3 at the best quality averages about 245 kbit/s
        for (const Stream &s : streams)
            if (s.kbps > 0 && s.bytes > 0)
                seconds = qMax(seconds, s.bytes * 8.0 / (s.kbps * 1000));
        return seconds > 0 ? qint64(seconds * 245000 / 8) : -1;
    }
    if (format == Options::AudioOriginal) {
        const Stream *a = bestSound(streams);
        return a ? a->bytes : -1;
    }
    if (format == Options::M4a) {   // the site's AAC as it is; converted from something else: unknown
        const Stream *a = pick(streams, isSound, [](const Stream &s) { return s.acodec.startsWith("mp4a"); });
        return a && a->acodec.startsWith("mp4a") ? a->bytes : -1;
    }
    if (format == Options::Wav) {   // 48 kHz, 16-bit stereo: 192 KB a second
        const double seconds = info.seconds;
        return seconds > 0 ? qint64(seconds * 192000) : -1;
    }
    // Re-encoded (AVI, GIF) or compressed by ffmpeg (FLAC, ALAC, Vorbis): no honest guess.
    if (format == Options::Avi || format == Options::Gif || Options::isAudio(format))
        return -1;
    const auto fits = [maxHeight](const Stream &s) { return s.picture() && (!maxHeight || s.height <= maxHeight); };
    const auto likedPicture = [format](const Stream &s) { return wantedPicture(s, format); };
    const auto likedSound = [format](const Stream &s) {
        const bool aac = format == Options::Mp4 || format == Options::Mov || format == Options::Flv;
        return aac ? s.ext == "m4a" : format == Options::Webm ? s.ext == "webm" : true;
    };
    const Stream *v = pick(streams, fits, likedPicture);
    if (!v || v->bytes < 0)
        return -1;
    if (v->sound())
        return v->bytes;   // picture and sound in one
    const Stream *a = pick(streams, isSound, likedSound);
    return a && a->bytes >= 0 ? v->bytes + a->bytes : -1;
}

PreviewJob::PreviewJob(QString url, bool playlist, Engine *engine, QObject *parent)
    : QObject(parent), url_(std::move(url)), playlist_(playlist), engine_(engine)
{
    ChildProcess::ownGroup(process_);
    connect(&process_, &QProcess::finished, this, &PreviewJob::finishedRun);
    timeout_.setSingleShot(true);
    timeout_.setInterval(timeoutMs);
    connect(&timeout_, &QTimer::timeout, this, [this] { ChildProcess::stop(process_, true); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit failed("Couldn't start yt-dlp");
    });
}

PreviewJob::~PreviewJob()
{
    disconnect(&process_, nullptr, this, nullptr);
    ChildProcess::stopAndWait(process_);   // the whole tree, or yt-dlp's second copy outlives Queenfisher
}

void PreviewJob::start()
{
    if (engine_->path().isEmpty()) {
        emit failed("yt-dlp isn't available yet");
        return;
    }
    QStringList args = {"--ignore-config", "--color", "never", "--skip-download", "-J"};
    args << (playlist_ ? QStringList{"--yes-playlist", "--flat-playlist"} : QStringList{"--no-playlist"});
    if (!cookies_.isEmpty())
        args << "--cookies-from-browser" << cookies_;
    if (!deno_.isEmpty())
        args << "--js-runtimes" << "deno:" + deno_;
    args << "--" << url_;
    process_.start(engine_->path(), args);
    timeout_.start();
}

void PreviewJob::finishedRun(int code, QProcess::ExitStatus status)
{
    timeout_.stop();
    const QByteArray out = process_.readAllStandardOutput();
    const QStringList problems = problemLines(process_.readAllStandardError());

    if (status == QProcess::NormalExit && code == 0) {
        const PreviewInfo info = parsePreview(out);
        if (info.title.isEmpty())
            emit failed("No preview for this link");
        else
            emit ready(info);
        return;
    }

    for (const QString &line : problems)
        emit logLine("Preview: " + line);
    const Diagnosis d = diagnose(problems);
    if (d.problem == Problem::SignIn && cookies_.isEmpty()) {
        cookies_ = defaultBrowserCookies();
        if (!cookies_.isEmpty()) {
            emit logLine("Preview: the site wants a sign-in, trying with your browser's sign-in.");
            start();
            return;
        }
    }
    emit failed(d.message);
}
