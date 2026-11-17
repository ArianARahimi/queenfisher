#include "DownloadJob.h"
#include "Cookies.h"

#include <QRegularExpression>

DownloadJob::DownloadJob(Options options, QObject *parent) : QObject(parent), options_(std::move(options))
{
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
        if (cancelled_)
            emit finished(false, "Stopped");
        else if (status == QProcess::NormalExit && code == 0)
            emit finished(true, "Done");
        else if (!retryWithSignIn())
            emit finished(false, friendlyError());
    });
}

DownloadJob::~DownloadJob()
{
    if (process_.state() != QProcess::NotRunning) {
        disconnect(&process_, nullptr, this, nullptr);   // no signals from a half-destroyed job
        process_.kill();
        process_.waitForFinished(2000);
    }
}

void DownloadJob::start(const QString &ytDlp)
{
    ytDlp_ = ytDlp;
    lastError_.clear();
    tracker_.reset();
    speed_ = eta_ = -1;
    sidecar_ = false;
    const QStringList args = ytDlpArgs(options_);
    emit logLine("> yt-dlp " + args.join(' '));
    process_.start(ytDlp, args);
}

void DownloadJob::cancel()
{
    cancelled_ = true;
    process_.terminate();   // lets yt-dlp tidy up its part files
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
    if (line.startsWith("ERROR:"))
        lastError_ = line.mid(6).trimmed();
    else if (line.startsWith("[Merger]"))
        emit stage("Merging");
    else if (line.startsWith("[ExtractAudio]"))
        emit stage("Converting");
    else if (line.startsWith("[EmbedSubtitle]"))
        emit stage("Adding subtitles");
}

// Lines that tell the tracker a new playlist video, a new file or a finished file has begun.
void DownloadJob::trackLine(const QString &line)
{
    static const QRegularExpression item(R"(^\[download\] Downloading item (\d+) of (\d+))");
    static const QRegularExpression formats(R"(^\[info\] .*Downloading \d+ format\(s\): (\S+))");
    static const QRegularExpression sidecar(R"(\.(vtt|srt|ass|lrc|json|jpe?g|png|webp)$)", QRegularExpression::CaseInsensitiveOption);

    if (const auto m = item.match(line); m.hasMatch())
        tracker_.startItem(m.captured(1).toInt(), m.captured(2).toInt());
    else if (const auto m = formats.match(line); m.hasMatch())
        tracker_.setPartCount(int(m.captured(1).count('+')) + 1);
    else if (line.startsWith("[download] Destination: ")) {
        sidecar_ = sidecar.match(line).hasMatch();   // subtitles and thumbnails aren't parts of the video
        if (!sidecar_)
            tracker_.startPart();
    }
    else if (line.startsWith("[download] ") && line.endsWith("has already been downloaded"))
        tracker_.finishPart();
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

bool DownloadJob::needsSignIn() const
{
    return lastError_.contains("Sign in to confirm", Qt::CaseInsensitive)
        || lastError_.contains("--cookies", Qt::CaseInsensitive);
}

// Account cookies are only sent when the site insists, and only one retry is made.
bool DownloadJob::retryWithSignIn()
{
    if (!needsSignIn() || !options_.cookies.isEmpty())
        return false;
    options_.cookies = defaultBrowserCookies();
    if (options_.cookies.isEmpty()) {
        emit logLine("The site wants a sign-in, but QFisher couldn't find your default browser's cookies.");
        return false;
    }
    emit logLine("The site wants a sign-in. Trying again with your browser's sign-in ("
                 + options_.cookies.section(':', 0, 0) + ").");
    emit stage("Signing in");
    start(ytDlp_);
    return true;
}

// yt-dlp's errors in words a non-programmer understands; the log keeps the original.
QString DownloadJob::friendlyError() const
{
    const QString e = lastError_;
    if (needsSignIn())
        return options_.cookies.isEmpty() ? "The site wants a sign-in, no browser found"
                                          : "Sign in to the site in your browser, then retry";
    if (e.contains("Unsupported URL"))
        return "This site isn't supported";
    if (e.contains("Private video") || e.contains("Video unavailable"))
        return "The video is private or gone";
    if (e.contains("HTTP Error 403"))
        return "The site refused the download, try again";
    if (e.contains("ffmpeg", Qt::CaseInsensitive))
        return "ffmpeg is missing (needed to join video and sound)";
    if (e.contains("Unable to download") || e.contains("timed out") || e.contains("Connection"))
        return "Network problem, check your connection";
    return e.isEmpty() ? "Failed, see the log" : "Failed: " + e.left(80);
}
