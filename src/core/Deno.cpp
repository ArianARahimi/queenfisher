#include "Deno.h"

#include <QHash>
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

// The pinned release's SHA-256 per build, checked against GitHub's own record of each file.
QString Deno::pinnedSha256(const QString &asset)
{
    static const QHash<QString, QString> pins = {
        {"deno-x86_64-pc-windows-msvc.zip", "cba84b26e0df5ae7f74fc66b86a95b866872a86292ebd84fa1e959927edd93bf"},
        {"deno-x86_64-unknown-linux-gnu.zip", "e890975bc2549807bbcc0b584ffafda72cc7c93349dd99c80483dd784f6c4609"},
        {"deno-aarch64-unknown-linux-gnu.zip", "d49596c20b9788cf8f5d8ddf644103d4bde56ed239753f0bdb92ba9361d51905"},
        {"deno-x86_64-apple-darwin.zip", "4b2f5edd2639b7470f034238b321d7002d8dfb4726d8558f52c17afac370eb2c"},
        {"deno-aarch64-apple-darwin.zip", "9f0bf660393da2eaff044cdc7444cb08c73f235849b61d224d825a194ce7c98e"},
    };
    return pins.value(asset);
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
    const QString asset = assetFor(os, QSysInfo::currentCpuArchitecture());
    s->sha256 = pinnedSha256(asset);
    if (s->sha256.isEmpty())
        return false;
    s->zip = QUrl("https://github.com/denoland/deno/releases/download/" + QString(version) + "/" + asset);
    s->limit = 300LL << 20;   // the zip is about 45 MB
    return true;
}
