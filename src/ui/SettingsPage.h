#pragma once

#include <QWidget>

class DownloadPage;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class YtDlpEngine;

// The Settings tab: downloads, sign-in, window and yt-dlp. Every change is saved at once
// and the Settings menu shows the same values.
class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    SettingsPage(DownloadPage *downloads, YtDlpEngine *engine, bool trayAvailable, QWidget *parent = nullptr);

private:
    void refresh();   // show what's saved now, whoever changed it

    DownloadPage *downloads_;
    YtDlpEngine *engine_;
    QLineEdit *folder_ = nullptr;
    QComboBox *atOnce_ = nullptr;
    QComboBox *existing_ = nullptr;
    QCheckBox *signIn_ = nullptr;
    QCheckBox *tray_ = nullptr;
    QLabel *version_ = nullptr;
    QComboBox *channel_ = nullptr;
};
