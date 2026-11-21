#pragma once

#include <QMainWindow>

class AppUpdate;
class DownloadPage;
class Deno;
class Ffmpeg;
class FetchedTool;
class Library;
class QLabel;
class QSystemTrayIcon;
class QTabWidget;
class YtDlpEngine;

// Menu bar, the Home / Library / Settings / About tabs, and the status bar.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void bringBack();   // Queenfisher was launched again: show this window, out of the tray if needed

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    static constexpr int settingsTab = 2;
    void buildMenus();
    void buildStatusBar();
    void showActivity(int downloading, int waiting, double bytesPerSecond);
    void showEngine();
    void checkTools();
    void askToContinue(const QString &title, const QString &message);
    void buildTray();
    void toggleWindow();
    void showWindow();
    void updateShowHide();   // the tray menu says what clicking it will do
    void checkForAppUpdate();                  // Help menu, Settings tab: always says what it found
    void appUpdateChecked(bool ok);
    void offerAppUpdate(const QString &version);

    YtDlpEngine *engine_ = nullptr;
    Library *library_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    DownloadPage *downloads_ = nullptr;
    Ffmpeg *ffmpeg_ = nullptr;
    Deno *deno_ = nullptr;
    QLabel *engineLight_ = nullptr;
    QLabel *activity_ = nullptr;
    QLabel *engineCell_ = nullptr;
    QLabel *updateCell_ = nullptr;   // "Queenfisher 0.2.0 is out", a link to the download page
    AppUpdate *appUpdate_ = nullptr;
    bool askedForUpdate_ = false;    // the next answer comes from a check the user started
    QSystemTrayIcon *tray_ = nullptr;
    QAction *toTray_ = nullptr;     // View > Minimize to Tray
    QAction *showHide_ = nullptr;   // the tray menu's first item
    bool busy_ = false;             // something is downloading, converting or waiting
};
