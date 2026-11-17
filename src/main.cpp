// QFisher - free media downloader. GPL-3.0-or-later.
#include "MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("QFisher");
    QApplication::setOrganizationName("QFisher");
    QApplication::setApplicationVersion(QFISHER_VERSION);

    MainWindow window;
    window.show();
    return QApplication::exec();
}
