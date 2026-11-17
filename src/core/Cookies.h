#pragma once

#include <QString>

// Value for yt-dlp's --cookies-from-browser that matches the user's default browser,
// or empty when it can't be worked out. Firefox forks (Waterfox, LibreWolf, Floorp) aren't
// browsers yt-dlp knows by name, so they get "firefox:<profile folder>".
QString defaultBrowserCookies();

// Picks the profile from a Firefox-style profiles.ini: the one whose cookies were written
// most recently, so leftovers of an old install lose to the profile in use.
QString newestFirefoxProfile(const QString &profilesDir);
