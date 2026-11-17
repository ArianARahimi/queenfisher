#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>

class Engine;

// Downloads a short, low-resolution piece of a video, with sound, for the preview to play.
// Going through yt-dlp (not a direct stream link) keeps sign-ins and site quirks working.
class ClipJob : public QObject
{
    Q_OBJECT

public:
    ClipJob(QString url, int startSeconds, QString folder, QString cookies, Engine *engine, QObject *parent = nullptr);
    ~ClipJob() override;
    void start();
    QString url() const { return url_; }

    static constexpr int lengthSeconds = 12;
    static QStringList args(const QString &url, int startSeconds, const QString &folder, const QString &cookies);

signals:
    void ready(const QString &file);
    void failed(const QString &message);
    void logLine(const QString &line);

private:
    void finishedRun(int code, QProcess::ExitStatus status);

    QString url_;
    int start_;
    QString folder_;
    QString cookies_;
    Engine *engine_;
    QProcess process_;
    QTimer timeout_;
};
