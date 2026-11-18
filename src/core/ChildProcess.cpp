#include "ChildProcess.h"

#include <QProcess>

#ifdef Q_OS_UNIX
#include <csignal>
#include <unistd.h>
#endif

namespace ChildProcess {

void ownGroup(QProcess &p)
{
#ifdef Q_OS_UNIX
    p.setChildProcessModifier([] { ::setpgid(0, 0); });
#else
    Q_UNUSED(p);
#endif
}

void stop(QProcess &p, bool hard)
{
    const qint64 pid = p.processId();
    if (pid <= 0 || p.state() == QProcess::NotRunning)
        return;
#ifdef Q_OS_UNIX
    ::kill(-pid_t(pid), hard ? SIGKILL : SIGTERM);
#else
    // Windows has no signals for console programs; taskkill /T ends the process and its children.
    Q_UNUSED(hard);
    QProcess::execute("taskkill", {"/T", "/F", "/PID", QString::number(pid)});
#endif
}

void stopAndWait(QProcess &p)
{
    if (p.state() == QProcess::NotRunning)
        return;
    stop(p, true);
    if (!p.waitForFinished(2000))
        p.kill();
}

}
