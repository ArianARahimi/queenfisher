#include "FetchedTool.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QNetworkReply>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>

#include <memory>

#pragma region Helpers and location
namespace {

constexpr qint64 retrySoonMs = 60 * 1000;               // after the first failure (often the network still waking up)
constexpr qint64 retryMs = 10 * 60 * 1000;              // after failing again
const char *stampName = ".fetched";                      // holds the SHA-256 of the zip the copy came from

QNetworkRequest request(const QUrl &url, int timeout)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Queenfisher");
    req.setTransferTimeout(timeout);
    return req;
}

bool runs(const QString &program, const QString &flag)
{
    QProcess p;
    p.start(program, {flag});
    return p.waitForFinished(30000) && p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
}

}

FetchedTool::FetchedTool(const QString &dir, const QString &name, const QString &size, QObject *parent)
    : QObject(parent), dir_(dir), name_(name), size_(size)
{
}

QString FetchedTool::exe(const QString &name)
{
#ifdef Q_OS_WIN
    return name + ".exe";
#else
    return name;
#endif
}

QString FetchedTool::unzipper()
{
#ifdef Q_OS_WIN
    return QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/tar.exe");   // bsdtar, since Windows 10
#elif defined(Q_OS_MACOS)
    return "/usr/bin/tar";   // bsdtar
#else
    for (const char *program : {"bsdtar", "unzip", "python3"})
        if (const QString found = QStandardPaths::findExecutable(program); !found.isEmpty())
            return found;
    return {};
#endif
}

QString FetchedTool::location() const
{
    const QDir d(dir_);
    for (const QString &program : programs())
        if (!d.exists(program))
            return {};
    return dir_;
}

bool FetchedTool::installed(const QString &sha256) const
{
    QFile stamp(dir_ + "/" + stampName);
    return stamp.open(QIODevice::ReadOnly) && stamp.read(64) == sha256.toLatin1();
}
#pragma endregion

#pragma region Installing
bool FetchedTool::install(const QString &zip, const QString &expectedSha256, const QString &unzipper, QString *why)
{
    const QString fresh = dir_ + ".new";
    const auto fail = [why, fresh](const QString &message) {
        QDir(fresh).removeRecursively();   // nothing half-made stays behind
        *why = message;
        return false;
    };
    QFile file(zip);
    if (!file.open(QIODevice::ReadOnly))
        return fail("Couldn't read the " + name_ + " download");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || hash.result().toHex() != expectedSha256.toLatin1())
        return fail("The " + name_ + " download doesn't match its checksum");
    file.close();

    // Each of these refuses paths that would leave the folder.
    QTemporaryDir unpacked;
    const QString tool = QFileInfo(unzipper).baseName();
    const QStringList args = tool == "unzip"   ? QStringList{"-q", zip, "-d", unpacked.path()}
                           : tool == "python3" ? QStringList{"-I", "-m", "zipfile", "-e", zip, unpacked.path()}
                                               : QStringList{"-xf", zip, "-C", unpacked.path()};
    QProcess p;
    p.start(unzipper, args);
    if (!p.waitForFinished(300000) || p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        return fail("Couldn't unpack " + name_);

    QDir(fresh).removeRecursively();
    QDir().mkpath(fresh);
    QDirIterator it(unpacked.path(), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo f(it.next());
        if (keep(f) && !QFile::copy(f.absoluteFilePath(), fresh + "/" + f.fileName()))
            return fail("Couldn't save " + name_);
    }
    for (const QString &program : programs()) {
        QFile::setPermissions(fresh + "/" + program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        if (!QFile::exists(fresh + "/" + program) || !runs(fresh + "/" + program, versionFlag()))
            return fail("The " + name_ + " download doesn't work: " + program);
    }
    QFile stamp(fresh + "/" + stampName);
    if (!stamp.open(QIODevice::WriteOnly) || stamp.write(expectedSha256.toLatin1()) != 64)
        return fail("Couldn't save " + name_);
    stamp.close();
    // The old copy goes only now that the new one works; it may be in use, so it's moved
    // aside first and removed after.
    const QString old = dir_ + ".old";
    QDir(old).removeRecursively();
    if (QDir(dir_).exists() && !QDir().rename(dir_, old))
        return fail("Couldn't replace " + name_ + " (is it running?)");
    if (!QDir().rename(fresh, dir_)) {
        QDir().rename(old, dir_);
        return fail("Couldn't put " + name_ + " in place");
    }
    QDir(old).removeRecursively();
    return true;
}
#pragma endregion

#pragma region Fetching
void FetchedTool::ensure()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (busy_ || !wanted() || now - lastTry_ < (failures_ == 1 ? retrySoonMs : retryMs))
        return;
    Source s;
    const bool known = source(&s);
    if (known && !location().isEmpty() && installed(s.sha256))
        return;
    if (!known || unzipper().isEmpty()) {
        if (location().isEmpty() && lastError_.isEmpty()) {
            lastError_ = "Queenfisher can't fetch " + name_ + " for this computer";
            emit readyChanged();
        }
        return;
    }
    const bool first = location().isEmpty();
    busy_ = true;
    lastTry_ = now;
    emit readyChanged();
    emit statusChanged(first ? "Getting " + name_ + " (" + size_ + ", once)..." : "Updating " + name_ + "...");
    emit logLine("Fetching " + name_ + " from " + s.zip.toString());
    download(s);
}

void FetchedTool::download(const Source &s)
{
    // Straight to a file, hashed after: the zip is too big to hold in memory for long.
    auto *file = new QTemporaryFile(QDir::tempPath() + "/queenfisher-" + name_.toLower() + "-XXXXXX.zip", this);
    if (!file->open()) {
        delete file;
        return finish(false, "Couldn't save the " + name_ + " download");
    }
    QNetworkReply *zip = net_.get(request(s.zip, 120000));
    connect(zip, &QNetworkReply::readyRead, file, [zip, file] { file->write(zip->readAll()); });
    connect(zip, &QNetworkReply::downloadProgress, zip, [zip, limit = s.limit](qint64 got, qint64 total) {
        if (got > limit || total > limit)
            zip->abort();   // never fill the disk because a server misbehaves
    });
    connect(zip, &QNetworkReply::finished, this, [this, zip, file, expected = s.sha256] {
        zip->deleteLater();
        file->write(zip->readAll());
        file->close();
        if (zip->error()) {
            delete file;
            return finish(false, "Couldn't download " + name_ + ": " + zip->errorString());
        }
        // Unpacking and trying it takes a few seconds: off the window's thread, so it doesn't freeze.
        auto result = std::make_shared<QString>();
        QThread *worker = QThread::create([this, result, path = file->fileName(), expected] {
            QString why;
            *result = install(path, expected, unzipper(), &why) ? QString() : why;
        });
        connect(worker, &QThread::finished, this, [this, worker, file, result] {
            worker->deleteLater();
            delete file;
            finish(result->isEmpty(), result->isEmpty() ? name_ + " is ready" : *result);
        });
        worker->start();
    });
}

void FetchedTool::finish(bool ok, const QString &message)
{
    busy_ = false;
    failures_ = ok ? 0 : failures_ + 1;
    // A failed refresh keeps the copy it has; a first failure is often just the network waking
    // up (Queenfisher started with the computer), so it's reported only when the retry fails too.
    lastError_ = ok || !location().isEmpty() || failures_ < 2 ? QString() : message;
    emit logLine(message);
    emit statusChanged(message);
    emit readyChanged();
}
#pragma endregion
