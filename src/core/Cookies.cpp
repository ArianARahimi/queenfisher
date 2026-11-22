#include "Cookies.h"
#include "ChildProcess.h"
#include "ErrorRules.h"
#include "PrivateFolder.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>   // before shlwapi.h, which needs its types

#include <shlwapi.h>
#endif

#pragma region Default browser
namespace {

// Where each Firefox-style browser keeps profiles.ini: Linux (from home), Windows (from
// %APPDATA%), macOS (from ~/Library/Application Support). Empty for browsers yt-dlp finds
// by itself.
struct Browser
{
    const char *match;
    const char *label;
    const char *ytDlpName;
    const char *onLinux, *onWindows, *onMac;
};
const Browser browsers[] = {
    {"waterfox", "Waterfox", "firefox", ".waterfox", "Waterfox", "Waterfox"},
    {"librewolf", "LibreWolf", "firefox", ".librewolf", "librewolf", "librewolf"},
    {"floorp", "Floorp", "firefox", ".floorp", "Floorp", "Floorp"},
    {"firefox", "Firefox", "firefox", ".mozilla/firefox", "Mozilla/Firefox", "Firefox"},
    {"brave", "Brave", "brave", "", "", ""},
    {"vivaldi", "Vivaldi", "vivaldi", "", "", ""},
    {"opera", "Opera", "opera", "", "", ""},
    {"edge", "Edge", "edge", "", "", ""},         // Windows MSEdgeHTM, macOS com.microsoft.edgemac
    {"chromium", "Chromium", "chromium", "", "", ""},
    {"chrome", "Chrome", "chrome", "", "", ""},      // Windows ChromeHTML, macOS com.google.chrome
    {"safari", "Safari", "safari", "", "", ""},
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
    // Newer Windows 11 keeps the choice in UserChoiceLatest and leaves UserChoice stale, so ask the shell.
    wchar_t progId[256];
    DWORD size = sizeof progId / sizeof *progId;
    if (AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_PROGID, L"https", L"open", progId, &size) == S_OK)
        return QString::fromWCharArray(progId);
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

QString browserLabel(const QString &cookiesValue)
{
    const QString value = cookiesValue.toLower();
    for (const Browser &b : browsers)
        if (value.contains(b.match))   // "firefox:<path>" names a fork by its folder
            return b.label;
    return "your browser";
}

QString defaultBrowserCookies()
{
    return cookiesFor(defaultBrowserId());
}
#pragma endregion

#pragma region Cookies for one site
QString siteOf(const QString &url)
{
    // Short-link hosts whose sign-in lives on the full site.
    static const QHash<QString, QString> shortHosts = {
        {"youtu.be", "youtube.com"}, {"redd.it", "reddit.com"}, {"fb.watch", "facebook.com"}};
    QString host = QUrl(url.trimmed()).host().toLower();
    if (host.startsWith("www."))
        host = host.mid(4);
    return shortHosts.value(host, host);
}

QByteArray cookiesForSite(const QByteArray &cookiesTxt, const QString &site)
{
    const QString wanted = site.toLower();
    QByteArray out = "# Netscape HTTP Cookie File\n";
    for (QByteArray line : cookiesTxt.split('\n')) {
        line.replace('\r', "");
        const bool httpOnly = line.startsWith("#HttpOnly_");   // a cookie line, not a comment
        const qsizetype tab = line.indexOf('\t');
        if (tab < 0 || (line.startsWith('#') && !httpOnly))
            continue;
        QString domain = QString::fromUtf8(line.left(tab)).toLower().mid(httpOnly ? 10 : 0);
        if (domain.startsWith('.'))
            domain = domain.mid(1);
        if (domain.contains('.') && (domain == wanted || domain.endsWith("." + wanted) || wanted.endsWith("." + domain)))
            out += line + "\n";
    }
    return out;
}

CookieExport::CookieExport(QString ytDlp, QString browser, QString site, QObject *parent)
    : QObject(parent), ytDlp_(std::move(ytDlp)), browser_(std::move(browser)), site_(std::move(site))
{
    ChildProcess::ownGroup(process_);
    connect(&process_, &QProcess::finished, this, &CookieExport::filter);   // exits 2: no link given
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            fail("Couldn't start yt-dlp to read your browser's sign-in");
    });
}

CookieExport::~CookieExport()
{
    disconnect(&process_, nullptr, this, nullptr);
    ChildProcess::stopAndWait(process_);
}

// yt-dlp reads the browser and saves what it read with --cookies, even without a link.
void CookieExport::start()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/cookies";
    all_.setFileTemplate(dir + "/all-XXXXXX.txt");
    mine_.setFileTemplate(dir + "/site-XXXXXX.txt");
    // yt-dlp refuses an existing file without the Netscape header and then saves nothing.
    if (!makePrivateFolder(dir) || !all_.open() || all_.write("# Netscape HTTP Cookie File\n") < 0)
        return fail("Couldn't make a private place for the sign-in");
    all_.close();
    process_.start(ytDlp_, {"--ignore-config", "--no-plugin-dirs", "--cookies-from-browser", browser_, "--cookies", all_.fileName()});
    QTimer::singleShot(60000, this, [this] { ChildProcess::stop(process_, true); });
}

void CookieExport::filter()
{
    const QStringList problems = problemLines(process_.readAllStandardError());
    const Diagnosis d = diagnose(problems);
    const bool errored = std::any_of(problems.cbegin(), problems.cend(), [](const QString &l) { return l.startsWith("ERROR:"); });
    QByteArray all;
    if (all_.open()) {
        all = all_.readAll();
        all_.close();
    }
    all_.remove();   // every site's sign-ins: gone as soon as this site's are out
    const QByteArray mine = cookiesForSite(all, site_);
    if (mine.count('\n') < 2) {
        const QString browser = browserLabel(browser_);
        if (d.problem == Problem::BrowserCookies)
            return fail(QString("Couldn't read %1's sign-in. Close %1 and retry").arg(browser));
        if (errored)
            return fail(QString("Couldn't read %1's sign-in: %2").arg(browser, d.message));
        return fail("Your browser has no sign-in for " + site_);
    }
    if (!mine_.open() || mine_.write(mine) != mine.size())
        return fail("Couldn't save the sign-in for " + site_);
    mine_.close();
    done_ = true;
    emit finished(true, {});
}

void CookieExport::fail(const QString &why)
{
    all_.remove();
    emit finished(false, why);
}
#pragma endregion
