#pragma once

#include "Options.h"

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

class CookieExport;
class Engine;
class QUrl;

// One of the video's streams, as yt-dlp lists them: picture, sound, or both.
struct Stream
{
    int height = 0;      // picture height; 0 for sound only
    double kbps = 0;     // average bitrate
    qint64 bytes = -1;   // size, exact or estimated; -1 if unknown
    QString ext, vcodec, acodec;
    bool hls = false;    // streamed in pieces (m3u8); yt-dlp prefers plain downloads
    bool picture() const { return height > 0 && !vcodec.isEmpty() && vcodec != "none"; }
    bool sound() const { return !acodec.isEmpty() && acodec != "none"; }
};

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
    QList<Stream> streams;   // empty for playlists
};

// The tallest picture this format can get from the video (MP4: H.264 only, WebM: WebM only);
// 0 if unknown.
int bestHeight(const PreviewInfo &info, Options::Format format);

// The sound "original audio" keeps, named for the format list ("Opus", "AAC"...); empty if
// unknown. Chosen the way yt-dlp ranks sound codecs, not by bitrate alone.
QString originalAudioName(const PreviewInfo &info);

// Roughly how big the download will be with these choices, picking streams the way yt-dlp
// does for them (H.264 and AAC for MP4, WebM for WebM); -1 if the sizes aren't known.
qint64 estimateSize(const PreviewInfo &info, Options::Format format, int maxHeight);

// Picture then sound for a two-part video download, as estimateSize reckons them; empty when
// it comes as one file or the sizes aren't known. Lets the bar weigh the parts from the start.
QList<qint64> estimateParts(const PreviewInfo &info, Options::Format format, int maxHeight);

// The 11-character video ID in a YouTube link (watch, youtu.be, shorts, embed, live, music);
// empty for anything else. Lets the picture and title come straight from YouTube while yt-dlp
// works out the rest.
QString youTubeId(const QString &link);

// False for this computer or its network, written out (localhost, 192.168.x.x...); checked
// for a thumbnail's address and each redirect it takes.
bool publicHost(const QUrl &url);

// A name's looked-up addresses: false if there are none or any is this computer or its network
// (127.0.0.1.nip.io is a public name for 127.0.0.1).
bool publicAddresses(const QList<QHostAddress> &addresses);

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
    QString cookies() const { return cookies_; }   // this site's cookies file, once a sign-in was needed
    void setDeno(const QString &program) { deno_ = program; }   // Queenfisher's own, if any
    void setBrowserSignIn(bool on) { browserSignIn_ = on; }      // off: never the browser's cookies

signals:
    void ready(const PreviewInfo &info);
    void failed(const QString &message);
    void logLine(const QString &line);
    void signInWanted();   // the site wants a sign-in this preview wasn't allowed to use; sent before failed

private:
    void finishedRun(int code, QProcess::ExitStatus status);

    QString url_;
    bool playlist_;
    Engine *engine_;
    QString cookies_;
    CookieExport *export_ = nullptr;   // owns the file cookies_ names
    QString deno_;
    bool browserSignIn_ = false;
    QProcess process_;
    QTimer timeout_;   // restarted for each attempt
};
