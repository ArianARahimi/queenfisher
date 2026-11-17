#include "Cookies.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>

namespace {

struct Browser { const char *match; const char *ytDlpName; const char *profilesDir; };

// profilesDir is relative to home; empty for browsers yt-dlp finds by itself.
const Browser browsers[] = {
    {"waterfox", "firefox", ".waterfox"},
    {"librewolf", "firefox", ".librewolf"},
    {"floorp", "firefox", ".floorp"},
    {"firefox", "firefox", ".mozilla/firefox"},
    {"brave", "brave", ""},
    {"vivaldi", "vivaldi", ""},
    {"opera", "opera", ""},
    {"edge", "edge", ""},
    {"chromium", "chromium", ""},
    {"chrome", "chrome", ""},
};

QString defaultBrowserId()
{
#ifdef Q_OS_LINUX
    QProcess p;
    p.start("xdg-mime", {"query", "default", "x-scheme-handler/https"});
    if (p.waitForFinished(3000))
        return QString::fromUtf8(p.readAllStandardOutput()).trimmed().toLower();
#endif
    return {};   // Windows and macOS: not built yet
}

}

QString newestFirefoxProfile(const QString &profilesDir)
{
    QSettings ini(profilesDir + "/profiles.ini", QSettings::IniFormat);
    QString best;
    QDateTime newest;
    for (const QString &group : ini.childGroups()) {
        if (!group.startsWith("Profile"))
            continue;
        ini.beginGroup(group);
        const QString path = ini.value("Path").toString();
        const bool relative = ini.value("IsRelative", 1).toInt() == 1;
        ini.endGroup();
        if (path.isEmpty())
            continue;
        const QString dir = relative ? profilesDir + "/" + path : path;
        const QFileInfo cookies(dir + "/cookies.sqlite");
        if (cookies.exists() && (best.isEmpty() || cookies.lastModified() > newest)) {
            best = QDir::cleanPath(dir);
            newest = cookies.lastModified();
        }
    }
    return best;
}

QString defaultBrowserCookies()
{
    const QString id = defaultBrowserId();
    if (id.isEmpty())
        return {};
    for (const Browser &b : browsers) {
        if (!id.contains(b.match))
            continue;
        if (!*b.profilesDir)
            return b.ytDlpName;
        const QString profile = newestFirefoxProfile(QDir::homePath() + "/" + b.profilesDir);
        return profile.isEmpty() ? QString() : QString("%1:%2").arg(b.ytDlpName, profile);
    }
    return {};
}
