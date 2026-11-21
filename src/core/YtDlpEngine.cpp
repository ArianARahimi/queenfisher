#include "YtDlpEngine.h"
#include "ChildProcess.h"
#include "PgpVerify.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>
#include <QTimer>

namespace {

const QString fingerprint = "AC0CBBE6848D6A873464AF4E57CF65933B5A7581";   // yt-dlp signing key
constexpr qint64 dayMs = 24LL * 60 * 60 * 1000;
constexpr qint64 retryMs = 60 * 1000;          // automatic checks after a failure wait this long
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

#pragma region Checking for updates
YtDlpEngine::YtDlpEngine(const QString &dataDir, QObject *parent) : Engine(parent), dir_(dataDir + "/bin")
{
    QDir().mkpath(dir_);
    loadStamp();
    recognize();
    // A copy by hand is looked at once it has been quiet for a moment, not halfway through.
    settle_.setSingleShot(true);
    settle_.setInterval(2000);
    connect(&settle_, &QTimer::timeout, this, &YtDlpEngine::recognize);
    connect(&watcher_, &QFileSystemWatcher::directoryChanged, &settle_, qOverload<>(&QTimer::start));
    connect(&watcher_, &QFileSystemWatcher::fileChanged, &settle_, qOverload<>(&QTimer::start));
    watcher_.addPath(dir_);
}

QString YtDlpEngine::managedPath() const
{
#ifdef Q_OS_WIN
    return dir_ + "/yt-dlp.exe";
#else
    return dir_ + "/yt-dlp";
#endif
}

// Only a copy Queenfisher signature-checked itself: one found on the computer is never run.
QString YtDlpEngine::path() const { return trusted_ && QFile::exists(managedPath()) ? managedPath() : QString(); }

void YtDlpEngine::recognize()
{
    const QString file = managedPath();
    if (QFile::exists(file) && !watcher_.files().contains(file))
        watcher_.addPath(file);   // a copy over it changes the file, not the folder
    const QString hash = QFile::exists(file) ? fileHash(file) : QString();
    const auto known = std::find_if(stamp_.known.cbegin(), stamp_.known.cend(), [&](const auto &k) { return k.first == hash; });
    const bool wasTrusted = std::exchange(trusted_, !hash.isEmpty() && known != stamp_.known.cend());
    if (!trusted_) {
        if (!hash.isEmpty() && wasTrusted)
            emit logLine("The yt-dlp in Queenfisher's folder isn't a copy it checked, so it won't be used");
        if (wasTrusted)
            emit readyChanged();
        return;
    }
    QString version = known->second;
    if (version.isEmpty()) {   // kept from before versions were noted: ask it
        QProcess p;
        p.start(file, {"--version"});
        version = p.waitForFinished(30000) ? QString::fromUtf8(p.readAllStandardOutput()).trimmed() : QString();
        remember(hash, version);
    }
    if (version != stamp_.version && !version.isEmpty()) {
        // Put back by hand: the version it replaced isn't brought back by the next automatic check.
        if (!stamp_.version.isEmpty() && compareVersions(version, stamp_.version) < 0)
            stamp_.skip = stamp_.version;
        emit logLine("Using yt-dlp " + version + ", the copy now in Queenfisher's folder");
        stamp_.version = version;
        stamp_.channel = version.count('.') > 2 ? Nightly : Stable;   // nightlies carry a time too
        saveStamp();
    }
    emit readyChanged();
}

void YtDlpEngine::remember(const QString &hash, const QString &version)
{
    stamp_.known.removeIf([&](const auto &k) { return k.first == hash; });
    stamp_.known.append({hash, version});
    while (stamp_.known.size() > 10)
        stamp_.known.removeFirst();
    saveStamp();
}

QString YtDlpEngine::version() const
{
    if (path().isEmpty())
        return {};
    if (stamp_.version.isEmpty())
        return "own copy";
    return QString("%1 (%2)").arg(stamp_.version, channelName(stamp_.channel));
}

void YtDlpEngine::checkDaily()
{
    const auto quiet = [](bool, const QString &) {};
    if (path().isEmpty() || stamp_.channel == Stable) {
        run(Stable, false, quiet);
        return;
    }
    // On nightly after a rescue: go back to stable once it has caught up.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (busy_ || now - stamp_.checked < dayMs || now - lastTry_ < retryMs)
        return;
    latestTag(Stable, [this, quiet](const QString &tag) {
        const bool caughtUp = !tag.isEmpty() && compareVersions(tag, stamp_.version, 3) >= 0;
        run(caughtUp ? Stable : Nightly, caughtUp, quiet);
    });
}

void YtDlpEngine::updateNow()
{
    run(path().isEmpty() ? Stable : stamp_.channel, true, [](bool, const QString &) { }, Update);
}

void YtDlpEngine::switchChannel(Channel channel)
{
    run(channel, true, [](bool, const QString &) { }, Switch);   // the user's pick may be an older stable
}

// Update the current channel; if that changes nothing, offer nightly, which gets site fixes first.
// Only the user switches: the failure that led here can quote text from the site.
void YtDlpEngine::repair(Done done)
{
    if (path().isEmpty()) {
        run(Stable, true, done);
        return;
    }
    run(stamp_.channel, true, [this, done](bool changed, const QString &message) {
        if (changed || stamp_.channel == Nightly)
            done(changed, message);
        else
            offerNightly(done, message);
    });
}

// Downloads failing together share one question and one nightly install.
void YtDlpEngine::offerNightly(const Done &done, const QString &message)
{
    if (!askNightly || nightlyAnswer_ == 0) {
        done(false, message);
        return;
    }
    if (nightlyAnswer_ == 1) {
        run(Nightly, true, done);
        return;
    }
    nightlyAsked_.append(done);
    if (nightlyAsked_.size() > 1)
        return;   // already asking
    QTimer::singleShot(0, this, [this, message] {   // not from inside a network reply
        const bool yes = askNightly();
        const QList<Done> waiting = std::exchange(nightlyAsked_, {});
        nightlyAnswer_ = yes ? 1 : 0;
        const Done all = [waiting](bool changed, const QString &m) {
            for (const Done &d : waiting)
                d(changed, m);
        };
        if (yes)
            run(Nightly, true, all);
        else
            all(false, message);
    });
}

void YtDlpEngine::run(Channel channel, bool force, const Done &done, Asked asked)
{
    if (busy_) {
        if (force)
            queued_.append({channel, done, asked});   // asked for on purpose: its own turn after this one
        else
            waiting_.append(done);             // automatic checks share the one running
        return;
    }
    const bool installed = !path().isEmpty();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!force && installed && stamp_.channel == channel && now - stamp_.checked < dayMs) {
        done(false, "yt-dlp was checked today");
        return;
    }
    // Without this, a failed check (offline, GitHub down) would start the next one straight away.
    if (!force && now - lastTry_ < retryMs) {
        done(false, "yt-dlp was checked a moment ago");
        return;
    }
    if (assetName().isEmpty()) {
        done(false, "Queenfisher can't fetch yt-dlp for this kind of computer");
        return;
    }
    busy_ = true;
    lastTry_ = now;
    waiting_.append(done);
    emit readyChanged();
    emit statusChanged(installed ? "Checking for a newer yt-dlp..." : "Getting yt-dlp...");

    latestTag(channel, [this, channel, installed, asked](const QString &tag) {
        if (tag.isEmpty())
            return finish(false, "Couldn't reach GitHub to check yt-dlp");
        if (installed && tag == stamp_.version && channel == stamp_.channel) {
            stamp_.checked = QDateTime::currentMSecsSinceEpoch();
            saveStamp();
            return finish(false, "yt-dlp " + tag + " is the newest " + channelName(channel));
        }
        // An older release is still validly signed, so the version and the signature's date say it's stale.
        if (installed && asked != Switch && isDowngrade(tag, stamp_.version))
            return finish(false, "Refused yt-dlp " + tag + ": older than the " + stamp_.version + " in use");
        if (installed && asked == Auto && isRolledBack(tag, stamp_.skip))
            return finish(false, "Staying on yt-dlp " + stamp_.version + ": " + stamp_.skip + " was rolled back");
        install(channel, tag, installed && asked != Switch && channel == stamp_.channel);
    });
}
#pragma endregion

#pragma region Downloading and installing
void YtDlpEngine::latestTag(Channel channel, const std::function<void(const QString &)> &then)
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

void YtDlpEngine::fetch(const QUrl &url, qint64 limit, const std::function<void(QNetworkReply *)> &then)
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

void YtDlpEngine::install(Channel channel, const QString &tag, bool sameChannel)
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
            // Replayed: an older signed release offered again under a newer-looking tag.
            if (sameChannel && isOlderSignature(check.created, stamp_.signedAt))
                return finish(false, "Refused yt-dlp " + tag + ": signed before the copy in use");
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
                smokeTest(fresh, tag, check.created);
            });
        });
    });
}

YtDlpEngine::~YtDlpEngine()
{
    for (QProcess *p : findChildren<QProcess *>())   // a version check still running when Queenfisher quits
        ChildProcess::stopAndWait(*p);
}

// The new copy has to start and report the expected version before it replaces the old one.
void YtDlpEngine::smokeTest(const QString &file, const QString &tag, qint64 signedAt)
{
    auto *p = new QProcess(this);
    ChildProcess::ownGroup(*p);
    QTimer::singleShot(60000, p, [p] { ChildProcess::stop(*p, true); });
    const auto giveUp = [this, p, file](const QString &why) {
        p->deleteLater();
        QFile::remove(file);
        loadStamp();   // back to the channel of the copy still in use
        finish(false, why);
    };
    // A file that can't run at all (removed by antivirus, no exec rights) never sends finished.
    connect(p, &QProcess::errorOccurred, this, [giveUp, tag](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            giveUp("The new yt-dlp " + tag + " couldn't start, keeping the old one");
    });
    connect(p, &QProcess::finished, this, [=, this](int code, QProcess::ExitStatus status) {
        p->deleteLater();
        const QString said = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
        if (status != QProcess::NormalExit || code != 0 || said != tag)
            return giveUp("The new yt-dlp " + tag + " didn't start properly, keeping the old one");
        const QString current = managedPath(), previous = managedPath() + ".previous";
        const QString hash = fileHash(file);
        if (hash.isEmpty())
            return giveUp("Couldn't read the new yt-dlp " + tag + ", keeping the old one");
        // The copy in use becomes the one to roll back to, unless it's not one Queenfisher checked.
        if (trusted_)
            QFile::remove(previous);
        if (QFile::exists(current) && !(trusted_ ? QFile::rename(current, previous) : QFile::remove(current)))
            return giveUp("Couldn't replace yt-dlp (is it running?)");
        if (!QFile::rename(file, current)) {
            QFile::rename(previous, current);
            return giveUp("Couldn't put the new yt-dlp in place");
        }
        remember(hash, tag);
        trusted_ = true;
        if (compareVersions(tag, stamp_.skip) >= 0)
            stamp_.skip.clear();   // at or past the version rolled back from
        stamp_.version = tag;
        stamp_.signedAt = signedAt;
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
    while (!busy_ && !queued_.isEmpty()) {   // a waiting callback may have started a run already
        const Request next = queued_.takeFirst();
        run(next.channel, true, next.done, next.asked);
    }
}
#pragma endregion

#pragma region Helpers and the stamp file
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

// Dates only: a stable release replaces a nightly from the same day.
bool YtDlpEngine::isDowngrade(const QString &tag, const QString &installed)
{
    return !installed.isEmpty() && compareVersions(tag, installed, 3) < 0;
}

bool YtDlpEngine::isOlderSignature(qint64 created, qint64 inUse) { return inUse > 0 && created < inUse; }

bool YtDlpEngine::isRolledBack(const QString &tag, const QString &skip) { return !skip.isEmpty() && compareVersions(tag, skip) <= 0; }

QString YtDlpEngine::fileHash(const QString &file)
{
    QFile f(file);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return f.open(QIODevice::ReadOnly) && hash.addData(&f) ? QString::fromLatin1(hash.result().toHex()) : QString();
}

void YtDlpEngine::loadStamp()
{
    QFile f(managedPath() + ".json");
    const QJsonObject o = f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
    stamp_.version = o.value("version").toString();
    stamp_.channel = o.value("channel").toString() == "nightly" ? Nightly : Stable;
    stamp_.checked = qint64(o.value("checked").toDouble());
    stamp_.skip = o.value("skip").toString();
    stamp_.signedAt = qint64(o.value("signed").toDouble());
    stamp_.known.clear();
    static const QRegularExpression sha(R"(^[0-9a-f]{64}$)");
    for (const auto &v : o.value("known").toArray()) {
        const QJsonObject k = v.toObject();
        if (sha.match(k.value("sha256").toString()).hasMatch())
            stamp_.known.append({k.value("sha256").toString(), k.value("version").toString()});
    }
    if (!o.contains("known") && !stamp_.version.isEmpty()) {
        // A stamp from before copies were noted: both files there were put in place by Queenfisher.
        if (const QString h = fileHash(managedPath()); !h.isEmpty())
            stamp_.known.append({h, stamp_.version});
        if (const QString h = fileHash(managedPath() + ".previous"); !h.isEmpty())
            stamp_.known.prepend({h, QString()});
        if (!stamp_.known.isEmpty())
            saveStamp();
    }
}

void YtDlpEngine::saveStamp() const
{
    QJsonArray known;
    for (const auto &[hash, version] : stamp_.known)
        known.append(QJsonObject{{"sha256", hash}, {"version", version}});
    QFile f(managedPath() + ".json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(
            QJsonObject{{"version", stamp_.version}, {"channel", channelName(stamp_.channel)}, {"checked", double(stamp_.checked)},
                {"known", known}, {"skip", stamp_.skip}, {"signed", double(stamp_.signedAt)}})
                .toJson());
}
#pragma endregion
