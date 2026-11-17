#include "YtDlpEngine.h"
#include "PgpVerify.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>

namespace {

const QString fingerprint = "AC0CBBE6848D6A873464AF4E57CF65933B5A7581";   // yt-dlp signing key
constexpr qint64 dayMs = 24LL * 60 * 60 * 1000;
constexpr qint64 smallLimit = 1 << 20;        // checksum files
constexpr qint64 binaryLimit = 200LL << 20;   // yt-dlp itself is about 35 MB

QString repo(YtDlpEngine::Channel c)
{
    return c == YtDlpEngine::Stable ? "yt-dlp/yt-dlp" : "yt-dlp/yt-dlp-nightly-builds";
}

QString channelName(YtDlpEngine::Channel c) { return c == YtDlpEngine::Stable ? "stable" : "nightly"; }

QByteArray signingKey()
{
    QFile f(":/ytdlp-public.key");
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

}

YtDlpEngine::YtDlpEngine(const QString &dataDir, QObject *parent) : Engine(parent), dir_(dataDir + "/bin")
{
    QDir().mkpath(dir_);
    loadStamp();
}

QString YtDlpEngine::managedPath() const
{
#ifdef Q_OS_WIN
    return dir_ + "/yt-dlp.exe";
#else
    return dir_ + "/yt-dlp";
#endif
}

// Queenfisher's own copy first; a system copy only until that exists.
QString YtDlpEngine::path() const
{
    return QFile::exists(managedPath()) ? managedPath() : QStandardPaths::findExecutable("yt-dlp");
}

QString YtDlpEngine::version() const
{
    if (QFile::exists(managedPath()) && !stamp_.version.isEmpty())
        return QString("%1 (%2)").arg(stamp_.version, channelName(stamp_.channel));
    return path().isEmpty() ? QString() : QString("system copy");
}

void YtDlpEngine::checkDaily()
{
    const auto quiet = [](bool, const QString &) {};
    if (!QFile::exists(managedPath()) || stamp_.channel == Stable) {
        run(Stable, false, quiet);
        return;
    }
    // On nightly after a rescue: go back to stable once it has caught up.
    if (busy_ || QDateTime::currentMSecsSinceEpoch() - stamp_.checked < dayMs)
        return;
    latestTag(Stable, [this, quiet](const QString &tag) {
        const bool caughtUp = !tag.isEmpty() && compareVersions(tag, stamp_.version, 3) >= 0;
        run(caughtUp ? Stable : Nightly, caughtUp, quiet);
    });
}

void YtDlpEngine::updateNow()
{
    run(QFile::exists(managedPath()) ? stamp_.channel : Stable, true, [](bool, const QString &) {});
}

void YtDlpEngine::switchChannel(Channel channel)
{
    run(channel, true, [](bool, const QString &) {});
}

// Update the current channel; if that changes nothing, try nightly, which gets site fixes first.
void YtDlpEngine::repair(Done done)
{
    if (!QFile::exists(managedPath())) {
        run(Stable, true, done);
        return;
    }
    run(stamp_.channel, true, [this, done](bool changed, const QString &message) {
        if (changed || stamp_.channel == Nightly)
            done(changed, message);
        else
            run(Nightly, true, done);
    });
}

void YtDlpEngine::run(Channel channel, bool force, Done done)
{
    if (busy_) {
        waiting_.append(done);
        return;
    }
    const bool installed = QFile::exists(managedPath());
    if (!force && installed && stamp_.channel == channel
        && QDateTime::currentMSecsSinceEpoch() - stamp_.checked < dayMs) {
        done(false, "yt-dlp was checked today");
        return;
    }
    if (assetName().isEmpty()) {
        done(false, "Queenfisher can't fetch yt-dlp for this kind of computer");
        return;
    }
    busy_ = true;
    waiting_.append(done);
    emit readyChanged();
    emit statusChanged(installed ? "Checking for a newer yt-dlp..." : "Getting yt-dlp...");

    latestTag(channel, [this, channel, installed](const QString &tag) {
        if (tag.isEmpty())
            return finish(false, "Couldn't reach GitHub to check yt-dlp");
        if (installed && tag == stamp_.version && channel == stamp_.channel) {
            stamp_.checked = QDateTime::currentMSecsSinceEpoch();
            saveStamp();
            return finish(false, "yt-dlp " + tag + " is the newest " + channelName(channel));
        }
        install(channel, tag);
    });
}

void YtDlpEngine::latestTag(Channel channel, std::function<void(const QString &)> then)
{
    // /releases/latest redirects to /releases/tag/<tag>; no API token or JSON needed.
    QNetworkRequest req(QUrl("https://github.com/" + repo(channel) + "/releases/latest"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Queenfisher");
    req.setTransferTimeout(30000);
    QNetworkReply *reply = net_.head(req);
    connect(reply, &QNetworkReply::finished, this, [reply, then] {
        reply->deleteLater();
        const QString target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl().path();
        const QString tag = target.section('/', -1);
        static const QRegularExpression valid(R"(^\d{4}\.\d{1,2}\.\d{1,2}(\.\d+)?$)");
        then(target.contains("/releases/tag/") && valid.match(tag).hasMatch() ? tag : QString());
    });
}

void YtDlpEngine::fetch(const QUrl &url, qint64 limit, std::function<void(QNetworkReply *)> then)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Queenfisher");
    req.setTransferTimeout(60000);
    QNetworkReply *reply = net_.get(req);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, limit](qint64 got, qint64 total) {
        if (got > limit || total > limit)
            reply->abort();   // never fill the disk because a server misbehaves
    });
    connect(reply, &QNetworkReply::finished, this, [reply, then] {
        reply->deleteLater();
        then(reply);
    });
}

void YtDlpEngine::install(Channel channel, const QString &tag)
{
    const QString base = "https://github.com/" + repo(channel) + "/releases/download/" + tag + "/";
    emit logLine(QString("Fetching yt-dlp %1 (%2)").arg(tag, channelName(channel)));

    fetch(QUrl(base + "SHA2-256SUMS"), smallLimit, [=, this](QNetworkReply *sums) {
        if (sums->error())
            return finish(false, "Couldn't download yt-dlp's checksums: " + sums->errorString());
        sums_ = sums->readAll();
        fetch(QUrl(base + "SHA2-256SUMS.sig"), smallLimit, [=, this](QNetworkReply *sig) {
            if (sig->error())
                return finish(false, "Couldn't download yt-dlp's signature: " + sig->errorString());
            const Pgp::Result check = Pgp::verifyDetached(sums_, sig->readAll(), signingKey(), fingerprint);
            if (!check.ok)
                return finish(false, "Refused yt-dlp " + tag + ": " + check.why);
            emit logLine("Signature by the yt-dlp developers checked");

            const QString expected = checksumFor(sums_, assetName());
            if (expected.isEmpty())
                return finish(false, "yt-dlp " + tag + " has no build for this computer");
            emit statusChanged("Downloading yt-dlp " + tag + "...");
            fetch(QUrl(base + assetName()), binaryLimit, [=, this](QNetworkReply *bin) {
                if (bin->error())
                    return finish(false, "Couldn't download yt-dlp: " + bin->errorString());
                const QByteArray data = bin->readAll();
                if (QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex() != expected.toLatin1())
                    return finish(false, "Refused yt-dlp " + tag + ": the download doesn't match its checksum");
                const QString fresh = managedPath() + ".new";
                QFile out(fresh);
                if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(data) != data.size())
                    return finish(false, "Couldn't save yt-dlp: " + out.errorString());
                out.close();
                out.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner | QFile::ReadGroup
                                   | QFile::ExeGroup | QFile::ReadOther | QFile::ExeOther);
                stamp_.channel = channel;   // only saved once the new copy is in place
                smokeTest(fresh, tag);
            });
        });
    });
}

// The new copy has to start and report the expected version before it replaces the old one.
void YtDlpEngine::smokeTest(const QString &file, const QString &tag)
{
    auto *p = new QProcess(this);
    QTimer::singleShot(60000, p, [p] { p->kill(); });
    connect(p, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        const QString said = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
        if (status != QProcess::NormalExit || code != 0 || said != tag) {
            QFile::remove(file);
            loadStamp();
            return finish(false, "The new yt-dlp " + tag + " didn't start properly, keeping the old one");
        }
        const QString current = managedPath(), previous = managedPath() + ".previous";
        QFile::remove(previous);
        if (QFile::exists(current) && !QFile::rename(current, previous))
            return finish(false, "Couldn't replace yt-dlp (is it running?)");
        if (!QFile::rename(file, current)) {
            QFile::rename(previous, current);
            return finish(false, "Couldn't put the new yt-dlp in place");
        }
        stamp_.version = tag;
        stamp_.checked = QDateTime::currentMSecsSinceEpoch();
        saveStamp();
        finish(true, QString("yt-dlp %1 (%2) installed").arg(tag, channelName(stamp_.channel)));
    });
    p->start(file, {"--version"});
}

void YtDlpEngine::finish(bool changed, const QString &message)
{
    busy_ = false;
    emit logLine(message);
    emit statusChanged(message);
    emit readyChanged();
    for (const Done &done : std::exchange(waiting_, {}))
        done(changed, message);
}

QString YtDlpEngine::assetName()
{
    const QString cpu = QSysInfo::currentCpuArchitecture();
#if defined(Q_OS_LINUX)
    if (cpu == "x86_64") return "yt-dlp_linux";
    if (cpu == "arm64") return "yt-dlp_linux_aarch64";
#elif defined(Q_OS_WIN)
    if (cpu == "x86_64" || cpu == "arm64") return "yt-dlp.exe";
#elif defined(Q_OS_MACOS)
    return "yt-dlp_macos";
#endif
    Q_UNUSED(cpu);
    return {};
}

// "<64 hex>  <name>" lines; the name must match exactly.
QString YtDlpEngine::checksumFor(const QByteArray &sums, const QString &asset)
{
    static const QRegularExpression line(R"(^([0-9a-f]{64}) [ *]?(\S+)$)");
    for (const QByteArray &raw : sums.split('\n')) {
        const auto m = line.match(QString::fromLatin1(raw).trimmed());
        if (m.hasMatch() && m.captured(2) == asset)
            return m.captured(1);
    }
    return {};
}

// Compares dotted numbers like 2026.10.26 and 2026.11.16.232655, first `parts` fields only.
int YtDlpEngine::compareVersions(const QString &a, const QString &b, int parts)
{
    const QStringList x = a.split('.'), y = b.split('.');
    for (int i = 0; i < parts && (i < x.size() || i < y.size()); ++i) {
        const qlonglong p = x.value(i).toLongLong(), q = y.value(i).toLongLong();
        if (p != q)
            return p < q ? -1 : 1;
    }
    return 0;
}

void YtDlpEngine::loadStamp()
{
    QFile f(managedPath() + ".json");
    const QJsonObject o = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    stamp_.version = o.value("version").toString();
    stamp_.channel = o.value("channel").toString() == "nightly" ? Nightly : Stable;
    stamp_.checked = qint64(o.value("checked").toDouble());
}

void YtDlpEngine::saveStamp() const
{
    QFile f(managedPath() + ".json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(QJsonObject{{"version", stamp_.version},
                                          {"channel", channelName(stamp_.channel)},
                                          {"checked", double(stamp_.checked)}}).toJson());
}
