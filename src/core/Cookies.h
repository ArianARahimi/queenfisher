#pragma once

#include <QByteArray>
#include <QString>

// Value for yt-dlp's --cookies-from-browser that matches the user's default browser,
// or empty when it can't be worked out. Firefox forks (Waterfox, LibreWolf, Floorp) aren't
// browsers yt-dlp knows by name, so they get "firefox:<profile folder>".
QString defaultBrowserCookies();

// The parts that don't depend on this computer, public for tests.
// browserId: what the system calls the default browser (Linux desktop file, Windows ProgId,
// macOS bundle id), matched without regard to case.
QString cookiesFor(const QString &browserId);
// macOS: the https handler's bundle id from LaunchServices' settings (as `plutil -convert
// json` prints them); "com.apple.safari" when nothing else was ever chosen.
QString launchServicesHttpsHandler(const QByteArray &json);

// Picks the profile from a Firefox-style profiles.ini: the one whose cookies were written
// most recently, so leftovers of an old install lose to the profile in use.
QString newestFirefoxProfile(const QString &profilesDir);
