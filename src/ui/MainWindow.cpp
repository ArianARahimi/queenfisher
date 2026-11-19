#include "MainWindow.h"
#include "AboutPage.h"
#include "AppSettings.h"
#include "DownloadPage.h"
#include "LibraryPage.h"
#include "SettingsPage.h"
#include "Theme.h"
#include "core/AppUpdate.h"
#include "core/Deno.h"
#include "core/Ffmpeg.h"
#include "core/Library.h"
#include "core/Tools.h"
#include "core/YtDlpEngine.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>

#include <memory>

#pragma region Window and menus
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("Queenfisher");
    setWindowIcon(QIcon(":/queenfisher.png"));
    setMinimumSize(900, 600);
    if (!restoreGeometry(QSettings().value("window").toByteArray())) {   // size and place from last time
        // First run: 1100 x 760, or less on a small screen so the whole window fits.
        const QRect screen = QGuiApplication::primaryScreen()->availableGeometry();
        resize(qMin(1100, screen.width() - 40), qMin(760, screen.height() - 60));
    }

    const QString data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    engine_ = new YtDlpEngine(data, this);
    library_ = new Library(data + "/library.json", this);

    ffmpeg_ = new Ffmpeg(data, this);
    deno_ = new Deno(data, this);
    downloads_ = new DownloadPage(engine_, library_, ffmpeg_, deno_);
    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(false);
    tabs_->setIconSize({22, 22});
    tabs_->addTab(downloads_, QIcon(":/icons/tab-home.png"), "Home");
    tabs_->addTab(new LibraryPage(library_), QIcon(":/icons/tab-library.png"), "Library");
    auto *settingsPage = new SettingsPage(downloads_, engine_, QSystemTrayIcon::isSystemTrayAvailable());
    tabs_->addTab(settingsPage, QIcon(":/icons/tab-settings.png"), "Settings");
    tabs_->addTab(new AboutPage, QIcon(":/icons/info.png"), "About");
    // The fish and the name, top right.
    auto *fish = new QLabel;
    fish->setPixmap(windowIcon().pixmap(26, 26));
    auto *wordmark = new QLabel("Queenfisher");
    wordmark->setObjectName("wordmark");
    auto *corner = new QWidget;
    auto *cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(1);   // the picture has its own clear margin
    cornerLayout->addWidget(fish);
    cornerLayout->addWidget(wordmark);
    tabs_->setCornerWidget(corner, Qt::TopRightCorner);
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

    // A newer Queenfisher: a few seconds after start, then hourly while it runs (at most one check a day).
    appUpdate_ = new AppUpdate(QApplication::applicationVersion(), this);
    connect(appUpdate_, &AppUpdate::checked, this, &MainWindow::appUpdateChecked);
    connect(settingsPage, &SettingsPage::checkForAppUpdate, this, &MainWindow::checkForAppUpdate);
    const auto daily = [this] {
        if (AppSettings::get().updateCheck())
            appUpdate_->checkDaily();
    };
    QTimer::singleShot(5000, this, daily);
    auto *hourly = new QTimer(this);
    connect(hourly, &QTimer::timeout, this, daily);
    hourly->start(3600 * 1000);

    // Once the window is up: a popup for each missing tool, and for a failed fetch (once each).
    QTimer::singleShot(0, this, &MainWindow::checkTools);
    const auto failurePopup = [this](FetchedTool *tool, const QString &title, const QString &without) {
        auto shown = std::make_shared<bool>(false);
        connect(tool, &FetchedTool::readyChanged, this, [this, tool, title, without, shown] {
            if (!tool->busy() && !tool->lastError().isEmpty() && !std::exchange(*shown, true))
                askToContinue(title, tool->lastError() + ".\nQueenfisher will try again; " + without);
        });
    };
    failurePopup(ffmpeg_, "ffmpeg", "until then most downloads can't join picture and sound.");
    failurePopup(deno_, "Deno", "until then YouTube offers only low qualities.");
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
    file->addAction(QIcon(":/icons/explore.png"), "&Open Downloads Directory", downloads_, &DownloadPage::openFolder);
    file->addSeparator();
    file->addAction("E&xit", QKeySequence::Quit, qApp, &QApplication::quit);

    QMenu *view = menuBar()->addMenu("&View");
    for (int i = 0; i < tabs_->count(); ++i)
        view->addAction("&" + tabs_->tabText(i), QKeySequence(QString("Ctrl+%1").arg(i + 1)), this,
                        [this, i] { tabs_->setCurrentIndex(i); });
    view->addSeparator();
    view->addAction(downloads_->logAction());
    view->addAction(toTray_);

    // The same settings as the Settings tab, ticked to match whenever the menu opens.
    AppSettings &s = AppSettings::get();
    QMenu *settings = menuBar()->addMenu("&Settings");
    settings->addAction(QIcon(":/icons/tab-settings.png"), "&Settings...", QKeySequence::Preferences, this,
                        [this] { tabs_->setCurrentIndex(settingsTab); });
    settings->addSeparator();
    QMenu *atOnce = settings->addMenu("&Downloads at Once");
    auto *atOnceGroup = new QActionGroup(atOnce);
    for (int n = 1; n <= AppSettings::maxAtOnce; ++n) {
        QAction *a = atOnce->addAction(QString::number(n), this, [this, n] { downloads_->setDownloadsAtOnce(n); });
        a->setCheckable(true);
        a->setData(n);
        atOnceGroup->addAction(a);
    }
    QAction *signIn = settings->addAction("Use Browser &Sign-in", &s, &AppSettings::setBrowserSignIn);
    signIn->setCheckable(true);
    QAction *trayOnMinimize = settings->addAction("&Hide in Tray When Minimized", &s, &AppSettings::setMinimizeToTray);
    trayOnMinimize->setCheckable(true);
    trayOnMinimize->setEnabled(tray_ != nullptr);
    settings->addSeparator();
    settings->addAction(QIcon(":/icons/retry.png"), "&Check for yt-dlp Updates", engine_, &YtDlpEngine::updateNow);
    QAction *nightly = settings->addAction("Use &Nightly yt-dlp", this, [this](bool on) {
        engine_->switchChannel(on ? YtDlpEngine::Nightly : YtDlpEngine::Stable);
    });
    nightly->setCheckable(true);
    connect(settings, &QMenu::aboutToShow, this, [this, atOnceGroup, signIn, trayOnMinimize, nightly] {
        const AppSettings &s = AppSettings::get();
        for (QAction *a : atOnceGroup->actions())
            a->setChecked(a->data().toInt() == s.downloadsAtOnce());
        signIn->setChecked(s.browserSignIn());
        trayOnMinimize->setChecked(s.minimizeToTray());
        nightly->setChecked(engine_->channel() == YtDlpEngine::Nightly);
        nightly->setEnabled(!engine_->busy());
    });

    QMenu *help = menuBar()->addMenu("&Help");
    help->addAction("Check for Queenfisher &Updates", this, &MainWindow::checkForAppUpdate);
    help->addSeparator();
    help->addAction("&About Queenfisher", this, [this] { tabs_->setCurrentIndex(tabs_->count() - 1); });
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
    updateCell_ = new QLabel;
    updateCell_->setObjectName("cell");
    updateCell_->setOpenExternalLinks(true);
    updateCell_->hide();
    statusBar()->addPermanentWidget(updateCell_);
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
#pragma endregion

#pragma region Tray and status
// Tray icon: click it (or its first menu item) to show or hide the window. Minimizing, or
// View > Minimize to Tray, puts the window there.
void MainWindow::buildTray()
{
    toTray_ = new QAction("Minimize to &Tray", this);
    toTray_->setShortcut(QKeySequence("Ctrl+M"));
    connect(toTray_, &QAction::triggered, this, &QWidget::hide);
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        toTray_->setEnabled(false);   // nowhere to go: minimizing works as usual
        return;
    }
    tray_ = new QSystemTrayIcon(windowIcon(), this);
    auto *menu = new QMenu(this);
    showHide_ = menu->addAction("&Hide Queenfisher", this, &MainWindow::toggleWindow);
    menu->addSeparator();
    menu->addAction("&Quit", qApp, &QApplication::quit);
    tray_->setContextMenu(menu);
    connect(tray_, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason why) {
        if (why == QSystemTrayIcon::Trigger)
            toggleWindow();
    });
    tray_->show();
}

void MainWindow::toggleWindow()
{
    if (isVisible() && !isMinimized()) {
        hide();
        return;
    }
    showNormal();
    raise();
    activateWindow();
}

void MainWindow::updateShowHide()
{
    if (showHide_)
        showHide_->setText(isVisible() && !isMinimized() ? "&Hide Queenfisher" : "&Show Queenfisher");
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    updateShowHide();
}

void MainWindow::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);
    updateShowHide();
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange)
        return;
    updateShowHide();
    if (isMinimized() && tray_ && AppSettings::get().minimizeToTray())
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
#pragma endregion

#pragma region Queenfisher updates
void MainWindow::checkForAppUpdate()
{
    askedForUpdate_ = true;
    appUpdate_->checkNow();
}

void MainWindow::appUpdateChecked(bool ok)
{
    const bool asked = std::exchange(askedForUpdate_, false);
    const QString version = appUpdate_->newer();
    const bool skipped = !version.isEmpty() && version == AppSettings::get().skippedVersion();
    updateCell_->setText(QString("<a href='%1' style='color:%2'>Queenfisher %3 is out</a>")
                             .arg(AppUpdate::downloadPage, Theme::accent.name(), version));
    updateCell_->setVisible(!version.isEmpty() && !skipped);
    if (asked && !ok)
        QMessageBox::warning(this, "Queenfisher update", "Couldn't reach GitHub to check. Try again later.");
    else if (asked && version.isEmpty())
        QMessageBox::information(this, "Queenfisher update",
                                 "You have the newest Queenfisher (" + QApplication::applicationVersion() + ").");
    else if (!version.isEmpty() && (asked || !skipped))
        offerAppUpdate(version);
}

// Open Download Page or Not Now; "don't show again" keeps quiet about this version only.
void MainWindow::offerAppUpdate(const QString &version)
{
    QMessageBox box(this);
    box.setWindowTitle("Queenfisher update");
    box.setIcon(QMessageBox::Information);
    box.setText(QString("Queenfisher %1 is out. You have %2.").arg(version, QApplication::applicationVersion()));
    box.setInformativeText("Get it from the download page. Your settings and Library stay as they are.");
    QPushButton *open = box.addButton("Open Download Page", QMessageBox::AcceptRole);
    box.addButton("Not Now", QMessageBox::RejectRole);
    box.setDefaultButton(open);
    auto *quiet = new QCheckBox("Don't show this again until the next version");
    box.setCheckBox(quiet);
    box.exec();
    if (box.clickedButton() == open) {
        QDesktopServices::openUrl(QUrl(AppUpdate::downloadPage));
    } else if (quiet->isChecked()) {
        AppSettings::get().setSkippedVersion(version);
        updateCell_->hide();
    }
}
#pragma endregion
