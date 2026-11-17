#include "MainWindow.h"
#include "ProgressDelegate.h"
#include "Thumbnail.h"
#include "core/DownloadJob.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFontDatabase>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTime>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

enum Column { Title, Format, Size, Progress, State };
constexpr int heights[] = {0, 2160, 1440, 1080, 720, 480};   // matches the Quality list

bool isWebLink(const QString &text)
{
    const QUrl url(text.trimmed());
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty();
}

// Navy bar with a centred white title, like a Windows 3.1 window caption.
QLabel *strip(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("strip");
    return l;
}

// Shows the home folder as ~ so long paths stay readable.
QString shortPath(const QString &path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home) ? "~" + path.mid(home.size()) : path;
}

QString megabytes(double bytes)
{
    return bytes >= 1e9 ? QString::number(bytes / 1e9, 'f', 2) + " GB" : QString::number(bytes / 1e6, 'f', 1) + " MB";
}

}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("QFisher");
    resize(860, 680);
    setMinimumSize(720, 520);

    link_ = new QLineEdit;
    link_->setPlaceholderText("Paste a video link (YouTube, Vimeo, Twitch, and more than a thousand other sites)");
    link_->setClearButtonEnabled(true);
    auto *paste = new QPushButton("Paste");
    paste->setToolTip("Paste the link you copied");

    auto *linkRow = new QHBoxLayout;
    linkRow->setSpacing(6);
    linkRow->addWidget(link_, 1);
    linkRow->addWidget(paste);

    log_ = new QPlainTextEdit;
    log_->setObjectName("log");
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setFixedHeight(150);
    log_->setVisible(false);

    status_ = new QLabel;
    status_->setObjectName("status");

    auto *central = new QWidget;
    central->setObjectName("central");
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(10);
    layout->addLayout(linkRow);
    layout->addWidget(buildCard());
    layout->addWidget(buildList(), 1);
    layout->addWidget(status_);
    layout->addWidget(log_);
    setCentralWidget(central);

    connect(link_, &QLineEdit::textChanged, this, &MainWindow::linkChanged);
    connect(link_, &QLineEdit::returnPressed, this, [this] { if (download_->isEnabled()) download(); });
    connect(paste, &QPushButton::clicked, this, [this] {
        link_->setText(QGuiApplication::clipboard()->text().trimmed());
        link_->setFocus();
    });

    linkChanged({});
    refreshStatus();
}

// Preview on the left; what to save and where on the right.
QWidget *MainWindow::buildCard()
{
    auto *card = new QFrame;
    card->setObjectName("card");

    thumbnail_ = new Thumbnail;
    title_ = new QLabel;
    title_->setWordWrap(true);
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.25);
    big.setBold(true);
    title_->setFont(big);
    meta_ = new QLabel;
    meta_->setObjectName("meta");

    format_ = new QComboBox;
    format_->addItems({"MP4 (plays everywhere)", "MKV", "WebM", "Audio only (MP3)", "Audio only (original)"});
    quality_ = new QComboBox;
    quality_->addItems({"Best available", "2160p (4K)", "1440p", "1080p", "720p", "480p"});
    quality_->setCurrentText("1080p");

    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    folder_ = new QLineEdit(shortPath(movies));
    folder_->setToolTip(movies);
    folder_->setReadOnly(true);
    auto *browse = new QPushButton("Change...");

    subtitles_ = new QCheckBox("Subtitles");
    subtitles_->setToolTip("Add English subtitles to the video when the site has them");
    playlist_ = new QCheckBox("Whole playlist");
    playlist_->setToolTip("When the link is part of a playlist, download every video in it");

    download_ = new QPushButton("Download");
    download_->setDefault(true);  // thick outline marks the main button
    download_->setMinimumSize(120, 34);

    auto *options = new QGridLayout;
    options->setHorizontalSpacing(8);
    options->setVerticalSpacing(6);
    options->addWidget(new QLabel("Format"), 0, 0);
    options->addWidget(format_, 0, 1);
    options->addWidget(new QLabel("Quality"), 0, 2);
    options->addWidget(quality_, 0, 3);
    options->addWidget(new QLabel("Save to"), 1, 0);
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(browse);
    options->addLayout(folderRow, 1, 1, 1, 3);
    options->setColumnStretch(1, 3);
    options->setColumnStretch(3, 2);

    auto *bottom = new QHBoxLayout;
    bottom->addWidget(subtitles_);
    bottom->addSpacing(12);
    bottom->addWidget(playlist_);
    bottom->addStretch(1);
    bottom->addWidget(download_);

    auto *right = new QVBoxLayout;
    right->setSpacing(6);
    right->addWidget(title_);
    right->addWidget(meta_);
    right->addStretch(1);
    right->addLayout(options);
    right->addSpacing(4);
    right->addLayout(bottom);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(10, 4, 10, 10);
    row->setSpacing(14);
    row->addWidget(thumbnail_, 0, Qt::AlignTop);
    row->addLayout(right, 1);

    auto *outer = new QVBoxLayout(card);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(6);
    outer->addWidget(strip("New download"));
    outer->addLayout(row);

    // Audio has no picture size to choose, and no subtitles.
    connect(format_, &QComboBox::currentIndexChanged, this, [this](int i) {
        quality_->setEnabled(i < Options::Mp3);
        subtitles_->setEnabled(i < Options::Mp3);
    });
    connect(browse, &QPushButton::clicked, this, &MainWindow::chooseFolder);
    connect(download_, &QPushButton::clicked, this, &MainWindow::download);
    return card;
}

QWidget *MainWindow::buildList()
{
    list_ = new QTreeWidget;
    list_->setHeaderLabels({"Video", "Format", "Size", "Progress", "Status"});
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setItemDelegateForColumn(Progress, new ProgressDelegate(list_));
    QHeaderView *header = list_->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(Title, QHeaderView::Stretch);
    for (int c : {Format, Size, State})
        header->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(Progress, QHeaderView::Fixed);
    header->resizeSection(Progress, 160);

    remove_ = new QPushButton("Remove");
    remove_->setToolTip("Take the selected downloads off the list (finished files stay)");
    remove_->setEnabled(false);
    auto *logButton = new QPushButton("Log");
    logButton->setToolTip("Show what yt-dlp is doing");
    logButton->setCheckable(true);
    auto *openFolder = new QPushButton("Open folder");
    openFolder->setToolTip("Show the downloaded files");

    auto *buttons = new QHBoxLayout;
    buttons->addWidget(remove_);
    buttons->addStretch(1);
    buttons->addWidget(logButton);
    buttons->addWidget(openFolder);

    auto *box = new QFrame;
    box->setObjectName("card");
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 8);
    layout->setSpacing(8);
    layout->addWidget(strip("Downloads"));
    auto *inner = new QVBoxLayout;
    inner->setContentsMargins(8, 0, 8, 0);
    inner->setSpacing(8);
    inner->addWidget(list_, 1);
    inner->addLayout(buttons);
    layout->addLayout(inner, 1);

    connect(list_, &QTreeWidget::itemSelectionChanged, this, [this] {
        remove_->setEnabled(!list_->selectedItems().isEmpty());
    });
    connect(remove_, &QPushButton::clicked, this, &MainWindow::removeSelected);
    connect(logButton, &QPushButton::toggled, log_, &QWidget::setVisible);
    connect(openFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder_->toolTip()));
    });
    return box;
}

void MainWindow::linkChanged(const QString &text)
{
    const bool ok = isWebLink(text);
    download_->setEnabled(ok);
    if (ok) {
        title_->setText(QUrl(text.trimmed()).host());
        meta_->setText("Preview isn't built yet");
        thumbnail_->setHint("Preview coming soon");
    } else {
        title_->setText("No video yet");
        meta_->setText(text.trimmed().isEmpty() ? "Paste a link above" : "That doesn't look like a web link");
        thumbnail_->setHint("Paste a link to see a preview");
    }
}

void MainWindow::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Save downloads to", folder_->toolTip());
    if (dir.isEmpty())
        return;
    folder_->setText(shortPath(dir));
    folder_->setToolTip(dir);
    refreshStatus();
}

Options MainWindow::currentOptions() const
{
    Options o;
    o.url = link_->text().trimmed();
    o.folder = folder_->toolTip();
    o.format = Options::Format(format_->currentIndex());
    o.maxHeight = heights[quality_->currentIndex()];
    o.subtitles = subtitles_->isChecked();
    o.playlist = playlist_->isChecked();
    return o;
}

void MainWindow::download()
{
    const Options o = currentOptions();
    auto *item = new QTreeWidgetItem(list_, {QUrl(o.url).host(), o.label(), "", "", "Waiting"});
    item->setData(Progress, Qt::UserRole, -1.0);
    item->setToolTip(Title, o.url);
    const int number = list_->indexOfTopLevelItem(item) + 1;

    auto *job = new DownloadJob(o, this);
    jobs_.insert(item, job);
    connect(job, &DownloadJob::logLine, this, [this, number](const QString &line) { log(line, number); });
    connect(job, &DownloadJob::titleKnown, this, [item](const QString &t) { item->setText(Title, t); });
    connect(job, &DownloadJob::stage, this, [item](const QString &s) { item->setText(State, s + "..."); });
    connect(job, &DownloadJob::progress, this, [item](double fraction, qint64 total, double speed, int left) {
        item->setData(Progress, Qt::UserRole, fraction);
        if (total > 0)
            item->setText(Size, megabytes(total));
        QString text = fraction >= 0 ? QString::number(int(fraction * 100)) + "%" : "Downloading";
        if (speed > 0)
            text += "  " + megabytes(speed) + "/s";
        if (left >= 0)
            text += "  " + QTime(0, 0).addSecs(left).toString(left >= 3600 ? "h:mm:ss" : "m:ss") + " left";
        item->setText(State, text);
    });
    connect(job, &DownloadJob::finished, this, [this, item, job, number](bool ok, const QString &message) {
        item->setText(State, message);
        if (ok)
            item->setData(Progress, Qt::UserRole, 1.0);
        log(ok ? "Finished" : "Stopped: " + message, number);
        if (running_ == job)
            running_ = nullptr;
        refreshStatus();
        startNext();
    });

    list_->scrollToItem(item);
    link_->clear();
    startNext();
    refreshStatus();
}

// Downloads run one at a time, top to bottom.
void MainWindow::startNext()
{
    if (running_)
        return;
    const QString ytDlp = QStandardPaths::findExecutable("yt-dlp");
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list_->topLevelItem(i);
        if (item->text(State) != "Waiting")
            continue;
        if (ytDlp.isEmpty()) {
            item->setText(State, "yt-dlp isn't installed");
            log("yt-dlp wasn't found on this computer. Install it and try again.", i + 1);
            continue;
        }
        running_ = jobs_.value(item);
        item->setText(State, "Starting...");
        running_->start(ytDlp);
        break;
    }
    refreshStatus();
}

void MainWindow::removeSelected()
{
    for (QTreeWidgetItem *item : list_->selectedItems()) {
        DownloadJob *job = jobs_.take(item);
        if (job == running_)
            running_ = nullptr;
        delete job;   // stops yt-dlp if it's still running
        delete item;
    }
    startNext();
}

void MainWindow::log(const QString &line, int number)
{
    const QString time = QTime::currentTime().toString("HH:mm:ss");
    log_->appendPlainText(number ? QString("%1 #%2  %3").arg(time).arg(number).arg(line)
                                 : QString("%1  %2").arg(time, line));
}

void MainWindow::refreshStatus()
{
    int waiting = 0;
    for (int i = 0; i < list_->topLevelItemCount(); ++i)
        waiting += list_->topLevelItem(i)->text(State) == "Waiting";
    QString text = running_ ? QString("Downloading") : QString("Nothing downloading");
    if (waiting)
        text += QString(", %1 waiting").arg(waiting);
    status_->setText(text + "  ·  saving to " + folder_->text());
}
