#pragma once

#include "ErrorRules.h"
#include "Options.h"
#include "ProgressTracker.h"

#include <QDateTime>
#include <QObject>
#include <QProcess>
#include <QSet>

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
    QString file() const { return file_; }   // the finished file, once yt-dlp has named it
    bool alreadyHad() const { return skipped_ && !downloaded_; }   // the file was there, nothing new

signals:
    void logLine(const QString &line);
    void titleKnown(const QString &title);
    void progress(double fraction, qint64 bytes, double bytesPerSecond, int secondsLeft);  // fraction never drops
    void stage(const QString &text);   // "Merging", "Converting", ...
    void finished(bool ok, const QString &message);

private:
    void readOutput(QProcess::ProcessChannel channel);
    void handleLine(const QString &line);
    void handleProgress(const QString &fields);
    void trackLine(const QString &line);
    void recover();
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
    QStringList created_;      // every file yt-dlp named during this download
    QSet<QString> finishedStems_;   // playlist videos already done: never cleaned up
    QDateTime startedAt_;
    QStringList problems_;   // ERROR and WARNING lines of the current attempt
    bool triedRepair_ = false;
    bool triedAgain_ = false;   // one plain retry after a refused (403) download
    bool cancelled_ = false;
};
