#include "Cookies.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>

namespace {

// Where each Firefox-style browser keeps profiles.ini: Linux (from home), Windows (from
// %APPDATA%), macOS (from ~/Library/Application Support). Empty for browsers yt-dlp finds
// by itself.
struct Browser { const char *match; const char *ytDlpName; const char *onLinux, *onWindows, *onMac; };
const Browser browsers[] = {
    {"waterfox", "firefox", ".waterfox", "Waterfox", "Waterfox"},
    {"librewolf", "firefox", ".librewolf", "librewolf", "librewolf"},
    {"floorp", "firefox", ".floorp", "Floorp", "Floorp"},
    {"firefox", "firefox", ".mozilla/firefox", "Mozilla/Firefox", "Firefox"},
    {"brave", "brave", "", "", ""},
    {"vivaldi", "vivaldi", "", "", ""},
    {"opera", "opera", "", "", ""},
    {"edge", "edge", "", "", ""},         // Windows MSEdgeHTM, macOS com.microsoft.edgemac
    {"chromium", "chromium", "", "", ""},
    {"chrome", "chrome", "", "", ""},      // Windows ChromeHTML, macOS com.google.chrome
    {"safari", "safari", "", "", ""},
};

QString profilesDir(const Browser &b)
{
#ifdef Q_OS_WIN
    return *b.onWindows ? qEnvironmentVariable("APPDATA") + "/" + b.onWindows : QString();
#elif defined(Q_OS_MACOS)
    return *b.onMac ? QDir::homePath() + "/Library/Application Support/" + b.onMac : QString();
#else
    return *b.onLinux ? QDir::homePath() + "/" + b.onLinux : QString();
#endif
}

QString defaultBrowserId()
{
#ifdef Q_OS_WIN
    const QSettings choice(R"(HKEY_CURRENT_USER\Software\Microsoft\Windows\Shell\Associations\UrlAssociations\https\UserChoice)",
                           QSettings::NativeFormat);
    return choice.value("ProgId").toString();
#elif defined(Q_OS_MACOS)
    QProcess p;
    p.start("/usr/bin/plutil", {"-convert", "json", "-o", "-",
        QDir::homePath() + "/Library/Preferences/com.apple.LaunchServices/com.apple.launchservices.secure.plist"});
    return launchServicesHttpsHandler(p.waitForFinished(3000) && p.exitCode() == 0 ? p.readAllStandardOutput() : QByteArray());
#else
    QProcess p;
    p.start("xdg-mime", {"query", "default", "x-scheme-handler/https"});
    return p.waitForFinished(3000) ? QString::fromUtf8(p.readAllStandardOutput()).trimmed() : QString();
#endif
}

}

QString launchServicesHttpsHandler(const QByteArray &json)
{
    const QJsonArray handlers = QJsonDocument::fromJson(json).object().value("LSHandlers").toArray();
    for (const auto &h : handlers) {
        const QJsonObject o = h.toObject();
        if (o.value("LSHandlerURLScheme").toString() == "https" && !o.value("LSHandlerRoleAll").toString().isEmpty())
            return o.value("LSHandlerRoleAll").toString();
    }
    return "com.apple.safari";   // never changed: Safari is the default
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

QString cookiesFor(const QString &browserId)
{
    const QString id = browserId.toLower();
    if (id.isEmpty())
        return {};
    for (const Browser &b : browsers) {
        if (!id.contains(b.match))
            continue;
        const QString dir = profilesDir(b);
        if (dir.isEmpty())
            return b.ytDlpName;
        const QString profile = newestFirefoxProfile(dir);
        return profile.isEmpty() ? QString() : QString("%1:%2").arg(b.ytDlpName, profile);
    }
    return {};
}

QString defaultBrowserCookies()
{
    return cookiesFor(defaultBrowserId());
}
