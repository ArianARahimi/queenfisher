#pragma once

#include "core/Options.h"

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QTemporaryDir>
#include <QTimer>

class ClipJob;
class DownloadJob;
class PreviewJob;
class QNetworkAccessManager;
struct PreviewInfo;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class Thumbnail;
class YtDlpEngine;

// Link box, preview card with download options, the download list, and a log at the bottom.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QWidget *buildCard();
    QWidget *buildList();
    void linkChanged(const QString &text);
    void startPreview();
    void showPreview(const PreviewInfo &info);
    void toggleClip();
    void chooseFolder();
    void download();
    void startNext();
    void removeSelected();
    void log(const QString &line, int number = 0);
    void refreshStatus();
    Options currentOptions() const;

    QLineEdit *link_ = nullptr;
    Thumbnail *thumbnail_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *meta_ = nullptr;
    QComboBox *format_ = nullptr;
    QComboBox *quality_ = nullptr;
    QLineEdit *folder_ = nullptr;
    QCheckBox *subtitles_ = nullptr;
    QCheckBox *playlist_ = nullptr;
    QPushButton *download_ = nullptr;
    QTreeWidget *list_ = nullptr;
    QPushButton *remove_ = nullptr;
    QPlainTextEdit *log_ = nullptr;
    QLabel *status_ = nullptr;

    QHash<QTreeWidgetItem *, DownloadJob *> jobs_;   // every row has one; only one runs at a time
    DownloadJob *running_ = nullptr;
    YtDlpEngine *engine_ = nullptr;
    QString engineStatus_;

    QTimer previewDelay_;              // waits for typing or pasting to settle
    QPointer<PreviewJob> preview_;
    QNetworkAccessManager *net_ = nullptr;
    QString previewTitle_;             // for the list once the download starts
    QString signInUrl_, signInCookies_;   // the preview needed a sign-in: reuse it for the download

    QPointer<ClipJob> clip_;
    QTemporaryDir clipDir_;             // short preview clips, deleted when QFisher closes
    QHash<QString, QString> clips_;     // link -> clip file already downloaded
    int previewSeconds_ = -1;
};
