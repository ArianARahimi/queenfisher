#include "DownloadJob.h"
#include "ChildProcess.h"
#include "Cookies.h"
#include "Engine.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

namespace {

// The part of a file name every file of one video shares: "Title [id]".
QString stemOf(const QString &path)
{
    const QFileInfo info(path);
    const QString name = info.fileName();
    const qsizetype id = name.lastIndexOf(']');
    return id > 0 ? name.left(id + 1) : info.completeBaseName();
}

}

DownloadJob::DownloadJob(Options options, Engine *engine, QObject *parent)
    : QObject(parent), options_(std::move(options)), engine_(engine)
{
    ChildProcess::ownGroup(process_);
    connect(&process_, &QProcess::readyReadStandardOutput, this, [this] { readOutput(QProcess::StandardOutput); });
    connect(&process_, &QProcess::readyReadStandardError, this, [this] { readOutput(QProcess::StandardError); });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit finished(false, "Couldn't start yt-dlp");
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        for (auto channel : {QProcess::StandardOutput, QProcess::StandardError})
            if (!partial_[channel].isEmpty())
                handleLine(QString::fromUtf8(std::exchange(partial_[channel], {})));
        if (cancelled_) {
            discardPartial();
            emit finished(false, "Stopped");
        }
        else if (status == QProcess::NormalExit && code == 0) {
            // Without ffmpeg, yt-dlp keeps picture and sound apart and still exits 0.
            const Diagnosis d = diagnose(problems_);
            if (d.problem == Problem::NoFfmpeg)
                emit finished(false, d.message);
            else
                emit finished(true, "Done");
        }
        else
            recover();
    });
}

DownloadJob::~DownloadJob()
{
    if (process_.state() != QProcess::NotRunning) {
        disconnect(&process_, nullptr, this, nullptr);   // no signals from a half-destroyed job
        stopAll(true);
        process_.waitForFinished(3000);
        discardPartial();   // removed while running: nothing of it should stay behind
    }
}

void DownloadJob::start()
{
    const QString program = engine_->path();
    if (program.isEmpty()) {
        emit finished(false, "yt-dlp isn't available yet");
        return;
    }
    problems_.clear();
    file_.clear();
    if (!startedAt_.isValid())
        startedAt_ = QDateTime::currentDateTime().addSecs(-2);   // a little slack for coarse file times
    tracker_.reset();
    speed_ = eta_ = -1;
    sidecar_ = skipped_ = downloaded_ = false;
    const QStringList args = ytDlpArgs(options_);
    emit logLine(QString("> yt-dlp %1  [%2]").arg(args.join(' '), engine_->version()));
    process_.start(program, args);
}

void DownloadJob::cancel()
{
    if (cancelled_)
        return;
    cancelled_ = true;
    if (process_.state() == QProcess::NotRunning) {   // between attempts, e.g. while yt-dlp updates
        discardPartial();
        emit finished(false, "Stopped");
        return;
    }
    stopAll(false);
    QTimer::singleShot(5000, this, [this] {   // still going after 5 s: force it
        if (process_.state() != QProcess::NotRunning)
            stopAll(true);
    });
}

void DownloadJob::stopAll(bool hard)
{
    ChildProcess::stop(process_, hard);   // yt-dlp and the ffmpeg it started
}

// Only files named after this video (title [id]) that were written since it started,
// so files that were there before, or belong to other videos, are never touched. Playlist
// videos that already finished stay too.
void DownloadJob::discardPartial()
{
    QHash<QString, QSet<QString>> stems;   // folder -> videos in it; each folder is listed once
    for (const QString &path : std::as_const(created_))
        if (const QString stem = stemOf(path); stem.size() >= 3 && !finishedStems_.contains(stem))
            stems[QFileInfo(path).absolutePath()].insert(stem);
    for (auto it = stems.cbegin(); it != stems.cend(); ++it)
        for (const QFileInfo &f : QDir(it.key()).entryInfoList(QDir::Files | QDir::Hidden)) {
            const bool ours = std::any_of(it->cbegin(), it->cend(), [&](const QString &stem) { return f.fileName().startsWith(stem); });
            if (ours && f.lastModified() >= startedAt_ && QFile::remove(f.absoluteFilePath()))
                emit logLine("Deleted leftover " + f.fileName());
        }
}

void DownloadJob::readOutput(QProcess::ProcessChannel channel)
{
    process_.setReadChannel(channel);
    QByteArray &buffer = partial_[channel];
    buffer += process_.readAll();
    qsizetype end;
    while ((end = buffer.indexOf('\n')) >= 0) {
        handleLine(QString::fromUtf8(buffer.left(end)).trimmed());
        buffer.remove(0, end + 1);
    }
}

void DownloadJob::handleLine(const QString &line)
{
    if (line.isEmpty())
        return;
    if (line.startsWith(progressMarker)) {
        handleProgress(line.mid(progressMarker.size()));
        return;
    }
    emit logLine(line);
    trackLine(line);
    if (line.startsWith("ERROR:") || line.startsWith("WARNING:"))
        problems_.append(line);
    else if (line.startsWith("[Merger]"))
        emit stage("Merging");
    else if (line.startsWith("[ExtractAudio]") || line.startsWith("[VideoConvertor]") || line.startsWith("[VideoRemuxer]"))
        emit stage("Converting");
    else if (line.startsWith("[ModifyChapters]"))
        emit stage("Cutting sponsors");
    else if (line.startsWith("[EmbedSubtitle]"))
        emit stage("Adding subtitles");
}

// Lines that tell the tracker a new playlist video, a new file or a finished file has begun.
void DownloadJob::trackLine(const QString &line)
{
    static const QRegularExpression item(R"(^\[download\] Downloading item (\d+) of (\d+))");
    static const QRegularExpression formats(R"(^\[info\] .*Downloading \d+ format\(s\): (\S+))");
    static const QRegularExpression sidecar(R"(\.(vtt|srt|ass|lrc|json|jpe?g|png|webp)$)", QRegularExpression::CaseInsensitiveOption);

    if (const auto m = item.match(line); m.hasMatch()) {
        if (!file_.isEmpty())
            finishedStems_.insert(stemOf(file_));   // the playlist moved on: the last video is done
        tracker_.startItem(m.captured(1).toInt(), m.captured(2).toInt());
    }
    else if (const auto m = formats.match(line); m.hasMatch())
        tracker_.setPartCount(int(m.captured(1).count('+')) + 1);
    else if (line.startsWith("[download] Destination: ")) {
        sidecar_ = sidecar.match(line).hasMatch();   // subtitles and thumbnails aren't parts of the video
        created_.append(line.mid(24));
        if (!sidecar_) {
            downloaded_ = true;
            tracker_.startPart();
            file_ = line.mid(24);
        }
    }
    else if (line.startsWith("[download] ") && line.endsWith(" has already been downloaded")) {
        tracker_.finishPart();
        skipped_ = true;
        file_ = line.mid(11).chopped(28);
    }

    // Later steps rename the file: merged, converted to audio, or remuxed.
    static const QRegularExpression merged(R"re(^\[Merger\] Merging formats into "(.+)"$)re");
    static const QRegularExpression moved(R"re(^\[(ExtractAudio|VideoRemuxer|VideoConvertor)\] .*Destination: (.+)$)re");
    static const QRegularExpression home(R"re(^\[MoveFiles\] Moving file ".+" to "(.+)"$)re");   // out of the temp folder
    if (const auto m = merged.match(line); m.hasMatch())
        created_.append(file_ = m.captured(1));
    else if (const auto m = moved.match(line); m.hasMatch())
        created_.append(file_ = m.captured(2));
    else if (const auto m = home.match(line); m.hasMatch())
        created_.append(file_ = m.captured(1));
}

// downloaded|total|estimate|speed|eta|fragment|fragments|title, missing numbers come as "NA".
void DownloadJob::handleProgress(const QString &fields)
{
    const QStringList f = fields.split('|');
    if (f.size() < 8)
        return;
    const QString title = f.mid(7).join('|');
    if (title != title_ && title != "NA") {
        title_ = title;
        emit titleKnown(title);
    }
    if (sidecar_)
        return;
    const auto number = [](const QString &s) { return s == "NA" ? -1.0 : s.toDouble(); };
    const double total = number(f[1]) > 0 ? number(f[1]) : number(f[2]);
    const double fraction = tracker_.update(qint64(number(f[0])), qint64(total), int(number(f[5])), int(number(f[6])));

    // Moving averages: a new reading only nudges the shown speed and time left.
    const auto smooth = [](double &shown, double reading) {
        if (reading >= 0)
            shown = shown < 0 ? reading : shown * 0.8 + reading * 0.2;
    };
    smooth(speed_, number(f[3]));
    smooth(eta_, number(f[4]));
    emit progress(fraction, tracker_.knownBytes(), speed_, eta_ < 0 ? -1 : int(eta_ + 0.5));
}

// Each fix is tried at most once: the browser's sign-in, then a newer (or nightly) yt-dlp.
void DownloadJob::recover()
{
    const Diagnosis d = diagnose(problems_);

    if (d.problem == Problem::SignIn && options_.cookies.isEmpty()) {
        options_.cookies = defaultBrowserCookies();
        if (!options_.cookies.isEmpty()) {
            emit logLine("The site wants a sign-in. Trying again with your browser's sign-in ("
                         + options_.cookies.section(':', 0, 0) + ").");
            emit stage("Signing in");
            start();
            return;
        }
        emit logLine("The site wants a sign-in, but Queenfisher couldn't find your default browser's cookies.");
    }

    if (d.problem == Problem::Outdated && !triedRepair_) {
        triedRepair_ = true;
        emit logLine("This looks like a site change. Checking for a newer yt-dlp before trying again.");
        emit stage("Updating yt-dlp");
        QPointer<DownloadJob> self(this);   // the row may be removed while yt-dlp updates
        engine_->repair([self, d](bool changed, const QString &message) {
            if (!self || self->cancelled_)
                return;   // removed or stopped while yt-dlp updated
            emit self->logLine(message);
            if (changed)
                self->start();
            else
                emit self->finished(false, d.message + ", even with the newest yt-dlp");
        });
        return;
    }

    if (d.problem == Problem::SignIn && !options_.cookies.isEmpty())
        emit finished(false, "Sign in to the site in your browser, then retry");
    else
        emit finished(false, d.message);
}
