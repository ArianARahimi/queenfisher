#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

class Engine;

// What the preview card shows for a link.
struct PreviewInfo
{
    QString title;
    QString channel;
    QString site;
    int seconds = -1;        // length, -1 if unknown (live streams, playlists)
    bool live = false;
    int playlistCount = -1;  // number of videos when the link is a playlist
    QString thumbnail;       // picture URL, JPEG preferred
};

// Reads yt-dlp's JSON for one video or playlist; empty title means it couldn't.
PreviewInfo parsePreview(const QByteArray &json);

// Asks yt-dlp about a link without downloading it. Retries once with the browser's sign-in
// when the site asks for one, and remembers that sign-in for the download that follows.
class PreviewJob : public QObject
{
    Q_OBJECT

public:
    PreviewJob(QString url, bool playlist, Engine *engine, QObject *parent = nullptr);
    ~PreviewJob() override;
    void start();
    QString url() const { return url_; }
    QString cookies() const { return cookies_; }   // set when the sign-in was needed

signals:
    void ready(const PreviewInfo &info);
    void failed(const QString &message);
    void logLine(const QString &line);

private:
    void finishedRun(int code, QProcess::ExitStatus status);

    QString url_;
    bool playlist_;
    Engine *engine_;
    QString cookies_;
    QProcess process_;
    QTimer timeout_;   // restarted for each attempt
};
