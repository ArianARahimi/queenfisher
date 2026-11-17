#pragma once

#include <QMainWindow>

class DownloadPage;
class Ffmpeg;
class Library;
class QLabel;
class QSystemTrayIcon;
class QTabWidget;
class YtDlpEngine;

// Menu bar, the Download / Library / About tabs, and the status bar.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void buildMenus();
    void buildStatusBar();
    void showActivity(int downloading, int waiting, double bytesPerSecond);
    void showEngine();
    void checkTools();
    void askToContinue(const QString &title, const QString &message);
    void buildTray();
    void showFromTray();

    YtDlpEngine *engine_ = nullptr;
    Library *library_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    DownloadPage *downloads_ = nullptr;
    Ffmpeg *ffmpeg_ = nullptr;
    bool ffmpegFailureShown_ = false;
    QLabel *engineLight_ = nullptr;
    QLabel *activity_ = nullptr;
    QLabel *engineCell_ = nullptr;
    QSystemTrayIcon *tray_ = nullptr;
    QAction *toTray_ = nullptr;   // View > Minimize to Tray
};
