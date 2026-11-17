#pragma once

#include <QList>
#include <QString>

// Programs yt-dlp leans on that Queenfisher doesn't bring: ffmpeg (joins picture and sound,
// makes MP3s) and Deno (YouTube). Each missing one comes with a short message for a popup.
struct MissingTool
{
    QString name;
    QString message;
};

// ownFfmpeg: Queenfisher has (or will fetch) its own ffmpeg, as on Windows.
QList<MissingTool> missingTools(bool ownFfmpeg);
