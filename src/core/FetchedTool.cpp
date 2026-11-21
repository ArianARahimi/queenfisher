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
const char *stampName = ".fetched";   // the zip's SHA-256, then "<SHA-256> <name>" for each file kept

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

QByteArray fileHash(const QString &file)
{
    QFile f(file);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return f.open(QIODevice::ReadOnly) && hash.addData(&f) ? hash.result().toHex() : QByteArray();
}

// The files in a tool's folder, the stamp aside, by name.
QStringList filesIn(const QString &dir)
{
    QStringList names = QDir(dir).entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Name);
    names.removeAll(stampName);
    return names;
}
}

FetchedTool::FetchedTool(const QString &dir, const QString &name, const QString &size, QObject *parent)
    : QObject(parent), dir_(dir), name_(name), size_(size)
{
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { intact_ = -1; });
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] { intact_ = -1; });
}

void FetchedTool::watch() const
{
    if (!watcher_.files().isEmpty())
        watcher_.removePaths(watcher_.files());
    if (!QDir(dir_).exists())
        return;
    if (!watcher_.directories().contains(dir_))
        watcher_.addPath(dir_);
    for (const QString &name : filesIn(dir_))
        watcher_.addPath(dir_ + "/" + name);
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
    if (intact_ < 0) {   // once, then again only after the folder changes
        watch();
        intact_ = intact();
    }
    return intact_ ? dir_ : QString();
}

bool FetchedTool::installed(const QString &sha256) const
{
    QFile stamp(dir_ + "/" + stampName);
    return stamp.open(QIODevice::ReadOnly) && stamp.read(64) == sha256.toLatin1();
}

// A copy from before files were noted has no list, so it's fetched again once.
bool FetchedTool::intact() const
{
    QFile stamp(dir_ + "/" + stampName);
    if (!stamp.open(QIODevice::ReadOnly))
        return false;
    QStringList listed;
    const QList<QByteArray> lines = stamp.readAll().split('\n');
    for (const QByteArray &line : lines.mid(1)) {
        const qsizetype space = line.indexOf(' ');
        if (line.isEmpty())
            continue;
        const QString name = QString::fromUtf8(line.mid(space + 1));
        if (space != 64 || name.contains('/') || fileHash(dir_ + "/" + name) != line.left(64))
            return false;
        listed << name;
    }
    listed.sort();
    return !listed.isEmpty() && listed == filesIn(dir_);
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
    QByteArray stamped = expectedSha256.toLatin1() + "\n";   // what each file is, so changes show later
    for (const QString &name : filesIn(fresh))
        stamped += fileHash(fresh + "/" + name) + " " + name.toUtf8() + "\n";
    for (const QString &program : programs()) {
        QFile::setPermissions(fresh + "/" + program, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        if (!QFile::exists(fresh + "/" + program) || !runs(fresh + "/" + program, versionFlag()))
            return fail("The " + name_ + " download doesn't work: " + program);
    }
    QFile stamp(fresh + "/" + stampName);
    if (!stamp.open(QIODevice::WriteOnly) || stamp.write(stamped) != stamped.size())
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
    if (first && intact_ == 0)
        emit logLine("The " + name_ + " in Queenfisher's folder isn't the copy it checked, so it won't be used");
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
    intact_ = -1;   // a new copy, or the folder as the failed install left it
    failures_ = ok ? 0 : failures_ + 1;
    // A failed refresh keeps the copy it has; a first failure is often just the network waking
    // up (Queenfisher started with the computer), so it's reported only when the retry fails too.
    lastError_ = ok || !location().isEmpty() || failures_ < 2 ? QString() : message;
    emit logLine(message);
    emit statusChanged(message);
    emit readyChanged();
}
#pragma endregion
