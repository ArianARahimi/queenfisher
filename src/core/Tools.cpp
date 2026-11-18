#include "Tools.h"

#include <QStandardPaths>

QList<MissingTool> missingTools(bool ownFfmpeg)
{
    QList<MissingTool> missing;
    const auto found = [](const char *program) { return !QStandardPaths::findExecutable(program).isEmpty(); };
    if (!ownFfmpeg && !(found("ffmpeg") && found("ffprobe")))
        missing.append(MissingTool{"ffmpeg", "ffmpeg isn't installed.\nQueenfisher needs it to join picture and sound and to make MP3s."});
    return missing;
}
