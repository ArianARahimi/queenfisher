#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTemporaryFile>

// Value for yt-dlp's --cookies-from-browser that matches the user's default browser,
// or empty when it can't be worked out. Firefox forks (Waterfox, LibreWolf, Floorp) aren't
// browsers yt-dlp knows by name, so they get "firefox:<profile folder>".
QString defaultBrowserCookies();

// The parts that don't depend on this computer, public for tests.
// browserId: what the system calls the default browser (Linux desktop file, Windows ProgId,
// macOS bundle id), matched without regard to case.
QString cookiesFor(const QString &browserId);
// The browser a defaultBrowserCookies() value reads, for messages: "Edge", "Waterfox",
// or "your browser" when it's none we know.
QString browserLabel(const QString &cookiesValue);
// macOS: the https handler's bundle id from LaunchServices' settings (as `plutil -convert
// json` prints them); "com.apple.safari" when nothing else was ever chosen.
QString launchServicesHttpsHandler(const QByteArray &json);

// Picks the profile from a Firefox-style profiles.ini: the one whose cookies were written
// most recently, so leftovers of an old install lose to the profile in use.
QString newestFirefoxProfile(const QString &profilesDir);

// The site a link belongs to, as sign-in is allowed for it: the host without "www.", short-link
// hosts as their full site (youtu.be is youtube.com).
QString siteOf(const QString &url);

// Lines of a cookies.txt (Netscape format) for this site, its subdomains and the domains above
// it (youtube.com's for music.youtube.com); nothing for other sites.
QByteArray cookiesForSite(const QByteArray &cookiesTxt, const QString &site);

// Copies the default browser's cookies for one site into a file of its own, readable by this
// user only, so yt-dlp never holds the sign-ins of other sites. The browser's full set lives
// only until it's filtered; the site's file is deleted with this object.
class CookieExport : public QObject
{
    Q_OBJECT

public:
    // ytDlp: the program that reads the browser; browser: a defaultBrowserCookies() value.
    CookieExport(QString ytDlp, QString browser, QString site, QObject *parent = nullptr);
    ~CookieExport() override;
    void start();
    QString file() const { return done_ ? mine_.fileName() : QString(); }   // empty until it worked

signals:
    void finished(bool ok, const QString &why);

private:
    void filter();
    void fail(const QString &why);

    QString ytDlp_, browser_, site_;
    QProcess process_;
    QTemporaryFile all_, mine_;
    bool done_ = false;
};
