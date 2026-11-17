#pragma once

#include "Options.h"

#include <QObject>
#include <QProcess>

// Runs yt-dlp for one link and reports progress, log lines and the result.
class DownloadJob : public QObject
{
    Q_OBJECT

public:
    explicit DownloadJob(Options options, QObject *parent = nullptr);
    ~DownloadJob() override;

    void start(const QString &ytDlp);
    void cancel();
    const Options &options() const { return options_; }

signals:
    void logLine(const QString &line);
    void titleKnown(const QString &title);
    void progress(double fraction, qint64 total, double bytesPerSecond, int secondsLeft);
    void stage(const QString &text);   // "Merging", "Converting", ...
    void finished(bool ok, const QString &message);

private:
    void readOutput(QProcess::ProcessChannel channel);
    void handleLine(const QString &line);
    void handleProgress(const QString &fields);
    QString friendlyError() const;
    bool needsSignIn() const;
    bool retryWithSignIn();

    Options options_;
    QString ytDlp_;
    QProcess process_;
    QByteArray partial_[2];   // unfinished line per channel
    QString title_;
    QString lastError_;
    bool cancelled_ = false;
};
