#include "DownloadJob.h"
#include "ChildProcess.h"
#include "Cookies.h"
#include "Engine.h"
#include "OutputGate.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTimer>

#include <memory>

#pragma region Starting, stopping and cleanup
namespace {

// The part of a file name every file of one video shares: "Title [id]".
QString stemOf(const QString &path)
{
    const QFileInfo info(path);
    const QString name = info.fileName();
    const qsizetype id = name.lastIndexOf(']');
    return id > 0 ? name.left(id + 1) : info.completeBaseName();
}

// A JSON string as yt-dlp prints it with the j conversion; empty if it isn't one.
QString jsonText(const QString &quoted) { return QJsonDocument::fromJson(("[" + quoted + "]").toUtf8()).array().at(0).toString(); }

bool sameFolder(const QString &path, const QString &folder)
{
    const QString canonical = QFileInfo(folder).canonicalFilePath();
    return !canonical.isEmpty() && QFileInfo(QFileInfo(path).absolutePath()).canonicalFilePath() == canonical;
}
}

DownloadJob::DownloadJob(Options options, Engine *engine, QObject *parent)
    : QObject(parent), options_(std::move(options)), engine_(engine)
{
    if (!options_.cookies.isEmpty()) {   // a retry: that file goes with the job it came from
        options_.cookies.clear();
        options_.needsSignIn = true;
    }
    // The sign-in file goes as soon as the job is done, not when its row is removed.
    connect(this, &DownloadJob::finished, this, [this] {
        if (!export_)
            return;
        std::exchange(export_, nullptr)->deleteLater();   // may be inside its own signal
        options_.cookies.clear();
        options_.needsSignIn = true;   // a retry signs in again
    });
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
                checkOutputs();
        }
        else
            recover();
    });
}

DownloadJob::~DownloadJob()
{
    if (gate_) {   // removed while its files were being checked
        gate_->wait();
        delete gate_;
    }
    for (QProcess *p : findChildren<QProcess *>(Qt::FindDirectChildrenOnly))
        ChildProcess::stopAndWait(*p);   // the stream check, if it's still asking
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
    if (options_.needsSignIn && options_.browserSignIn && options_.cookies.isEmpty() && !signInTried_)
        return exportCookies();
    problems_.clear();
    lastLine_.clear();
    file_.clear();
    outputs_.clear();
    if (!startedAt_.isValid())
        startedAt_ = QDateTime::currentDateTime().addSecs(-2);   // a little slack for coarse file times
    tracker_.reset();
    tracker_.setExpected(options_.playlist ? QList<qint64>() : options_.expectedParts);
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
    if (gate_)
        return;   // the check finishes first, then cleans up
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
#pragma endregion

#pragma region Reading yt-dlp output
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
    if (line.startsWith(fileMarker)) {
        const QString path = jsonText(line.mid(fileMarker.size()));
        if (sameFolder(path, options_.folder) && !outputs_.contains(path))
            outputs_.append(file_ = path);
        else
            emit logLine("Ignored a finished file outside the download folder: " + path);
        return;
    }
    if (line == lastLine_)
        return;
    lastLine_ = line;
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
        if (inOurFolders(line.mid(24)))
            created_.append(line.mid(24));
        if (!sidecar_) {
            downloaded_ = true;
            tracker_.startPart();
        }
    }
    else if (line.startsWith("[download] ") && line.endsWith(" has already been downloaded")) {
        tracker_.finishPart();
        skipped_ = true;
    }

    // Later steps make new files: merged, converted, remuxed, moved out of the temp folder.
    static const QRegularExpression merged(R"re(^\[Merger\] Merging formats into "(.+)"$)re");
    static const QRegularExpression moved(R"re(^\[(ExtractAudio|VideoRemuxer|VideoConvertor)\] .*Destination: (.+)$)re");
    static const QRegularExpression home(R"re(^\[MoveFiles\] Moving file ".+" to "(.+)"$)re");
    QString made;
    if (const auto m = merged.match(line); m.hasMatch())
        made = m.captured(1);
    else if (const auto m = moved.match(line); m.hasMatch())
        made = m.captured(2);
    else if (const auto m = home.match(line); m.hasMatch())
        made = m.captured(1);
    if (!made.isEmpty() && inOurFolders(made))
        created_.append(made);
}

// yt-dlp without the strict filter and without downloading: true if the link has any stream.
void DownloadJob::anyStreams(const std::function<void(bool)> &then)
{
    auto *p = new QProcess(this);
    ChildProcess::ownGroup(*p);
    QStringList args = {"--ignore-config", "--no-plugin-dirs", "--encoding", "utf-8", "--simulate", "-f", "bv*+ba/b", "--print", "id",
        options_.playlist ? "--yes-playlist" : "--no-playlist"};
    if (!options_.cookies.isEmpty())
        args << "--cookies" << options_.cookies;
    if (!options_.deno.isEmpty())
        args << "--js-runtimes" << "deno:" + options_.deno;
    args << "--" << options_.url;
    QTimer::singleShot(60000, p, [p] { ChildProcess::stop(*p, true); });
    connect(p, &QProcess::finished, this, [p, then](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        then(status == QProcess::NormalExit && code == 0);
    });
    connect(p, &QProcess::errorOccurred, this, [p, then](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            p->deleteLater();
            then(false);
        }
    });
    emit stage("Checking the site");
    p->start(engine_->path(), args);
}

// Log lines can carry text from the site, so a path there counts only inside our two folders.
bool DownloadJob::inOurFolders(const QString &path) const
{
    return sameFolder(path, options_.folder) || sameFolder(path, options_.tempFolder);
}

// downloaded|total|estimate|speed|eta|fragment|fragments|title, missing numbers come as "NA".
void DownloadJob::handleProgress(const QString &fields)
{
    const QStringList f = fields.split('|');
    if (f.size() < 8)
        return;
    const QString title = jsonText(f.mid(7).join('|'));
    if (title != title_ && !title.isEmpty()) {
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
    emit progress(fraction, tracker_.knownBytes(), speed_, eta_ < 0 ? -1 : qRound(eta_));
}
#pragma endregion

#pragma region Recovering from failures
// Only this site's cookies, in a file of the job's own (see CookieExport); asked for once.
void DownloadJob::exportCookies()
{
    signInTried_ = true;
    const QString browser = defaultBrowserCookies();
    const QString site = siteOf(options_.url);
    if (browser.isEmpty()) {
        emit logLine("The site wants a sign-in, but Queenfisher couldn't find your default browser's cookies.");
        return start();   // without; if the site insists, the failure says so
    }
    emit stage("Signing in");
    emit logLine("Reading the sign-in for " + site + " from " + browserLabel(browser) + ".");
    export_ = new CookieExport(engine_->path(), browser, site, this);
    connect(export_, &CookieExport::finished, this, [this, site](bool ok, const QString &why) {
        if (cancelled_)
            return;   // Stop was pressed meanwhile and has already finished the job
        if (!ok)
            return emit finished(false, why);
        options_.cookies = export_->file();
        emit logLine("Using your browser's sign-in for " + site + " only.");
        start();
    });
    export_->start();
}

// Each fix is tried at most once: the browser's sign-in, then a newer (or nightly) yt-dlp.
void DownloadJob::recover()
{
    const Diagnosis d = diagnose(problems_);

    if (d.problem == Problem::SignIn && options_.cookies.isEmpty() && !options_.browserSignIn) {
        emit logLine("The site wants a sign-in, and browser sign-in isn't allowed for it.");
        emit signInWanted();
    } else if (d.problem == Problem::SignIn && options_.cookies.isEmpty() && !signInTried_) {
        emit logLine("The site wants a sign-in. Trying again with your browser's sign-in.");
        options_.needsSignIn = true;
        start();
        return;
    }

    // Nothing to download with a strict stream filter: either no stream fits this format (no update
    // changes that), or the site changed and offers none at all (the fixes below).
    if (!checkedStreams_ && !Options::streamFilter(options_.format).isEmpty()
        && problems_.join('\n').contains("requested format is not available", Qt::CaseInsensitive)) {
        checkedStreams_ = true;
        QPointer<DownloadJob> self(this);
        anyStreams([self](bool any) {
            if (!self || self->cancelled_)
                return;
            if (!any)
                return self->recover();
            emit self->finished(
                false, "The site has no streams that can become " + Options::name(self->options_.format) + ", so nothing was downloaded");
        });
        return;
    }

    // ffmpeg couldn't make the picked format from what came down: a retry would fail the same way.
    if (problems_.join('\n').contains("conversion failed", Qt::CaseInsensitive)) {
        discardPartial();
        emit finished(false, "It couldn't be turned into " + Options::name(options_.format) + ", so nothing was saved");
        return;
    }

    // A refused download (HTTP 403) is often a one-off on YouTube: one plain retry before
    // deciding yt-dlp needs updating, which could switch it to nightly for nothing.
    if (d.problem == Problem::Outdated && !triedAgain_ && problems_.join('\n').contains("HTTP Error 403")) {
        triedAgain_ = true;
        emit logLine("The site refused the download. Trying once more.");
        start();
        return;
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
#pragma endregion

#pragma region Checking the finished files
// The output gate: every finished file must be the picked format, or it's deleted. Runs on a
// thread of its own (ffprobe takes a moment per file; a playlist has many).
void DownloadJob::checkOutputs()
{
    if (outputs_.isEmpty()) {
        emit finished(false, "yt-dlp didn't report a finished file");
        return;
    }
    const QString ffprobe = OutputGate::ffprobe(options_.ffmpeg);
    if (ffprobe.isEmpty()) {
        emit finished(false, "ffprobe is missing, so the file couldn't be checked");
        return;
    }
    const QStringList files = outputs_;
    auto problems = std::make_shared<QStringList>();
    gate_ = QThread::create([files, problems, ffprobe, format = options_.format] {
        for (const QString &file : files)
            problems->append(OutputGate::problem(file, format, ffprobe));
    });
    connect(gate_, &QThread::finished, this, [this, files, problems] {
        gate_->deleteLater();
        gate_ = nullptr;
        outputsChecked(files, *problems);
    });
    emit stage("Checking");
    gate_->start();
}

void DownloadJob::outputsChecked(const QStringList &files, const QStringList &problems)
{
    if (cancelled_) {   // Stop was pressed while checking
        discardPartial();
        emit finished(false, "Stopped");
        return;
    }
    QString first;
    int bad = 0;
    for (qsizetype i = 0; i < files.size(); ++i) {
        if (problems[i].isEmpty())
            continue;
        ++bad;
        const QFileInfo f(files[i]);
        if (first.isEmpty())
            first = problems[i];
        if (files[i] == file_)
            file_.clear();
        // Only what this download wrote: a file that was already there stays, even if it fails.
        if (f.lastModified() < startedAt_)
            emit logLine(f.fileName() + ": " + problems[i] + " (it was there before, so it stays)");
        else if (QFile::remove(files[i]))
            emit logLine("Deleted " + f.fileName() + ": " + problems[i]);
        else
            emit logLine("Couldn't delete " + f.fileName() + ": " + problems[i]);
    }
    if (bad == 0)
        emit finished(true, "Done");
    else if (files.size() == 1)
        emit finished(false, first.left(1).toUpper() + first.mid(1) + ", so it was deleted");
    else
        emit finished(
            false, QString("%1 of %2 files weren't %3 and were deleted").arg(bad).arg(files.size()).arg(Options::name(options_.format)));
}
#pragma endregion
