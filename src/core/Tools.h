#pragma once

#include <QList>
#include <QString>

// Programs yt-dlp leans on that Queenfisher doesn't bring: ffmpeg on Linux and macOS (joins
// picture and sound, converts). Deno, and ffmpeg on Windows, Queenfisher fetches itself.
// Each missing one comes with a short message for a popup.
struct MissingTool
{
    QString name;
    QString message;
};

// ownFfmpeg: Queenfisher has (or will fetch) its own ffmpeg, as on Windows.
QList<MissingTool> missingTools(bool ownFfmpeg);
