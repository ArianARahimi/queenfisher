#include "Tools.h"

#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>

QList<MissingTool> missingTools(bool ownFfmpeg)
{
    QList<MissingTool> missing;
    const auto found = [](const char *program) { return !QStandardPaths::findExecutable(program).isEmpty(); };
    if (!ownFfmpeg && !(found("ffmpeg") && found("ffprobe")))
        missing.append(MissingTool{"ffmpeg", "ffmpeg isn't installed.\nQueenfisher needs it to join picture and sound and to make MP3s."});
    return missing;
}

QString oldFfmpegNote(const QByteArray &versionOutput)
{
    static const QVersionNumber oldestFixed(5, 1);   // ffmpeg's oldest line still getting fixes
    // "ffmpeg version 6.1.1-3ubuntu5" or "n7.1"; git builds ("N-118000-g...") stay unknown.
    static const QRegularExpression version(R"(^ffmpeg version n?(\d+\.\d+(?:\.\d+)?))");
    const QRegularExpressionMatch m = version.match(QString::fromUtf8(versionOutput.left(200)));
    if (!m.hasMatch())
        return {};
    const QVersionNumber v = QVersionNumber::fromString(m.captured(1));
    if (v >= oldestFixed)
        return {};
    return "ffmpeg " + m.captured(1) + " is older than " + oldestFixed.toString()
           + ": newer ones fix bugs in reading media files, updating it is safer";
}
