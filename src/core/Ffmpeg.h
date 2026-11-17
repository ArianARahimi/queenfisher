#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

class QNetworkReply;

// ffmpeg, which yt-dlp needs to join picture and sound and to make MP3s. Linux and macOS
// use the one installed on the computer. Windows rarely has one, so Queenfisher fetches
// yt-dlp's own build once, checked against the release's SHA-256 checksums.
class Ffmpeg : public QObject
{
    Q_OBJECT

public:
    explicit Ffmpeg(const QString &dataDir, QObject *parent = nullptr);

    QString location() const;   // folder to hand yt-dlp (--ffmpeg-location); empty = yt-dlp looks itself
    bool busy() const { return busy_; }
    QString lastError() const { return lastError_; }   // why the last fetch failed; empty once it works
    void ensure();              // Windows: fetch it if missing; call when idle. Elsewhere: nothing.

    // The parts that don't need the network, public for tests.
    // Checks the zip's SHA-256, unpacks it with `tar` and keeps only ffmpeg, ffprobe and DLLs.
    bool install(const QString &zip, const QString &expectedSha256, const QString &tar, QString *why);
    static QString exe(const QString &name);   // "ffmpeg.exe" on Windows, "ffmpeg" elsewhere

signals:
    void statusChanged(const QString &text);
    void logLine(const QString &line);
    void readyChanged();   // location() or busy() changed

private:
    void finish(bool ok, const QString &message);

    QString dir_;
    bool busy_ = false;
    QString lastError_;
    qint64 lastTry_ = 0;
    QNetworkAccessManager net_;
};
