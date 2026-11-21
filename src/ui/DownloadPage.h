#pragma once

#include "core/Options.h"
#include "core/PreviewJob.h"

#include <QHash>
#include <QSet>
#include <QPointer>
#include <QTimer>
#include <QWidget>
#include <functional>

class Banner;
class DownloadJob;
class Deno;
class Ffmpeg;
class FetchedTool;
class Library;
class PreviewJob;
class QAction;
class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QFrame;
class QPlainTextEdit;
class QListWidgetItem;
class QPushButton;
class QSplitter;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class Thumbnail;
class YtDlpEngine;

// The Home tab: options on the left, link and preview on top, downloads below.
class DownloadPage : public QWidget
{
    Q_OBJECT

public:
    DownloadPage(YtDlpEngine *engine, Library *library, Ffmpeg *ffmpeg = nullptr, Deno *deno = nullptr, QWidget *parent = nullptr);
    ~DownloadPage() override;

    QAction *logAction() const { return logAction_; }
    // Asked when the video is already in the folder; true replaces it. Tests swap it out.
    std::function<bool(const QString &file)> askOverwrite;
    // Asked the first time a site wants a sign-in, with browser sign-in on; true allows it for that site.
    std::function<bool(const QString &site)> askSignIn;
    QString folder() const;
    void pasteLink();
    void chooseFolder();
    void openFolder();
    void log(const QString &line, int number = 0);
    void notify(const QString &text);   // the banner above the downloads
    // How many downloads run side by side; the rest wait their turn. Kept in AppSettings.
    int downloadsAtOnce() const;
    void setDownloadsAtOnce(int n);

signals:
    void activityChanged(int downloading, int waiting, double bytesPerSecond);
    void folderChanged(const QString &folder);

private:
    QWidget *buildSidebar();
    QWidget *buildTop();
    QWidget *buildDownloads();

    void linkChanged(const QString &text);
    void startPreview();
    void quickPreview(const QString &link);
    void quickPicture(const QString &id, QStringList sizes);
    void showPreview(const PreviewInfo &info);
    void engineChanged();
    void showNotice();
    void showQuality();
    Options::Format currentFormat() const;
    static QString partialFolder();
    QListWidgetItem *formatItem(Options::Format f) const;
    void selectFormat(Options::Format f);
    void restoreSettings();

    Options currentOptions() const;
    void download();
    void addJob(QTreeWidgetItem *item, const Options &o);
    bool offerSignIn(const QString &url);   // true once the link's site may use the browser's sign-in
    void startNext();
    void removeSelected();
    void retrySelected();
    void stopSelected();
    void launchSelected();
    void clearFinished();
    void clearAll();
    void removeRow(QTreeWidgetItem *item);
    void refreshButtons();
    void reportActivity();

    YtDlpEngine *engine_;
    Library *library_;
    Ffmpeg *ffmpeg_;
    Deno *deno_;
    bool toolsBusy() const;   // yt-dlp, ffmpeg or Deno being fetched

    QListWidget *format_ = nullptr;
    QListWidget *quality_ = nullptr;
    QFrame *qualityBox_ = nullptr;
    QLineEdit *folder_ = nullptr;
    QCheckBox *subtitles_ = nullptr;
    QCheckBox *playlist_ = nullptr;
    QCheckBox *sponsors_ = nullptr;

    QLineEdit *link_ = nullptr;
    Banner *banner_ = nullptr;
    Thumbnail *thumbnail_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *meta_ = nullptr;
    QPushButton *download_ = nullptr;
    QLabel *qualityInfo_ = nullptr;

    QTreeWidget *list_ = nullptr;
    QToolButton *remove_ = nullptr, *retry_ = nullptr, *stop_ = nullptr, *launch_ = nullptr;
    QToolButton *clearAll_ = nullptr, *clear_ = nullptr;
    QAction *logAction_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QSplitter *split_ = nullptr;

    QHash<QTreeWidgetItem *, DownloadJob *> jobs_;   // every row has one
    QSet<DownloadJob *> running_;

    QTimer previewDelay_;   // waits for typing or pasting to settle
    QTimer engineRetry_;    // tries to get yt-dlp again while there is none
    QPointer<PreviewJob> preview_;
    QNetworkAccessManager *net_ = nullptr;
    QString previewTitle_;
    QString quickId_;          // YouTube video whose picture and title were fetched directly
    QString quickPictureId_;   // ...and whose picture is showing
    PreviewInfo lastPreview_;   // the current link's, for the quality line
    bool previewPending_ = false;   // a link is in but its qualities aren't known yet
    int wantedQuality_ = 0;         // the row the user picked; shown lower when the video lacks it
    QString signInUrl_;   // a preview needed a sign-in: the download of that link asks for it up front
    QSet<QString> declinedSites_;          // said Not now to this run: not asked again until restart
};
