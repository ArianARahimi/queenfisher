#include "DownloadPage.h"
#include "ProgressDelegate.h"
#include "Theme.h"
#include "Thumbnail.h"
#include "Widgets.h"
#include "core/ClipJob.h"
#include "core/DownloadJob.h"
#include "core/Library.h"
#include "core/PreviewJob.h"
#include "core/YtDlpEngine.h"

#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardPaths>
#include <QTime>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

enum Column { Name, Format, Size, Status, Progress, Speed, Time };
enum class State { Waiting, Running, Done, Failed, Stopped };
constexpr int stateRole = Qt::UserRole + 10;   // State of a row, on the Name column
constexpr int heights[] = {0, 2160, 1440, 1080, 720, 480};   // matches the Quality list

bool isWebLink(const QString &text)
{
    const QUrl url(text.trimmed());
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty();
}

// Shows the home folder as ~ so long paths stay readable.
QString shortPath(const QString &path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home) ? "~" + path.mid(home.size()) : path;
}

QString clock(int seconds)
{
    return QTime(0, 0).addSecs(seconds).toString(seconds >= 3600 ? "h:mm:ss" : "m:ss");
}

QString bytes(double n)
{
    if (n >= 1e9) return QString::number(n / 1e9, 'f', 2) + " GB";
    if (n >= 1e6) return QString::number(n / 1e6, 'f', 1) + " MB";
    return QString::number(n / 1e3, 'f', 0) + " KB";
}

State stateOf(const QTreeWidgetItem *item) { return State(item->data(Name, stateRole).toInt()); }

void setState(QTreeWidgetItem *item, State s, const QString &status)
{
    item->setData(Name, stateRole, int(s));
    item->setText(Status, status);
    const Theme::BarState bar = s == State::Running ? Theme::BarState::Running
                              : s == State::Done    ? Theme::BarState::Done
                              : s == State::Waiting ? Theme::BarState::Waiting
                                                    : Theme::BarState::Failed;
    item->setData(Progress, ProgressDelegate::stateRole, int(bar));
    if (s != State::Running) {
        item->setText(Speed, {});
        item->setText(Time, {});
    }
}

QHBoxLayout *actionRow(std::initializer_list<QWidget *> widgets)
{
    auto *row = new QHBoxLayout;
    row->setSpacing(4);
    row->addStretch(1);
    for (QWidget *w : widgets)
        row->addWidget(w);
    row->addStretch(1);
    return row;
}

}

DownloadPage::DownloadPage(YtDlpEngine *engine, Library *library, QWidget *parent)
    : QWidget(parent), engine_(engine), library_(library)
{
    setObjectName("page");
    net_ = new QNetworkAccessManager(this);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(buildTop());
    split->addWidget(buildDownloads());
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setChildrenCollapsible(false);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(buildSidebar());
    layout->addWidget(split, 1);

    previewDelay_.setSingleShot(true);
    previewDelay_.setInterval(500);
    connect(&previewDelay_, &QTimer::timeout, this, &DownloadPage::startPreview);
    connect(engine_, &Engine::readyChanged, this, &DownloadPage::engineChanged);
    connect(engine_, &Engine::statusChanged, this, [this](const QString &text) {
        if (engine_->busy())
            banner_->setNotice(Banner::Info, text);
    });
    connect(engine_, &Engine::logLine, this, [this](const QString &line) { log(line); });

    linkChanged({});
    engineChanged();
}

// Gold-header boxes: what to save and where.
QWidget *DownloadPage::buildSidebar()
{
    format_ = new QListWidget;
    const struct { const char *icon, *text; } formats[] = {
        {"video", "MP4 (plays everywhere)"}, {"video", "MKV"}, {"video", "WebM"},
        {"audio", "MP3"}, {"audio", "Audio (original)"}};
    for (const auto &f : formats)
        new QListWidgetItem(QIcon(QString(":/icons/") + f.icon + ".png"), f.text, format_);
    format_->setCurrentRow(Options::Mp4);

    quality_ = new QListWidget;
    quality_->addItems({"Best available", "2160p (4K)", "1440p", "1080p", "720p", "480p"});
    quality_->setCurrentRow(3);
    for (QListWidget *l : {format_, quality_}) {   // tall enough for every row, no scroll bar
        l->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        l->setFixedHeight(l->sizeHintForRow(0) * l->count() + 2 * l->frameWidth() + 2);
    }

    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    folder_ = new QLineEdit(shortPath(movies));
    folder_->setToolTip(movies);
    folder_->setReadOnly(true);
    auto *change = new QPushButton("Change...");

    subtitles_ = new QCheckBox("Subtitles");
    subtitles_->setToolTip("Add English subtitles to the video when the site has them");
    playlist_ = new QCheckBox("Whole playlist");
    playlist_->setToolTip("When the link is part of a playlist, download every video in it");

    auto *formatBox = new QVBoxLayout;
    formatBox->addWidget(format_);
    auto *qualityBox = new QVBoxLayout;
    qualityBox->addWidget(quality_);
    auto *folderBox = new QVBoxLayout;
    folderBox->setSpacing(4);
    folderBox->addWidget(folder_);
    folderBox->addWidget(change, 0, Qt::AlignRight);
    auto *extrasBox = new QVBoxLayout;
    extrasBox->setSpacing(4);
    extrasBox->addWidget(subtitles_);
    extrasBox->addWidget(playlist_);

    auto *openFolder = new QPushButton("Open Download Folder");
    auto *side = new QWidget;
    side->setFixedWidth(210);
    auto *layout = new QVBoxLayout(side);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto *title = new QLabel("Download Options:");
    title->setObjectName("sideTitle");
    layout->addWidget(title);
    layout->addWidget(Widgets::sideBox("Format", formatBox));
    layout->addWidget(Widgets::sideBox("Quality", qualityBox));
    layout->addWidget(Widgets::sideBox("Save To", folderBox));
    layout->addWidget(Widgets::sideBox("Extras", extrasBox));
    layout->addStretch(1);
    layout->addWidget(openFolder);

    // Audio has no picture size to choose, and no subtitles.
    connect(format_, &QListWidget::currentRowChanged, this, [this](int row) {
        quality_->setEnabled(row < Options::Mp3);
        subtitles_->setEnabled(row < Options::Mp3);
    });
    connect(change, &QPushButton::clicked, this, &DownloadPage::chooseFolder);
    connect(openFolder, &QPushButton::clicked, this, &DownloadPage::openFolder);
    connect(playlist_, &QCheckBox::toggled, this, [this] { if (isWebLink(link_->text())) previewDelay_.start(); });
    return side;
}

// Link box, a tab with the current title, the notice bar, the preview and its actions.
QWidget *DownloadPage::buildTop()
{
    link_ = new QLineEdit;
    link_->setPlaceholderText("Paste a video link (YouTube, Vimeo, Twitch, and more than a thousand other sites)");
    link_->setClearButtonEnabled(true);
    auto *paste = new QPushButton(QIcon(":/icons/paste.png"), "Paste");
    paste->setToolTip("Paste the link you copied");

    chip_ = new QLabel;
    chip_->setObjectName("chip");
    banner_ = new Banner;

    thumbnail_ = new Thumbnail;
    title_ = new QLabel;
    title_->setWordWrap(true);
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.3);
    big.setBold(true);
    title_->setFont(big);
    meta_ = new QLabel;
    meta_->setObjectName("meta");
    meta_->setWordWrap(true);

    auto *info = new QVBoxLayout;
    info->setSpacing(6);
    info->addWidget(title_);
    info->addWidget(meta_);
    info->addStretch(1);
    auto *previewRow = new QHBoxLayout;
    previewRow->setSpacing(12);
    previewRow->addWidget(thumbnail_, 0, Qt::AlignTop);
    previewRow->addLayout(info, 1);

    auto *pane = new QFrame;
    pane->setObjectName("previewPane");
    auto *paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(8, 8, 8, 8);
    paneLayout->setSpacing(8);
    paneLayout->addWidget(banner_);
    paneLayout->addLayout(previewRow, 1);

    download_ = Widgets::action("download", "Download", "Download this link with the options on the left");
    play_ = Widgets::action("play", "Play Clip", "Play a short clip with sound in the preview");
    openPage_ = Widgets::action("browser", "Open Page", "Open the link in your web browser");
    auto *junk = Widgets::action("trash", "Clear", "Clear the link");

    auto *actions = actionRow({download_, play_, openPage_});
    actions->insertSpacing(0, 80);   // balances the Clear button on the right
    actions->addWidget(junk);

    auto *linkRow = new QHBoxLayout;
    linkRow->setSpacing(6);
    linkRow->addWidget(link_, 1);
    linkRow->addWidget(paste);

    auto *top = new QWidget;
    auto *layout = new QVBoxLayout(top);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto *tabbed = new QVBoxLayout;   // the title tab sits right on the pane
    tabbed->setSpacing(0);
    tabbed->addWidget(chip_, 0, Qt::AlignLeft);
    tabbed->addWidget(pane, 1);
    layout->addLayout(linkRow);
    layout->addLayout(tabbed, 1);
    layout->addLayout(actions);

    connect(link_, &QLineEdit::textChanged, this, &DownloadPage::linkChanged);
    connect(link_, &QLineEdit::returnPressed, this, [this] { if (download_->isEnabled()) download(); });
    connect(paste, &QPushButton::clicked, this, &DownloadPage::pasteLink);
    connect(download_, &QToolButton::clicked, this, &DownloadPage::download);
    connect(play_, &QToolButton::clicked, this, &DownloadPage::toggleClip);
    connect(thumbnail_, &Thumbnail::clicked, this, &DownloadPage::toggleClip);
    connect(openPage_, &QToolButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl(link_->text().trimmed())); });
    connect(junk, &QToolButton::clicked, link_, &QLineEdit::clear);
    return top;
}

// The download list with its row of actions, and the log under it.
QWidget *DownloadPage::buildDownloads()
{
    list_ = new QTreeWidget;
    list_->setHeaderLabels({"Name", "Format", "Size", "Status", "Progress", "Speed", "Time"});
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list_->setItemDelegateForColumn(Progress, new ProgressDelegate(list_));
    QHeaderView *header = list_->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(Name, QHeaderView::Stretch);
    for (int c : {Format, Size, Speed, Time})
        header->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(Status, QHeaderView::Interactive);
    header->resizeSection(Status, 190);
    header->setSectionResizeMode(Progress, QHeaderView::Fixed);
    header->resizeSection(Progress, 120);

    remove_ = Widgets::action("remove", "Remove", "Take the selected downloads off the list (finished files stay)");
    retry_ = Widgets::action("retry", "Retry", "Try the selected failed or stopped downloads again");
    stop_ = Widgets::action("stop", "Stop", "Stop the selected downloads");
    launch_ = Widgets::action("launch", "Launch", "Open the selected finished file");
    auto *logButton = Widgets::action("log", "Log", "Show what yt-dlp is doing");
    auto *explore = Widgets::action("explore", "Explore", "Show the downloaded files");
    clear_ = Widgets::action("clear", "Clear Finished", "Take finished downloads off the list");

    logAction_ = new QAction(QIcon(":/icons/log.png"), "Log", this);
    logAction_->setCheckable(true);
    logAction_->setToolTip(logButton->toolTip());
    logButton->setDefaultAction(logAction_);

    log_ = new QPlainTextEdit;
    log_->setObjectName("log");
    log_->setReadOnly(true);
    log_->setMaximumBlockCount(5000);
    log_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log_->setFixedHeight(140);
    log_->setVisible(false);

    auto *title = new QLabel("Downloads");
    title->setObjectName("paneTitle");
    auto *box = new QWidget;
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(3);
    layout->addWidget(title);
    layout->addWidget(list_, 1);
    layout->addSpacing(3);
    layout->addLayout(actionRow({remove_, retry_, stop_, launch_, logButton, explore, clear_}));
    layout->addWidget(log_);

    connect(list_, &QTreeWidget::itemSelectionChanged, this, &DownloadPage::refreshButtons);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, &DownloadPage::launchSelected);
    connect(remove_, &QToolButton::clicked, this, &DownloadPage::removeSelected);
    connect(retry_, &QToolButton::clicked, this, &DownloadPage::retrySelected);
    connect(stop_, &QToolButton::clicked, this, &DownloadPage::stopSelected);
    connect(launch_, &QToolButton::clicked, this, &DownloadPage::launchSelected);
    connect(explore, &QToolButton::clicked, this, &DownloadPage::openFolder);
    connect(clear_, &QToolButton::clicked, this, &DownloadPage::clearFinished);
    connect(logAction_, &QAction::toggled, log_, &QWidget::setVisible);
    refreshButtons();
    return box;
}

QString DownloadPage::folder() const { return folder_->toolTip(); }

void DownloadPage::pasteLink()
{
    link_->setText(QGuiApplication::clipboard()->text().trimmed());
    link_->setFocus();
}

void DownloadPage::chooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Save downloads to", folder_->toolTip());
    if (dir.isEmpty())
        return;
    folder_->setText(shortPath(dir));
    folder_->setToolTip(dir);
}

void DownloadPage::openFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder_->toolTip()));
}

void DownloadPage::log(const QString &line, int number)
{
    const QString time = QTime::currentTime().toString("HH:mm:ss");
    log_->appendPlainText(number ? QString("%1 #%2  %3").arg(time).arg(number).arg(line)
                                 : QString("%1  %2").arg(time, line));
}

// The banner says what yt-dlp is up to once nothing more pressing is shown.
void DownloadPage::engineChanged()
{
    if (engine_->busy())
        return;   // statusChanged shows the progress
    if (engine_->version().isEmpty())
        banner_->setNotice(Banner::Warning, "yt-dlp isn't available yet. Check your connection; QFisher will try again.");
    else
        banner_->setNotice(Banner::Info, "yt-dlp " + engine_->version()
                           + " is ready. Only the sites you paste links from are contacted.");
    if (isWebLink(link_->text()) && previewTitle_.isEmpty() && !preview_)
        startPreview();   // a preview was waiting for yt-dlp
    startNext();
}

void DownloadPage::linkChanged(const QString &text)
{
    const bool ok = isWebLink(text);
    download_->setEnabled(ok);
    openPage_->setEnabled(ok);
    play_->setEnabled(false);
    delete preview_;   // a new link makes the old answer useless
    delete clip_;
    playWhenReady_ = false;
    previewTitle_.clear();
    previewSeconds_ = -1;
    previewDelay_.stop();
    if (ok) {
        chip_->setText(QUrl(text.trimmed()).host());
        title_->setText(QUrl(text.trimmed()).host());
        meta_->setText("Loading preview...");
        thumbnail_->setHint("Loading preview...");
        previewDelay_.start();
    } else {
        chip_->setText("No link yet");
        title_->setText("No video yet");
        meta_->setText(text.trimmed().isEmpty() ? "Paste a link above, then choose a format on the left."
                                                : "That doesn't look like a web link.");
        thumbnail_->setHint("Paste a link to see a preview");
    }
}

void DownloadPage::startPreview()
{
    const QString url = link_->text().trimmed();
    if (!isWebLink(url))
        return;
    if (engine_->busy()) {
        meta_->setText("Waiting for yt-dlp...");
        return;   // engineChanged() retries
    }
    delete preview_;
    preview_ = new PreviewJob(url, playlist_->isChecked(), engine_, this);
    connect(preview_, &PreviewJob::logLine, this, [this](const QString &line) { log(line); });
    connect(preview_, &PreviewJob::ready, this, [this, job = preview_.data()](const PreviewInfo &info) {
        if (!job->cookies().isEmpty()) {
            signInUrl_ = job->url();
            signInCookies_ = job->cookies();
            banner_->setNotice(Banner::Info, "This site wanted a sign-in, so QFisher is using your browser's.");
        }
        job->deleteLater();
        showPreview(info);
    });
    connect(preview_, &PreviewJob::failed, this, [this, job = preview_.data()](const QString &message) {
        job->deleteLater();
        meta_->setText(message);
        thumbnail_->setHint("No preview");
        banner_->setNotice(Banner::Warning, message);
    });
    preview_->start();
}

void DownloadPage::showPreview(const PreviewInfo &info)
{
    previewTitle_ = info.title;
    previewSeconds_ = info.seconds;
    const bool clipOk = info.playlistCount < 0 && !info.live && info.seconds > 0;
    play_->setEnabled(clipOk);
    chip_->setText(info.playlistCount >= 0 ? QString("%1 (%2)").arg(info.title).arg(info.playlistCount) : info.title);
    title_->setText(info.title);
    QStringList meta;
    if (!info.channel.isEmpty())
        meta << info.channel;
    if (info.playlistCount >= 0)
        meta << QString("Playlist, %1 videos").arg(info.playlistCount);
    else if (info.live)
        meta << "Live now";
    else if (info.seconds >= 0)
        meta << clock(info.seconds);
    if (!info.site.isEmpty())
        meta << info.site;
    meta_->setText(meta.join("  ·  "));

    const QString badge = info.playlistCount >= 0 ? QString("%1 videos").arg(info.playlistCount)
                        : info.live ? QString("LIVE") : info.seconds >= 0 ? clock(info.seconds) : QString();
    if (info.thumbnail.isEmpty()) {
        thumbnail_->setHint("No picture");
        thumbnail_->setClipAvailable(clipOk);
        if (clipOk)
            fetchClip(false);
        return;
    }
    thumbnail_->setHint("Loading picture...");
    QNetworkRequest req{QUrl(info.thumbnail)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(20000);
    QNetworkReply *reply = net_->get(req);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64 total) {
        if (got > (8 << 20) || total > (8 << 20))
            reply->abort();   // a thumbnail is never this big
    });
    const QString url = link_->text().trimmed();
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, badge, clipOk] {
        reply->deleteLater();
        if (link_->text().trimmed() != url)
            return;   // the link changed while the picture loaded
        QPixmap picture;
        if (reply->error() || !picture.loadFromData(reply->readAll()))
            thumbnail_->setHint("No picture");
        else
            thumbnail_->setPicture(picture, badge);
        thumbnail_->setClipAvailable(clipOk);
    });
    if (clipOk)
        fetchClip(false);   // in the background, so a click can play it straight away
}

// Click: play the clip (or stop it). It's usually fetched already, see showPreview().
void DownloadPage::toggleClip()
{
    if (thumbnail_->playing()) {
        thumbnail_->stopClip();
        return;
    }
    if (play_->isEnabled())
        fetchClip(true);
}

// Gets the clip for the current link; plays it once it's there if `play` (or a click while
// loading) asks for it. Started as soon as the preview shows, so clicking rarely waits.
void DownloadPage::fetchClip(bool play)
{
    const QString url = link_->text().trimmed();
    if (QFile::exists(clips_.value(url))) {
        if (play)
            thumbnail_->playClip(clips_.value(url));
        return;
    }
    playWhenReady_ = playWhenReady_ || play;
    if (playWhenReady_)
        thumbnail_->setLoadingClip("Loading clip...");
    if (clip_)
        return;   // already on its way
    // Start a little way into longer videos, past intros and black frames.
    const int start = previewSeconds_ > 60 ? qMin(previewSeconds_ / 10, 60) : 0;
    clip_ = new ClipJob(url, start, clipDir_.path(), url == signInUrl_ ? signInCookies_ : QString(), engine_, this);
    connect(clip_, &ClipJob::logLine, this, [this](const QString &line) { log(line); });
    connect(clip_, &ClipJob::ready, this, [this, job = clip_.data()](const QString &file) {
        clips_.insert(job->url(), file);
        job->deleteLater();
        if (link_->text().trimmed() == job->url() && std::exchange(playWhenReady_, false))
            thumbnail_->playClip(file);
    });
    connect(clip_, &ClipJob::failed, this, [this, job = clip_.data()](const QString &message) {
        job->deleteLater();
        if (!std::exchange(playWhenReady_, false))
            return;   // a background fetch failing isn't worth a message until someone clicks
        thumbnail_->setLoadingClip(message);
        QTimer::singleShot(3000, thumbnail_, [this] { thumbnail_->setLoadingClip({}); });
    });
    clip_->start();
}

Options DownloadPage::currentOptions() const
{
    Options o;
    o.url = link_->text().trimmed();
    o.folder = folder_->toolTip();
    o.format = Options::Format(qMax(0, format_->currentRow()));
    o.maxHeight = heights[qMax(0, quality_->currentRow())];
    o.subtitles = subtitles_->isChecked();
    o.playlist = playlist_->isChecked();
    if (o.url == signInUrl_)
        o.cookies = signInCookies_;   // the preview already found a sign-in is needed
    return o;
}

void DownloadPage::download()
{
    const Options o = currentOptions();
    const QString name = previewTitle_.isEmpty() ? QUrl(o.url).host() : previewTitle_;
    auto *item = new QTreeWidgetItem(list_, {name, o.label()});
    item->setIcon(Name, QIcon(o.audioOnly() ? ":/icons/audio.png" : ":/icons/video.png"));
    item->setToolTip(Name, o.url);
    addJob(item, o);
    list_->scrollToItem(item);
    link_->clear();
    startNext();
}

void DownloadPage::addJob(QTreeWidgetItem *item, const Options &o)
{
    const int number = list_->indexOfTopLevelItem(item) + 1;
    item->setData(Progress, Qt::UserRole, -1.0);
    setState(item, State::Waiting, "Waiting");

    auto *job = new DownloadJob(o, engine_, this);
    delete jobs_.take(item);
    jobs_.insert(item, job);
    connect(job, &DownloadJob::logLine, this, [this, number](const QString &line) { log(line, number); });
    connect(job, &DownloadJob::titleKnown, this, [item](const QString &t) { item->setText(Name, t); });
    connect(job, &DownloadJob::stage, this, [item](const QString &s) { item->setText(Status, s + "..."); });
    connect(job, &DownloadJob::progress, this, [this, item](double fraction, qint64 total, double speed, int left) {
        item->setData(Progress, Qt::UserRole, fraction);
        if (stateOf(item) != State::Running)
            setState(item, State::Running, {});
        item->setText(Status, "Downloading");
        if (total > 0)
            item->setText(Size, bytes(total));
        item->setText(Speed, speed > 0 ? bytes(speed) + "/s" : QString());
        item->setData(Speed, Qt::UserRole, speed);
        item->setText(Time, left >= 0 ? clock(left) : QString());
        reportActivity();
    });
    connect(job, &DownloadJob::finished, this, [this, item, job, number](bool ok, const QString &message) {
        const bool stopped = message == "Stopped";
        setState(item, ok ? State::Done : stopped ? State::Stopped : State::Failed, ok ? "Complete" : message);
        log(ok ? "Finished: " + job->file() : "Stopped: " + message, number);
        if (ok)
            library_->add({item->text(Name), job->options().url, job->file(), job->options().label(),
                           QFileInfo(job->file()).size(), QDateTime::currentDateTime()});
        if (running_ == job)
            running_ = nullptr;
        refreshButtons();
        startNext();
    });
}

// Downloads run one at a time, top to bottom, and wait while yt-dlp is being installed or updated.
void DownloadPage::startNext()
{
    if (!running_ && !engine_->busy()) {
        for (int i = 0; i < list_->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = list_->topLevelItem(i);
            if (stateOf(item) != State::Waiting || !jobs_.value(item))
                continue;
            running_ = jobs_.value(item);
            setState(item, State::Running, "Starting...");
            running_->start();
            break;
        }
        if (!running_)
            engine_->checkDaily();   // idle: a good moment for the once-a-day update check
    }
    refreshButtons();
    reportActivity();
}

void DownloadPage::removeSelected()
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

void DownloadPage::retrySelected()
{
    for (QTreeWidgetItem *item : list_->selectedItems())
        if (const State s = stateOf(item); s == State::Failed || s == State::Stopped)
            addJob(item, jobs_.value(item)->options());
    startNext();
}

void DownloadPage::stopSelected()
{
    for (QTreeWidgetItem *item : list_->selectedItems()) {
        if (stateOf(item) == State::Running)
            jobs_.value(item)->cancel();   // finished() marks it Stopped
        else if (stateOf(item) == State::Waiting)
            setState(item, State::Stopped, "Stopped");
    }
    refreshButtons();
}

void DownloadPage::launchSelected()
{
    for (QTreeWidgetItem *item : list_->selectedItems())
        if (stateOf(item) == State::Done && QFile::exists(jobs_.value(item)->file()))
            QDesktopServices::openUrl(QUrl::fromLocalFile(jobs_.value(item)->file()));
}

void DownloadPage::clearFinished()
{
    for (int i = list_->topLevelItemCount() - 1; i >= 0; --i) {
        QTreeWidgetItem *item = list_->topLevelItem(i);
        if (stateOf(item) == State::Done) {
            delete jobs_.take(item);
            delete item;
        }
    }
    refreshButtons();
}

// Buttons only light up when they'd do something for the selected rows.
void DownloadPage::refreshButtons()
{
    bool any = false, failed = false, active = false, done = false, finishedAny = false;
    for (QTreeWidgetItem *item : list_->selectedItems()) {
        const State s = stateOf(item);
        any = true;
        failed |= s == State::Failed || s == State::Stopped;
        active |= s == State::Running || s == State::Waiting;
        done |= s == State::Done;
    }
    for (int i = 0; i < list_->topLevelItemCount(); ++i)
        finishedAny |= stateOf(list_->topLevelItem(i)) == State::Done;
    remove_->setEnabled(any);
    retry_->setEnabled(failed);
    stop_->setEnabled(active);
    launch_->setEnabled(done);
    clear_->setEnabled(finishedAny);
}

void DownloadPage::reportActivity()
{
    int downloading = 0, waiting = 0;
    double speed = 0;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const State s = stateOf(list_->topLevelItem(i));
        downloading += s == State::Running;
        waiting += s == State::Waiting;
    }
    for (int i = 0; i < list_->topLevelItemCount(); ++i)
        if (stateOf(list_->topLevelItem(i)) == State::Running)
            speed += list_->topLevelItem(i)->data(Speed, Qt::UserRole).toDouble();
    emit activityChanged(downloading, waiting, speed);
}
