#include "Deno.h"

#include "YtDlpEngine.h"

#include <QRegularExpression>
#include <QStandardPaths>
#include <QSysInfo>

Deno::Deno(const QString &dataDir, QObject *parent) : FetchedTool(dataDir + "/deno", "Deno", "about 45 MB", parent) {}

QString Deno::program() const
{
    return location().isEmpty() ? QString() : location() + "/" + exe("deno");
}

// Deno's own build names, one per computer kind.
QString Deno::assetFor(const QString &os, const QString &cpu)
{
    const QString arch = cpu == "x86_64" ? "x86_64" : cpu == "arm64" ? "aarch64" : QString();
    const QString target = os == "windows" ? (arch == "x86_64" ? "x86_64-pc-windows-msvc" : QString())
                         : os == "macos"   ? arch + "-apple-darwin"
                         : os == "linux"   ? arch + "-unknown-linux-gnu"
                                           : QString();
    return arch.isEmpty() || target.isEmpty() ? QString() : "deno-" + target + ".zip";
}

// Each Deno zip has its own checksum file: "hash  name" for Linux and macOS, PowerShell's
// "Hash : HASH" (upper case) for Windows. Either way it holds the one hash.
QString Deno::checksumIn(const QByteArray &sums, const QString &asset)
{
    if (const QString line = YtDlpEngine::checksumFor(sums, asset); !line.isEmpty())
        return line;
    static const QRegularExpression hash(R"((?<![0-9A-Fa-f])([0-9A-Fa-f]{64})(?![0-9A-Fa-f]))");
    QStringList found;
    for (auto it = hash.globalMatch(QString::fromLatin1(sums)); it.hasNext();)
        found << it.next().captured(1).toLower();
    return found.size() == 1 ? found.first() : QString();   // more than one: not a single-file list
}

QString Deno::checksum(const QByteArray &sums, const QString &asset) const
{
    return checksumIn(sums, asset);
}

bool Deno::wanted() const
{
    return QStandardPaths::findExecutable("deno").isEmpty();   // yt-dlp finds an installed one by itself
}

bool Deno::source(Source *s) const
{
#ifdef Q_OS_WIN
    const QString os = "windows";
#elif defined(Q_OS_MACOS)
    const QString os = "macos";
#else
    const QString os = "linux";
#endif
    s->asset = assetFor(os, QSysInfo::currentCpuArchitecture());
    if (s->asset.isEmpty())
        return false;
    const QString latest = "https://github.com/denoland/deno/releases/latest/download/";
    s->sums = QUrl(latest + s->asset + ".sha256sum");
    s->zip = QUrl(latest + s->asset);
    s->limit = 300LL << 20;   // the zip is about 45 MB
    return true;
}
