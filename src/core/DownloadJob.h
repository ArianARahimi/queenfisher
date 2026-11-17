#pragma once

#include "ErrorRules.h"
#include "Options.h"
#include "ProgressTracker.h"

#include <QObject>
#include <QProcess>

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
    void cancel();
    const Options &options() const { return options_; }
    QString file() const { return file_; }   // the finished file, once yt-dlp has named it

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

    Options options_;
    Engine *engine_;
    QProcess process_;
    QByteArray partial_[2];   // unfinished line per channel
    ProgressTracker tracker_;
    double speed_ = -1, eta_ = -1;   // smoothed, so the numbers don't flicker
    bool sidecar_ = false;           // a subtitle or thumbnail is downloading, not the video
    QString title_;
    QString file_;
    QStringList problems_;   // ERROR and WARNING lines of the current attempt
    bool triedRepair_ = false;
    bool cancelled_ = false;
};
