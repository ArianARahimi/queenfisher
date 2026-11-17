#include "ClipJob.h"
#include "Cookies.h"
#include "Engine.h"
#include "ErrorRules.h"

#include <QFileInfo>

ClipJob::ClipJob(QString url, int startSeconds, QString folder, QString cookies, Engine *engine, QObject *parent)
    : QObject(parent), url_(std::move(url)), start_(startSeconds), folder_(std::move(folder)),
      cookies_(std::move(cookies)), engine_(engine)
{
    connect(&process_, &QProcess::finished, this, &ClipJob::finishedRun);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit failed("Couldn't start yt-dlp");
    });
    timeout_.setSingleShot(true);
    timeout_.setInterval(90000);
    connect(&timeout_, &QTimer::timeout, &process_, &QProcess::kill);
}

ClipJob::~ClipJob()
{
    if (process_.state() != QProcess::NotRunning) {
        disconnect(&process_, nullptr, this, nullptr);
        process_.kill();
        process_.waitForFinished(2000);
    }
}

QStringList ClipJob::args(const QString &url, int startSeconds, const QString &folder, const QString &cookies,
                          const QString &ffmpeg)
{
    QStringList a = {
        "--ignore-config", "--color", "never", "--no-playlist", "--no-part",
        // Small, with sound: one combined stream at or under 360p is quickest; otherwise
        // picture and sound are fetched and joined. H.264 and AAC play everywhere.
        "-f", "b[height<=360]/bv*[height<=360]+ba/w",
        "-S", "vcodec:h264,acodec:aac", "--merge-output-format", "mp4",
        "--download-sections", QString("*%1-%2").arg(startSeconds).arg(startSeconds + lengthSeconds),
        "-P", folder, "-o", "clip-%(id)s.%(ext)s",
        "--print", "after_move:filepath",   // the finished file's path, nothing else on stdout
    };
    if (!cookies.isEmpty())
        a << "--cookies-from-browser" << cookies;
    if (!ffmpeg.isEmpty())
        a << "--ffmpeg-location" << ffmpeg;
    a << "--" << url;
    return a;
}

void ClipJob::start()
{
    if (engine_->path().isEmpty()) {
        emit failed("yt-dlp isn't available yet");
        return;
    }
    process_.start(engine_->path(), args(url_, start_, folder_, cookies_, ffmpeg_));
    timeout_.start();
}

void ClipJob::finishedRun(int code, QProcess::ExitStatus status)
{
    timeout_.stop();
    const QStringList out = QString::fromUtf8(process_.readAllStandardOutput()).split('\n', Qt::SkipEmptyParts);
    const QStringList problems = problemLines(process_.readAllStandardError());

    const QString file = out.isEmpty() ? QString() : out.last().trimmed();
    if (status == QProcess::NormalExit && code == 0 && QFileInfo(file).isFile()
        && QFileInfo(file).absolutePath() == QFileInfo(folder_).absoluteFilePath()) {
        emit ready(file);
        return;
    }

    for (const QString &line : problems)
        emit logLine("Clip: " + line);
    const Diagnosis d = diagnose(problems);
    if (d.problem == Problem::SignIn && cookies_.isEmpty()) {
        cookies_ = defaultBrowserCookies();
        if (!cookies_.isEmpty()) {
            start();
            return;
        }
    }
    emit failed(d.problem == Problem::Unknown ? QString("Couldn't load a clip") : d.message);
}
