#pragma once

#include "FetchedTool.h"

// Deno, which yt-dlp needs to unlock most YouTube videos (without it, YouTube offers little
// beyond 480p). One installed on the computer is used as it is; otherwise Queenfisher fetches
// the official build for this computer and keeps it up to date.
class Deno : public FetchedTool
{
    Q_OBJECT

public:
    explicit Deno(const QString &dataDir, QObject *parent = nullptr);
    QString program() const;   // Queenfisher's own deno, for yt-dlp's --js-runtimes; empty = none

    static QString assetFor(const QString &os, const QString &cpu);   // "deno-<target>.zip", empty if none
    static QString checksumIn(const QByteArray &sums, const QString &asset);   // public for tests

protected:
    bool wanted() const override;
    bool source(Source *s) const override;
    bool keep(const QFileInfo &f) const override { return f.fileName() == exe("deno"); }
    QStringList programs() const override { return {exe("deno")}; }
    QString versionFlag() const override { return "--version"; }
    QString checksum(const QByteArray &sums, const QString &asset) const override;
};
