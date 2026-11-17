#pragma once

#include "core/Options.h"

#include <QHash>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

class Banner;
class ClipJob;
class DownloadJob;
class Ffmpeg;
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
class QPushButton;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class Thumbnail;
class YtDlpEngine;
struct PreviewInfo;

// The Download tab: options on the left, link and preview on top, downloads below.
class DownloadPage : public QWidget
{
    Q_OBJECT

public:
    DownloadPage(YtDlpEngine *engine, Library *library, Ffmpeg *ffmpeg = nullptr, QWidget *parent = nullptr);
    ~DownloadPage() override;

    QAction *logAction() const { return logAction_; }
    QString folder() const;
    void pasteLink();
    void chooseFolder();
    void openFolder();
    void log(const QString &line, int number = 0);

signals:
    void activityChanged(int downloading, int waiting, double bytesPerSecond);

private:
    QWidget *buildSidebar();
    QWidget *buildTop();
    QWidget *buildDownloads();

    void linkChanged(const QString &text);
    void startPreview();
    void showPreview(const PreviewInfo &info);
    void toggleClip();
    void fetchClip(bool play);
    void engineChanged();

    Options currentOptions() const;
    void download();
    void addJob(QTreeWidgetItem *item, const Options &o);
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

    QListWidget *format_ = nullptr;
    QListWidget *quality_ = nullptr;
    QFrame *qualityBox_ = nullptr;
    QLineEdit *folder_ = nullptr;
    QCheckBox *subtitles_ = nullptr;
    QCheckBox *playlist_ = nullptr;

    QLineEdit *link_ = nullptr;
    Banner *banner_ = nullptr;
    Thumbnail *thumbnail_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *meta_ = nullptr;
    QPushButton *download_ = nullptr;
    QPushButton *play_ = nullptr;
    QPushButton *openPage_ = nullptr;

    QTreeWidget *list_ = nullptr;
    QToolButton *remove_ = nullptr, *retry_ = nullptr, *stop_ = nullptr, *launch_ = nullptr;
    QToolButton *clearAll_ = nullptr, *clear_ = nullptr;
    QAction *logAction_ = nullptr;
    QPlainTextEdit *log_ = nullptr;

    QHash<QTreeWidgetItem *, DownloadJob *> jobs_;   // every row has one; only one runs at a time
    DownloadJob *running_ = nullptr;

    QTimer previewDelay_;   // waits for typing or pasting to settle
    QTimer engineRetry_;    // tries to get yt-dlp again while there is none
    QPointer<PreviewJob> preview_;
    QNetworkAccessManager *net_ = nullptr;
    QString previewTitle_;
    int previewSeconds_ = -1;
    QString signInUrl_, signInCookies_;   // a preview needed a sign-in: reuse it for that link

    QPointer<ClipJob> clip_;
    QTemporaryDir clipDir_;               // preview clips, deleted when Queenfisher closes
    QHash<QString, QString> clips_;       // link -> clip file
    bool playWhenReady_ = false;          // clicked while the clip was still loading
};
