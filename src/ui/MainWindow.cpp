#include "MainWindow.h"
#include "AboutPage.h"
#include "DownloadPage.h"
#include "LibraryPage.h"
#include "Theme.h"
#include "core/Ffmpeg.h"
#include "core/Library.h"
#include "core/Tools.h"
#include "core/YtDlpEngine.h"

#include <QApplication>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QCloseEvent>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabWidget>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("Queenfisher");
    setWindowIcon(QIcon(":/queenfisher.png"));
    resize(1100, 760);
    setMinimumSize(900, 640);
    restoreGeometry(QSettings().value("window").toByteArray());   // size and place from last time

    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    engine_ = new YtDlpEngine(data, this);
    library_ = new Library(data + "/library.json", this);

    ffmpeg_ = new Ffmpeg(data, this);
    downloads_ = new DownloadPage(engine_, library_, ffmpeg_);
    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(false);
    tabs_->setIconSize({22, 22});
    tabs_->addTab(downloads_, windowIcon(), "Download");   // the fish
    tabs_->addTab(new LibraryPage(library_), QIcon(":/icons/tab-library.png"), "Library");
    tabs_->addTab(new AboutPage, QIcon(":/icons/info.png"), "About");
    auto *wordmark = new QLabel("Queenfisher");
    wordmark->setObjectName("wordmark");
    tabs_->setCornerWidget(wordmark, Qt::TopRightCorner);
    setCentralWidget(tabs_);

    buildTray();
    buildMenus();
    buildStatusBar();
    connect(downloads_, &DownloadPage::activityChanged, this, &MainWindow::showActivity);
    connect(engine_, &Engine::readyChanged, this, &MainWindow::showEngine);
    connect(engine_, &Engine::statusChanged, this, &MainWindow::showEngine);
    showActivity(0, 0, 0);
    showEngine();

    // Queenfisher's own yt-dlp: installed on first run, updated once a day, repaired when a site changes.
    engine_->checkDaily();

    // Once the window is up: a popup for each missing tool, and for a failed ffmpeg fetch (once).
    QTimer::singleShot(0, this, &MainWindow::checkTools);
    connect(ffmpeg_, &Ffmpeg::readyChanged, this, [this] {
        if (!ffmpeg_->busy() && !ffmpeg_->lastError().isEmpty() && !std::exchange(ffmpegFailureShown_, true))
            askToContinue("ffmpeg", ffmpeg_->lastError() + ".\nQueenfisher will try again; until then most downloads can't join picture and sound.");
    });
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QSettings().setValue("window", saveGeometry());
    QMainWindow::closeEvent(event);
}

void MainWindow::checkTools()
{
#ifdef Q_OS_WIN
    const bool ownFfmpeg = true;   // fetched by Queenfisher itself
#else
    const bool ownFfmpeg = false;
#endif
    for (const MissingTool &tool : missingTools(ownFfmpeg))
        askToContinue(tool.name, tool.message);
}

// OK carries on without it; Cancel closes Queenfisher.
void MainWindow::askToContinue(const QString &title, const QString &message)
{
    if (QMessageBox::warning(this, title + " is missing", message, QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Ok)
        == QMessageBox::Cancel)
        QTimer::singleShot(0, qApp, &QApplication::quit);
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
    const char *names[] = {"&Download", "&Library", "&About"};
    for (int i = 0; i < 3; ++i)
        view->addAction(names[i], QKeySequence(QString("Ctrl+%1").arg(i + 1)), this, [this, i] { tabs_->setCurrentIndex(i); });
    view->addSeparator();
    view->addAction(downloads_->logAction());
    view->addAction(toTray_);

    QMenu *tools = menuBar()->addMenu("&Tools");
    tools->addAction(QIcon(":/icons/retry.png"), "&Check for yt-dlp Updates", engine_, &YtDlpEngine::updateNow);
    tools->addAction("Use &Nightly yt-dlp", this, [this] { engine_->switchChannel(YtDlpEngine::Nightly); });
    tools->addAction("Use &Regular yt-dlp", this, [this] { engine_->switchChannel(YtDlpEngine::Stable); });

    QMenu *help = menuBar()->addMenu("&Help");
    help->addAction("&About Queenfisher", this, [this] { tabs_->setCurrentIndex(2); });
}

// Sunken cells left and right, like the status bars of the time.
void MainWindow::buildStatusBar()
{
    engineLight_ = new QLabel;
    engineLight_->setObjectName("cell");
    activity_ = new QLabel;
    activity_->setObjectName("cell");
    engineCell_ = new QLabel;
    engineCell_->setObjectName("cell");

    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(engineLight_);
    statusBar()->addWidget(activity_);
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
    if (tray_)
        tray_->setToolTip("Queenfisher\n" + text);
}

// Tray icon: click to show or hide the window; minimizing hides it there when that's on.
void MainWindow::buildTray()
{
    toTray_ = new QAction("Minimize to &Tray", this);
    toTray_->setCheckable(true);
    toTray_->setChecked(QSettings().value("minimizeToTray", true).toBool());
    connect(toTray_, &QAction::toggled, this, [](bool on) { QSettings().setValue("minimizeToTray", on); });
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        toTray_->setEnabled(false);   // nowhere to go: minimizing works as usual
        return;
    }
    tray_ = new QSystemTrayIcon(windowIcon(), this);
    auto *menu = new QMenu(this);
    menu->addAction("&Show Queenfisher", this, &MainWindow::showFromTray);
    menu->addSeparator();
    menu->addAction("&Quit", qApp, &QApplication::quit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason why) {
        if (why != QSystemTrayIcon::Trigger)
            return;
        if (isVisible() && !isMinimized())
            hide();
        else
            showFromTray();
    });
    tray_->show();
}

void MainWindow::showFromTray()
{
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && isMinimized() && tray_ && toTray_->isChecked())
        QTimer::singleShot(0, this, &QWidget::hide);   // after the minimize finishes
}

void MainWindow::showEngine()
{
    const bool busy = engine_->busy();
    const bool ready = !engine_->version().isEmpty();
    const QString colour = busy ? Theme::busy.name() : ready ? Theme::accent.name() : Theme::failed.name();
    engineLight_->setText(QString("<span style='color:%1'>●</span> %2")
                              .arg(colour, busy ? "Updating" : ready ? "Ready" : "No yt-dlp"));
    engineCell_->setText(ready ? "yt-dlp " + engine_->version() : QString("yt-dlp not installed"));
}
