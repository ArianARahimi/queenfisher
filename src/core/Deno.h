#pragma once

#include "FetchedTool.h"

// Deno, which yt-dlp needs to unlock most YouTube videos (without it, YouTube offers little
// beyond 480p). One installed on the computer is used as it is; otherwise Queenfisher fetches
// the official build of the pinned version for this computer.
class Deno : public FetchedTool
{
    Q_OBJECT

public:
    static constexpr const char *version = "v2.9.8";   // with pinnedSha256(), updated per Queenfisher release

    explicit Deno(const QString &dataDir, QObject *parent = nullptr);
    QString program() const;   // Queenfisher's own deno, for yt-dlp's --js-runtimes; empty = none

    static QString assetFor(const QString &os, const QString &cpu);   // "deno-<target>.zip", empty if none
    static QString pinnedSha256(const QString &asset);                // empty: no pinned build

protected:
    bool wanted() const override;
    bool source(Source *s) const override;
    bool keep(const QFileInfo &f) const override { return f.fileName() == exe("deno"); }
    QStringList programs() const override { return {exe("deno")}; }
    QString versionFlag() const override { return "--version"; }
};
