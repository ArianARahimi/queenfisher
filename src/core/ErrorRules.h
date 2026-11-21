#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// What went wrong with a download, worked out from yt-dlp's ERROR and WARNING lines.
// When a site changes how it breaks, the table in ErrorRules.cpp is the only place to edit.
enum class Problem {
    None,
    SignIn,        // the site wants an account: retry with the browser's sign-in
    BrowserCookies,   // the browser's cookies couldn't be read (locked while it runs, or encrypted)
    Outdated,      // the site changed and yt-dlp can't keep up: update yt-dlp, then try nightly
    RateLimited,   // too many requests from this connection
    Blocked,       // the site refuses this connection (often a VPN or data-centre address)
    Network,
    Unavailable,   // private, removed, region-locked, not started yet
    Unsupported,
    NoFfmpeg,
    NoJsRuntime,   // YouTube needs a JavaScript runtime (Deno) to unlock videos
    Unknown,
};

struct Diagnosis
{
    Problem problem = Problem::None;
    QString message;   // plain words for the Status column
};

// lines: yt-dlp's ERROR: and WARNING: lines, oldest first. The last error decides;
// warnings only count when no error matched.
Diagnosis diagnose(const QStringList &lines);

// The ERROR: and WARNING: lines of yt-dlp's output, oldest first.
QStringList problemLines(const QByteArray &output);
