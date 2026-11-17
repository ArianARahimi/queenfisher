#include "PreviewJob.h"
#include "Cookies.h"
#include "Engine.h"
#include "ErrorRules.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

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
    return info;
}

PreviewJob::PreviewJob(QString url, bool playlist, Engine *engine, QObject *parent)
    : QObject(parent), url_(std::move(url)), playlist_(playlist), engine_(engine)
{
    connect(&process_, &QProcess::finished, this, &PreviewJob::finishedRun);
    timeout_.setSingleShot(true);
    timeout_.setInterval(timeoutMs);
    connect(&timeout_, &QTimer::timeout, &process_, &QProcess::kill);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit failed("Couldn't start yt-dlp");
    });
}

PreviewJob::~PreviewJob()
{
    if (process_.state() != QProcess::NotRunning) {
        disconnect(&process_, nullptr, this, nullptr);
        process_.kill();
        process_.waitForFinished(2000);
    }
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
    args << "--" << url_;
    process_.start(engine_->path(), args);
    timeout_.start();
}

void PreviewJob::finishedRun(int code, QProcess::ExitStatus status)
{
    timeout_.stop();
    const QByteArray out = process_.readAllStandardOutput();
    QStringList problems;
    for (const QString &line : QString::fromUtf8(process_.readAllStandardError()).split('\n'))
        if (line.startsWith("ERROR:") || line.startsWith("WARNING:"))
            problems << line.trimmed();

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
