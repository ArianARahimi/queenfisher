#pragma once

#include "FetchedTool.h"

// ffmpeg, which yt-dlp needs to join picture and sound and to convert. Linux and macOS use
// the one installed on the computer. Windows rarely has one, so Queenfisher fetches yt-dlp's
// own build (checked against the release's SHA-256 checksums) and keeps it up to date.
class Ffmpeg : public FetchedTool
{
    Q_OBJECT

public:
    explicit Ffmpeg(const QString &dataDir, QObject *parent = nullptr);

protected:
    bool wanted() const override;
    bool source(Source *s) const override;
    bool keep(const QFileInfo &f) const override;
    QStringList programs() const override { return {exe("ffmpeg"), exe("ffprobe")}; }
    QString versionFlag() const override { return "-version"; }
};
