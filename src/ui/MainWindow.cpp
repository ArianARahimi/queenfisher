#include "MainWindow.h"
#include "DownloadPage.h"
#include "EnginePage.h"
#include "LibraryPage.h"
#include "core/Library.h"
#include "core/YtDlpEngine.h"

#include <QApplication>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("QFisher");
    resize(1100, 760);
    setMinimumSize(900, 640);

    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    engine_ = new YtDlpEngine(data, this);
    library_ = new Library(data + "/library.json", this);

    downloads_ = new DownloadPage(engine_, library_);
    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(false);
    tabs_->setIconSize({22, 22});
    tabs_->addTab(downloads_, QIcon(":/icons/tab-download.png"), "Download");
    tabs_->addTab(new LibraryPage(library_), QIcon(":/icons/tab-library.png"), "Library");
    tabs_->addTab(new EnginePage(engine_), QIcon(":/icons/tab-engine.png"), "yt-dlp");
    auto *wordmark = new QLabel("QFisher");
    wordmark->setObjectName("wordmark");
    tabs_->setCornerWidget(wordmark, Qt::TopRightCorner);
    setCentralWidget(tabs_);

    buildMenus();
    buildStatusBar();
    connect(downloads_, &DownloadPage::activityChanged, this, &MainWindow::showActivity);
    connect(engine_, &Engine::readyChanged, this, &MainWindow::showEngine);
    connect(engine_, &Engine::statusChanged, this, &MainWindow::showEngine);
    showActivity(0, 0, 0);
    showEngine();

    // QFisher's own yt-dlp: installed on first run, updated once a day, repaired when a site changes.
    engine_->checkDaily();
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu("&File");
    file->addAction(QIcon(":/icons/paste.png"), "&Paste Link", QKeySequence("Ctrl+Shift+V"), downloads_, &DownloadPage::pasteLink);
    file->addAction("&Choose Download Folder...", downloads_, &DownloadPage::chooseFolder);
    file->addAction(QIcon(":/icons/explore.png"), "&Open Download Folder", downloads_, &DownloadPage::openFolder);
    file->addSeparator();
    file->addAction("E&xit", QKeySequence::Quit, qApp, &QApplication::quit);

    QMenu *view = menuBar()->addMenu("&View");
    const char *names[] = {"&Download", "&Library", "&yt-dlp"};
    for (int i = 0; i < 3; ++i)
        view->addAction(names[i], QKeySequence(QString("Ctrl+%1").arg(i + 1)), this, [this, i] { tabs_->setCurrentIndex(i); });
    view->addSeparator();
    view->addAction(downloads_->logAction());

    QMenu *tools = menuBar()->addMenu("&Tools");
    tools->addAction(QIcon(":/icons/retry.png"), "&Check for yt-dlp Updates", engine_, &YtDlpEngine::updateNow);
    tools->addAction("Use &Nightly yt-dlp", this, [this] { engine_->switchChannel(YtDlpEngine::Nightly); });
    tools->addAction("Use &Regular yt-dlp", this, [this] { engine_->switchChannel(YtDlpEngine::Stable); });

    QMenu *help = menuBar()->addMenu("&Help");
    help->addAction("&About QFisher", this, &MainWindow::about);
}

// Sunken cells left and right, the motto in the middle, like the status bars of the time.
void MainWindow::buildStatusBar()
{
    engineLight_ = new QLabel;
    engineLight_->setObjectName("cell");
    activity_ = new QLabel;
    activity_->setObjectName("cell");
    auto *motto = new QLabel("Free and open source. No ads, no tracking, no account.");
    motto->setObjectName("motto");
    motto->setAlignment(Qt::AlignCenter);
    engineCell_ = new QLabel;
    engineCell_->setObjectName("cell");

    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(engineLight_);
    statusBar()->addWidget(activity_);
    statusBar()->addWidget(motto, 1);
    statusBar()->addPermanentWidget(engineCell_);
}

void MainWindow::showActivity(int downloading, int waiting, double bytesPerSecond)
{
    QString text = QString("↓ %1 downloading").arg(downloading);
    if (bytesPerSecond > 0)
        text += QString(" @ %1 MB/s").arg(bytesPerSecond / 1e6, 0, 'f', 1);
    if (waiting)
        text += QString("  ·  %1 waiting").arg(waiting);
    activity_->setText(text);
}

void MainWindow::showEngine()
{
    const bool busy = engine_->busy();
    const bool ready = !engine_->version().isEmpty();
    const QString colour = busy ? "#e3b341" : ready ? "#4ccf5a" : "#e04a4a";
    engineLight_->setText(QString("<span style='color:%1'>●</span> %2")
                              .arg(colour, busy ? "Updating" : ready ? "Ready" : "No yt-dlp"));
    engineCell_->setText(ready ? "yt-dlp " + engine_->version() : QString("yt-dlp not installed"));
}

void MainWindow::about()
{
    QMessageBox::about(this, "About QFisher", QString(
        "<b>QFisher %1</b><br>Free media downloader.<br><br>"
        "Licensed under the GNU GPL, version 3 or later.<br>"
        "Downloads are done by <a href='https://github.com/yt-dlp/yt-dlp'>yt-dlp</a> (public domain).<br>"
        "Icons from the Tango Desktop Project (public domain).").arg(QApplication::applicationVersion()));
}
