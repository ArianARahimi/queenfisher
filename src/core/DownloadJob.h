#pragma once

#include "ErrorRules.h"
#include "Options.h"
#include "ProgressTracker.h"

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QSet>

#include <functional>

class CookieExport;
class QThread;

// Runs yt-dlp for one link and reports progress, log lines and the result.
// When it fails, it tries the fixes ErrorRules points to before giving up.
class Engine;

class DownloadJob : public QObject
{
    Q_OBJECT

public:
    DownloadJob(Options options, Engine *engine, QObject *parent = nullptr);
    ~DownloadJob() override;

    void start();
    void cancel();           // stops yt-dlp and anything it started, then deletes this download's files
    void discardPartial();   // deletes leftovers of an unfinished download (.part, pieces, subtitles)
    const Options &options() const { return options_; }
    QString file() const { return file_; }   // the finished file, as yt-dlp reported it once in place
    bool alreadyHad() const { return skipped_ && !downloaded_; }   // the file was there, nothing new

signals:
    void logLine(const QString &line);
    void titleKnown(const QString &title);
    void progress(double fraction, qint64 bytes, double bytesPerSecond, int secondsLeft);  // fraction never drops
    void stage(const QString &text);   // "Merging", "Converting", ...
    void finished(bool ok, const QString &message);
    void signInWanted();   // the site wants a sign-in this job wasn't allowed to use; sent before finished

private:
    void readOutput(QProcess::ProcessChannel channel);
    void handleLine(const QString &line);
    void handleProgress(const QString &fields);
    void trackLine(const QString &line);
    void recover();
    void exportCookies();
    void anyStreams(const std::function<void(bool)> &then);   // does the site have any at all?
    void checkOutputs();   // the output gate, off the window's thread
    void outputsChecked(const QStringList &files, const QStringList &problems);
    bool inOurFolders(const QString &path) const;
    void stopAll(bool hard);

    Options options_;
    Engine *engine_;
    QProcess process_;
    QByteArray partial_[2];   // unfinished line per channel
    ProgressTracker tracker_;
    double speed_ = -1, eta_ = -1;   // smoothed, so the numbers don't flicker
    bool skipped_ = false;           // yt-dlp found a file already there
    bool downloaded_ = false;        // ...and whether anything was downloaded anyway
    bool sidecar_ = false;           // a subtitle or thumbnail is downloading, not the video
    QString title_;
    QString file_;
    QStringList created_;      // every file yt-dlp named during this download, in our folders only
    QStringList outputs_;      // finished files yt-dlp reported, in the download folder only
    QThread *gate_ = nullptr;   // checking outputs_
    QSet<QString> finishedStems_;   // playlist videos already done: never cleaned up
    QDateTime startedAt_;
    QStringList problems_;   // ERROR and WARNING lines of the current attempt
    QString lastLine_;       // yt-dlp can print an error twice: the log shows it once
    bool triedRepair_ = false;
    bool checkedStreams_ = false;   // asked once whether a strict filter or the site left nothing
    bool triedAgain_ = false;   // one plain retry after a refused (403) download
    bool cancelled_ = false;
    bool signInTried_ = false;    // the browser's sign-in was asked for once already
    CookieExport *export_ = nullptr;   // owns the file options_.cookies names
};
