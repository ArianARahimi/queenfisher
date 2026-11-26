// Queenfisher - free media downloader. GPL-3.0-or-later.
#include "core/PrivateFolder.h"
#include "core/SingleInstance.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QFileInfo>
#include <QNetworkProxyFactory>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace {

// Apps opened from the Dock or Finder get only /usr/bin:/bin:/usr/sbin:/sbin, so ffmpeg from
// Homebrew (/opt/homebrew/bin, /usr/local/bin on Intel) or MacPorts would never be found, by
// Queenfisher or by yt-dlp. They're added once, here, for both.
void addPackageManagerPaths()
{
#ifdef Q_OS_MACOS
    QStringList path = qEnvironmentVariable("PATH").split(':', Qt::SkipEmptyParts);
    for (const char *dir : {"/opt/homebrew/bin", "/usr/local/bin", "/opt/local/bin"})
        if (!path.contains(dir))
            path << dir;
    qputenv("PATH", path.join(':').toUtf8());
#endif
}

// History, partial downloads and settings (sign-in sites) for this user's eyes only.
void keepFoldersPrivate()
{
    makePrivateFolder(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation));
    makePrivateFolder(QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)   // macOS keeps settings in its own private Preferences
    makePrivateFolder(QFileInfo(QSettings().fileName()).absolutePath());
#endif
}
}

int main(int argc, char *argv[])
{
    addPackageManagerPaths();
    const LaunchTokens tokens = LaunchTokens::fromEnvironment();
    QApplication app(argc, argv);
    QApplication::setApplicationName("Queenfisher");
    QApplication::setOrganizationName("Queenfisher");
    QApplication::setApplicationVersion(QUEENFISHER_VERSION);
    keepFoldersPrivate();

    SingleInstance instance(SingleInstance::defaultName(), tokens);
    if (instance.alreadyRunning())
        return 0;   // the open Queenfisher shows its window instead

    QNetworkProxyFactory::setUseSystemConfiguration(true);   // honour the system or environment proxy
    Theme::apply(app);

    MainWindow window;
    QObject::connect(&instance, &SingleInstance::showRequested, &window, &MainWindow::bringBack);
    window.show();
    return QApplication::exec();
}
