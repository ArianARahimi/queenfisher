#include "Ffmpeg.h"
#include "YtDlpEngine.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QNetworkReply>
#include <QProcess>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>

#include <memory>

namespace {

#ifdef Q_OS_WIN
const QString release = "https://github.com/yt-dlp/FFmpeg-Builds/releases/download/latest/";
const QString asset = "ffmpeg-master-latest-win64-gpl-shared.zip";
constexpr qint64 zipLimit = 400LL << 20;      // the zip is about 90 MB
constexpr qint64 retryMs = 10 * 60 * 1000;    // after a failed fetch
#endif

bool runs(const QString &program)
{
    QProcess p;
    p.start(program, {"-version"});
    return p.waitForFinished(30000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

}

Ffmpeg::Ffmpeg(const QString &dataDir, QObject *parent) : QObject(parent), dir_(dataDir + "/ffmpeg") {}

QString Ffmpeg::exe(const QString &name)
{
#ifdef Q_OS_WIN
    return name + ".exe";
#else
    return name;
#endif
}

QString Ffmpeg::location() const
{
    const QDir d(dir_);
    return d.exists(exe("ffmpeg")) && d.exists(exe("ffprobe")) ? dir_ : QString();
}

bool Ffmpeg::install(const QString &zip, const QString &expectedSha256, const QString &tar, QString *why)
{
    const auto fail = [why](const QString &message) { *why = message; return false; };
    QFile file(zip);
    if (!file.open(QIODevice::ReadOnly))
        return fail("Couldn't read the ffmpeg download");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result().toHex() != expectedSha256.toLatin1())
        return fail("The ffmpeg download doesn't match its checksum");
    file.close();

    QTemporaryDir unpacked;
    QProcess p;
    p.start(tar, {"-xf", zip, "-C", unpacked.path()});   // tar refuses paths that leave the folder
    if (!p.waitForFinished(300000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return fail("Couldn't unpack ffmpeg");

    // Only the programs and their libraries, from the bin folder; nothing else is kept.
    const QString fresh = dir_ + ".new";
    QDir(fresh).removeRecursively();
    QDir().mkpath(fresh);
    QDirIterator it(unpacked.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo f(it.next());
        const QString name = f.fileName();
        const bool wanted = name == exe("ffmpeg") || name == exe("ffprobe") || name.endsWith(".dll", Qt::CaseInsensitive);
        if (wanted && f.absoluteDir().dirName() == "bin" && !QFile::copy(f.absoluteFilePath(), fresh + "/" + name))
            return fail("Couldn't save ffmpeg");
    }
    for (const QString &name : {exe("ffmpeg"), exe("ffprobe")}) {
        QFile::setPermissions(fresh + "/" + name, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        if (!QFile::exists(fresh + "/" + name) || !runs(fresh + "/" + name))
            return fail("The ffmpeg download doesn't work: " + name);
    }
    QDir(dir_).removeRecursively();
    if (!QDir().rename(fresh, dir_))
        return fail("Couldn't put ffmpeg in place");
    return true;
}

void Ffmpeg::ensure()
{
#ifdef Q_OS_WIN
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (busy_ || !location().isEmpty() || now - lastTry_ < retryMs)
        return;
    busy_ = true;
    lastTry_ = now;
    emit readyChanged();
    emit statusChanged("Getting ffmpeg (about 90 MB, once)...");
    emit logLine("Fetching ffmpeg from yt-dlp's FFmpeg builds");

    QNetworkRequest sumsReq(QUrl(release + "checksums.sha256"));
    sumsReq.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    sumsReq.setHeader(QNetworkRequest::UserAgentHeader, "Queenfisher");
    sumsReq.setTransferTimeout(60000);
    QNetworkReply *sums = net_.get(sumsReq);
    connect(sums, &QNetworkReply::finished, this, [this, sums] {
        sums->deleteLater();
        const QString expected = sums->error() ? QString() : YtDlpEngine::checksumFor(sums->read(1 << 20), asset);
        if (expected.isEmpty())
            return finish(false, "Couldn't get ffmpeg's checksums");

        // Straight to a file, hashed after: the zip is too big to hold in memory for long.
        auto *file = new QTemporaryFile(QDir::tempPath() + "/queenfisher-ffmpeg-XXXXXX.zip", this);
        if (!file->open()) {
            delete file;
            return finish(false, "Couldn't save the ffmpeg download");
        }
        QNetworkRequest req(QUrl(release + asset));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::UserAgentHeader, "Queenfisher");
        req.setTransferTimeout(120000);
        QNetworkReply *zip = net_.get(req);
        connect(zip, &QNetworkReply::readyRead, file, [zip, file] { file->write(zip->readAll()); });
        connect(zip, &QNetworkReply::downloadProgress, zip, [zip](qint64 got, qint64 total) {
            if (got > zipLimit || total > zipLimit)
                zip->abort();   // never fill the disk because a server misbehaves
        });
        connect(zip, &QNetworkReply::finished, this, [this, zip, file, expected] {
            zip->deleteLater();
            file->write(zip->readAll());
            file->close();
            if (zip->error()) {
                delete file;
                return finish(false, "Couldn't download ffmpeg: " + zip->errorString());
            }
            // Unpacking and trying it takes a few seconds: off the window's thread, so it doesn't freeze.
            const QString tar = QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/tar.exe");
            auto result = std::make_shared<QString>();
            QThread *worker = QThread::create([this, result, path = file->fileName(), expected, tar] {
                QString why;
                *result = install(path, expected, tar, &why) ? QString() : why;
            });
            connect(worker, &QThread::finished, this, [this, worker, file, result] {
                worker->deleteLater();
                delete file;
                finish(result->isEmpty(), result->isEmpty() ? QString("ffmpeg is ready") : *result);
            });
            worker->start();
        });
    });
#endif
}

void Ffmpeg::finish(bool ok, const QString &message)
{
    busy_ = false;
    lastError_ = ok ? QString() : message;
    emit logLine(message);
    emit statusChanged(message);
    emit readyChanged();
}
