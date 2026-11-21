#include "PrivateFolder.h"

#include <QDir>
#include <QFile>

bool makePrivateFolder(const QString &dir)
{
    if (!QDir().mkpath(dir))
        return false;
#ifdef Q_OS_WIN
    return true;
#else
    return QFile::setPermissions(dir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
#endif
}
