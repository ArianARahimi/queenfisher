#pragma once

#include <QMainWindow>

class DownloadPage;
class Library;
class QLabel;
class QTabWidget;
class YtDlpEngine;

// Menu bar, the Download / Library / yt-dlp tabs, and the status bar.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void buildMenus();
    void buildStatusBar();
    void showActivity(int downloading, int waiting, double bytesPerSecond);
    void showEngine();
    void about();

    YtDlpEngine *engine_ = nullptr;
    Library *library_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    DownloadPage *downloads_ = nullptr;
    QLabel *engineLight_ = nullptr;
    QLabel *activity_ = nullptr;
    QLabel *engineCell_ = nullptr;
};
