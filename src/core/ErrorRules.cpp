#include "ErrorRules.h"

#include <QRegularExpression>

namespace {

struct Rule
{
    const char *pattern;   // case-insensitive regular expression
    Problem problem;
    const char *message;
};

// First match wins, so specific rules go above general ones.
const Rule rules[] = {
    {R"(sign in to confirm|sign in if you.ve been granted|use --cookies|cookies.*authentication|login required|sign in to view)",
     Problem::SignIn, "The site wants a sign-in"},
    {R"(http error 429|too many requests)",
     Problem::RateLimited, "The site is limiting this connection, try again later"},
    {R"(data center ip|vpn/proxy|your ip.*(blocked|banned)|blocked.*your (ip|country|region))",
     Problem::Blocked, "The site is blocking this connection, a VPN can cause this"},
    {R"(no supported javascript runtime|javascript runtime.*(not found|required)|install.*deno)",
     Problem::NoJsRuntime, "YouTube needs Deno installed to unlock videos"},
    {R"(ffmpeg.*not (found|installed)|ffprobe.*not found|you have requested merging)",
     Problem::NoFfmpeg, "ffmpeg is missing (needed to join video and sound)"},
    {R"(private video|video unavailable|has been removed|no longer available|members.only|)"
     R"(not available in your country|geo.?restrict|this live event will begin|premieres in)",
     Problem::Unavailable, "The video is private, removed or not available here"},
    {R"(unsupported url)",
     Problem::Unsupported, "This site isn't supported"},
    // Signs that the site changed and yt-dlp needs a newer version.
    {R"(confirm you are on the latest version|yt-dlp -u|please report this issue|nsig extraction failed|)"
     R"(signature extraction failed|unable to extract|failed to extract|n challenge|requested format is not available|)"
     R"(http error 403|unable to decode|player response|po token|sabr)",
     Problem::Outdated, "The site changed something or refused the download"},
    {R"(unable to download webpage|timed out|connection (refused|reset|aborted)|name or service not known|)"
     R"(temporary failure in name resolution|getaddrinfo failed|network is unreachable|ssl)",
     Problem::Network, "Network problem, check your connection"},
};

Diagnosis match(const QString &line)
{
    static const QList<QRegularExpression> compiled = [] {
        QList<QRegularExpression> list;
        for (const Rule &r : rules)
            list.append(QRegularExpression(QString::fromLatin1(r.pattern), QRegularExpression::CaseInsensitiveOption));
        return list;
    }();
    for (qsizetype i = 0; i < compiled.size(); ++i)
        if (compiled[i].match(line).hasMatch())
            return {rules[i].problem, QString::fromLatin1(rules[i].message)};
    return {};
}

}

Diagnosis diagnose(const QStringList &lines)
{
    QString lastError;
    for (const QString &line : lines)
        if (line.startsWith("ERROR:"))
            lastError = line;
    if (!lastError.isEmpty()) {
        const Diagnosis d = match(lastError);
        if (d.problem != Problem::None)
            return d;
    }
    // No error matched: a warning may still say why (missing Deno shows up as a warning first).
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        const Diagnosis d = match(*it);
        if (d.problem != Problem::None && d.problem != Problem::Network)
            return d;
    }
    if (lastError.isEmpty())
        return {Problem::Unknown, "Failed, see the log"};
    return {Problem::Unknown, "Failed: " + lastError.mid(6).trimmed().left(80)};
}
