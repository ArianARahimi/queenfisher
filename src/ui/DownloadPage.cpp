#include "DownloadPage.h"
#include "ProgressDelegate.h"
#include "Theme.h"
#include "Thumbnail.h"
#include "Widgets.h"
#include "core/DownloadJob.h"
#include "core/Ffmpeg.h"
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
#include <QSettings>
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

DownloadPage::DownloadPage(YtDlpEngine *engine, Library *library, Ffmpeg *ffmpeg, QWidget *parent)
    : QWidget(parent), engine_(engine), library_(library), ffmpeg_(ffmpeg)
{
    setObjectName("page");
    net_ = new QNetworkAccessManager(this);

    auto *split = new QSplitter(Qt::Vertical);
    split->addWidget(buildTop());
    split->addWidget(buildDownloads());
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    split->setChildrenCollapsible(false);
    split_ = split;

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(buildSidebar());
    layout->addWidget(split, 1);

    previewDelay_.setSingleShot(true);
    previewDelay_.setInterval(500);
    connect(&previewDelay_, &QTimer::timeout, this, &DownloadPage::startPreview);
    engineRetry_.setSingleShot(true);
    engineRetry_.setInterval(65000);   // safely past the engine's own minute between automatic checks
    connect(&engineRetry_, &QTimer::timeout, this, &DownloadPage::startNext);
    connect(engine_, &Engine::readyChanged, this, &DownloadPage::engineChanged);
    connect(engine_, &Engine::statusChanged, this, [this](const QString &text) {
        if (engine_->busy())
            banner_->setNotice(text);
    });
    connect(engine_, &Engine::logLine, this, [this](const QString &line) { log(line); });
    if (ffmpeg_) {
        connect(ffmpeg_, &Ffmpeg::statusChanged, this, [this](const QString &text) {
            if (ffmpeg_->busy() && !engine_->busy())
                banner_->setNotice(text);
        });
        connect(ffmpeg_, &Ffmpeg::logLine, this, [this](const QString &line) { log(line); });
        connect(ffmpeg_, &Ffmpeg::readyChanged, this, &DownloadPage::showNotice);
        connect(ffmpeg_, &Ffmpeg::readyChanged, this, &DownloadPage::startNext);
    }

    linkChanged({});
    restoreSettings();
    engineChanged();
}

// Everything picked last time comes back: format, quality, folder, extras and the split
// between preview and downloads (the log always starts closed). Saved the moment it changes.
void DownloadPage::restoreSettings()
{
    QSettings s;
    const int format = s.value("format", int(Options::Mp4)).toInt();
    if (format >= 0 && format < format_->count())
        format_->setCurrentRow(format);
    const int quality = s.value("quality", 0).toInt();   // Best available
    if (quality >= 0 && quality < quality_->count())
        quality_->setCurrentRow(quality);
    const QString folder = s.value("folder").toString();
    if (!folder.isEmpty() && QFileInfo(folder).isDir()) {   // a folder that's gone falls back to Videos
        folder_->setText(shortPath(folder));
        folder_->setToolTip(folder);
    }
    subtitles_->setChecked(s.value("subtitles", false).toBool());
    playlist_->setChecked(s.value("playlist", false).toBool());
    split_->restoreState(s.value("split").toByteArray());

    connect(format_, &QListWidget::currentRowChanged, this, [](int row) { QSettings().setValue("format", row); });
    connect(quality_, &QListWidget::currentRowChanged, this, [](int row) { QSettings().setValue("quality", row); });
    connect(subtitles_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("subtitles", on); });
    connect(playlist_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("playlist", on); });
    connect(split_, &QSplitter::splitterMoved, this, [this] { QSettings().setValue("split", split_->saveState()); });
}

// Closing with downloads running: they stop and clean up first, while the log their last
// lines go to still exists (Qt would otherwise delete them after the widgets).
DownloadPage::~DownloadPage()
{
    for (DownloadJob *job : std::as_const(jobs_)) {
        disconnect(job, nullptr, this, nullptr);
        delete job;
    }
}

// Option boxes on the left: what to save and where.
QWidget *DownloadPage::buildSidebar()
{
    format_ = new QListWidget;
    const struct { const char *icon, *text; } formats[] = {
        {"video", "MP4 (plays everywhere)"}, {"video", "MKV"}, {"video", "WebM"},
        {"audio", "MP3"}, {"audio", "Audio (original)"}};
    for (const auto &f : formats)
        new QListWidgetItem(Widgets::formatIcon(QString(f.icon) == "audio"), f.text, format_);
    format_->setCurrentRow(Options::Mp4);

    quality_ = new QListWidget;
    quality_->addItems({"Best available", "2160p (4K)", "1440p", "1080p", "720p", "480p"});
    quality_->setCurrentRow(0);   // Best available
    // Quality is exactly as tall as its rows; Format takes the rest, with room for more formats.
    quality_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    quality_->setFixedHeight(quality_->sizeHintForRow(0) * quality_->count() + 2 * quality_->frameWidth() + 2);
    format_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    for (QListWidget *l : {format_, quality_})
        Widgets::pickOnClick(l);

    auto *formatBox = new QVBoxLayout;
    formatBox->addWidget(format_);
    auto *qualityBox = new QVBoxLayout;
    qualityBox->addWidget(quality_);

    auto *openFolder = new QPushButton("Open Download Folder");
    auto *side = new QWidget;
    side->setFixedWidth(210);
    auto *layout = new QVBoxLayout(side);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    auto *title = new QLabel("Download Options:");
    title->setObjectName("sideTitle");
    layout->addWidget(title);
    qualityBox_ = Widgets::sideBox("Quality", qualityBox);
    layout->addWidget(Widgets::sideBox("Format", formatBox), 1);
    layout->addWidget(qualityBox_);
    layout->addWidget(openFolder);

    // Audio has no picture size to choose, and no subtitles: the whole Quality box greys out.
    connect(format_, &QListWidget::currentRowChanged, this, [this](int row) {
        qualityBox_->setEnabled(row < Options::Mp3);
        subtitles_->setEnabled(row < Options::Mp3);
    });
    connect(format_, &QListWidget::currentRowChanged, this, &DownloadPage::showQuality);
    connect(quality_, &QListWidget::currentRowChanged, this, &DownloadPage::showQuality);
    connect(openFolder, &QPushButton::clicked, this, &DownloadPage::openFolder);
    return side;
}

// Link box, the notice bar, the preview and its actions.
QWidget *DownloadPage::buildTop()
{
    link_ = new QLineEdit;
    link_->setPlaceholderText("Paste a video link (YouTube, Vimeo, Twitch, and more than a thousand other sites)");
    link_->setClearButtonEnabled(true);
    link_->setObjectName("link");
    QPalette hint = link_->palette();   // a readable placeholder, so the empty box is easy to spot
    hint.setColor(QPalette::PlaceholderText, Theme::dim);
    link_->setPalette(hint);
    auto *paste = new QPushButton(QIcon(":/icons/paste.png"), "Paste");
    paste->setToolTip("Paste the link you copied");

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

    // Where it goes and the extras, above the title, in two compact lines.
    const QString movies = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    folder_ = new QLineEdit(shortPath(movies));
    folder_->setToolTip(movies);
    folder_->setReadOnly(true);
    auto *change = new QPushButton("Change...");
    auto *saveLabel = new QLabel("Save to");
    saveLabel->setObjectName("meta");
    auto *saveRow = new QHBoxLayout;
    saveRow->setSpacing(6);
    saveRow->addWidget(saveLabel);
    saveRow->addWidget(folder_, 1);
    saveRow->addWidget(change);

    subtitles_ = new QCheckBox("Subtitles");
    subtitles_->setToolTip("Add English subtitles to the video when the site has them");
    playlist_ = new QCheckBox("Whole playlist");
    playlist_->setToolTip("When the link is part of a playlist, download every video in it");
    auto *extrasRow = new QHBoxLayout;
    extrasRow->setSpacing(16);
    extrasRow->addWidget(subtitles_);
    extrasRow->addWidget(playlist_);
    extrasRow->addStretch(1);

    // What the video really offers, and roughly how big the download will be.
    qualityInfo_ = new QLabel;
    qualityInfo_->setObjectName("qualityInfo");
    qualityInfo_->setVisible(false);

    auto *info = new QVBoxLayout;
    info->setSpacing(6);
    info->addLayout(saveRow);
    info->addLayout(extrasRow);
    info->addSpacing(8);
    info->addWidget(title_);
    info->addWidget(meta_);
    info->addSpacing(4);
    info->addWidget(qualityInfo_);
    info->addStretch(1);
    auto *previewRow = new QHBoxLayout;
    previewRow->setSpacing(12);
    previewRow->addWidget(thumbnail_, 3);   // the picture grows with the window
    previewRow->addLayout(info, 2);

    // Download right next to Paste: paste, then one short move to download.
    download_ = new QPushButton(Theme::recolour(":/icons/download.png", Theme::accent, false), "Download");
    download_->setObjectName("primary");
    download_->setToolTip("Download this link with the options on the left");

    auto *linkRow = new QHBoxLayout;
    linkRow->setSpacing(6);
    linkRow->addWidget(link_, 1);
    linkRow->addWidget(paste);
    linkRow->addWidget(download_);
    for (QPushButton *b : {paste, download_})   // as tall as the link box
        b->setMinimumHeight(link_->sizeHint().height());

    // Link row on top of the pane, a notice under it only when there's something to say.
    auto *pane = new QFrame;
    pane->setObjectName("previewPane");
    auto *paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(8, 8, 8, 8);
    paneLayout->setSpacing(8);
    paneLayout->addLayout(linkRow);
    paneLayout->addWidget(banner_);
    paneLayout->addLayout(previewRow, 1);

    auto *top = new QWidget;
    auto *layout = new QVBoxLayout(top);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(pane, 1);

    connect(change, &QPushButton::clicked, this, &DownloadPage::chooseFolder);
    connect(playlist_, &QCheckBox::toggled, this, [this] { if (isWebLink(link_->text())) previewDelay_.start(); });
    connect(link_, &QLineEdit::textChanged, this, &DownloadPage::linkChanged);
    connect(link_, &QLineEdit::returnPressed, this, [this] { if (download_->isEnabled()) download(); });
    connect(paste, &QPushButton::clicked, this, &DownloadPage::pasteLink);
    connect(download_, &QPushButton::clicked, this, &DownloadPage::download);
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
    clearAll_ = Widgets::action("trash", "Clear", "Take everything that isn't downloading or waiting off the list");
    clear_ = Widgets::action("clear", "Clear Finished", "Take finished downloads off the list");

    logAction_ = new QAction(QIcon(":/icons/log.png"), "Log", this);
    logAction_->setCheckable(true);
    logAction_->setToolTip(logButton->toolTip());
    logButton->setDefaultAction(logAction_);

    log_ = new QPlainTextEdit;
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
    layout->addLayout(actionRow({remove_, retry_, stop_, launch_, logButton, explore, clearAll_, clear_}));
    layout->addWidget(log_);

    connect(list_, &QTreeWidget::itemSelectionChanged, this, &DownloadPage::refreshButtons);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, &DownloadPage::launchSelected);
    connect(remove_, &QToolButton::clicked, this, &DownloadPage::removeSelected);
    connect(retry_, &QToolButton::clicked, this, &DownloadPage::retrySelected);
    connect(stop_, &QToolButton::clicked, this, &DownloadPage::stopSelected);
    connect(launch_, &QToolButton::clicked, this, &DownloadPage::launchSelected);
    connect(explore, &QToolButton::clicked, this, &DownloadPage::openFolder);
    connect(clearAll_, &QToolButton::clicked, this, &DownloadPage::clearAll);
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
    QSettings().setValue("folder", dir);
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

// The notice bar: yt-dlp or ffmpeg being fetched (their own messages), or yt-dlp missing.
// Missing tools get a popup when the window opens instead (MainWindow).
void DownloadPage::engineChanged()
{
    if (engine_->busy())
        return;   // statusChanged shows the progress
    showNotice();
    if (isWebLink(link_->text()) && previewTitle_.isEmpty() && !preview_)
        startPreview();   // a preview was waiting for yt-dlp
    startNext();
}

void DownloadPage::linkChanged(const QString &text)
{
    const bool ok = isWebLink(text);
    download_->setEnabled(ok);
    lastPreview_ = {};
    showQuality();
    delete preview_;   // a new link makes the old answer useless
    previewTitle_.clear();
    previewDelay_.stop();
    if (ok) {
        title_->setText(QUrl(text.trimmed()).host());
        meta_->setText("Loading preview...");
        thumbnail_->setHint("Loading preview...");
        previewDelay_.start();
    } else {
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
            banner_->setNotice("This site wanted a sign-in, so Queenfisher is using your browser's.");
        }
        job->deleteLater();
        showPreview(info);
    });
    connect(preview_, &PreviewJob::failed, this, [this, job = preview_.data()](const QString &message) {
        job->deleteLater();
        meta_->setText(message);
        thumbnail_->setHint("No preview");
        banner_->setNotice(message);
    });
    preview_->start();
}

void DownloadPage::showPreview(const PreviewInfo &info)
{
    previewTitle_ = info.title;
    lastPreview_ = info;
    showQuality();
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
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, badge] {
        reply->deleteLater();
        if (link_->text().trimmed() != url)
            return;   // the link changed while the picture loaded
        QPixmap picture;
        if (reply->error() || !picture.loadFromData(reply->readAll()))
            thumbnail_->setHint("No picture");
        else
            thumbnail_->setPicture(picture, badge);
    });
}

void DownloadPage::showNotice()
{
    if (engine_->busy() || (ffmpeg_ && ffmpeg_->busy()))
        return;   // they show their own progress
    const bool noYtDlp = engine_->version().isEmpty();
    const bool ffmpegFailed = ffmpeg_ && !ffmpeg_->lastError().isEmpty();
    banner_->setNotice(noYtDlp ? QString("yt-dlp isn't available yet. Check your connection; Queenfisher will try again.")
                               : QString());
    if (noYtDlp || ffmpegFailed)
        engineRetry_.start();   // startNext() asks again when nothing is downloading
    else
        engineRetry_.stop();
}

// "Up to 1080p · about 51 MB" for the current choices; quality rows the video doesn't have
// are greyed out. Nothing for playlists or before a preview.
void DownloadPage::showQuality()
{
    const auto format = Options::Format(qMax(0, format_->currentRow()));
    const int best = bestHeight(lastPreview_, format);
    for (int row = 1; row < quality_->count(); ++row) {   // "Best available" always applies
        QListWidgetItem *item = quality_->item(row);
        const bool offered = !best || heights[row] <= best;
        item->setFlags(offered ? item->flags() | Qt::ItemIsEnabled : item->flags() & ~Qt::ItemIsEnabled);
    }
    if (lastPreview_.streams.isEmpty()) {
        qualityInfo_->setVisible(false);
        return;
    }
    const bool audio = format == Options::Mp3 || format == Options::AudioOriginal;
    QStringList parts;
    if (best)
        parts << QString("Up to %1p").arg(best);
    if (const qint64 bytes = estimateSize(lastPreview_, format, audio ? 0 : heights[qMax(0, quality_->currentRow())]); bytes > 0)
        parts << "about " + Widgets::sizeText(double(bytes));
    qualityInfo_->setText(parts.join("  ·  "));
    qualityInfo_->setVisible(!parts.isEmpty());
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
    if (ffmpeg_)
        o.ffmpeg = ffmpeg_->location();
    return o;
}

void DownloadPage::download()
{
    const Options o = currentOptions();
    const QString name = previewTitle_.isEmpty() ? QUrl(o.url).host() : previewTitle_;
    auto *item = new QTreeWidgetItem(list_, {name, o.label()});
    item->setIcon(Name, Widgets::formatIcon(o.audioOnly()));
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
            item->setText(Size, Widgets::sizeText(total));
        item->setText(Speed, speed > 0 ? Widgets::sizeText(speed) + "/s" : QString());
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

// Downloads run one at a time, top to bottom, and wait while yt-dlp or ffmpeg is being fetched.
void DownloadPage::startNext()
{
    if (!running_ && !engine_->busy() && !(ffmpeg_ && ffmpeg_->busy())) {
        for (int i = 0; i < list_->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = list_->topLevelItem(i);
            if (stateOf(item) != State::Waiting || !jobs_.value(item))
                continue;
            running_ = jobs_.value(item);
            setState(item, State::Running, "Starting...");
            running_->start();
            break;
        }
        if (!running_) {
            engine_->checkDaily();   // idle: a good moment for the once-a-day update check
            if (ffmpeg_)
                ffmpeg_->ensure();   // Windows: fetches ffmpeg the first time
        }
    }
    refreshButtons();
    reportActivity();
}

void DownloadPage::removeRow(QTreeWidgetItem *item)
{
    DownloadJob *job = jobs_.take(item);
    if (job == running_)
        running_ = nullptr;
    if (job && (stateOf(item) == State::Failed || stateOf(item) == State::Stopped))
        job->discardPartial();   // a stopped or failed download leaves nothing behind
    delete job;                  // a running one is stopped first, then cleans up after itself
    delete item;
}

void DownloadPage::removeSelected()
{
    for (QTreeWidgetItem *item : list_->selectedItems())
        removeRow(item);
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
        if (stateOf(item) == State::Done)
            removeRow(item);
    }
    refreshButtons();
}

// Finished, failed and stopped rows go; downloads still running or waiting stay.
void DownloadPage::clearAll()
{
    for (int i = list_->topLevelItemCount() - 1; i >= 0; --i) {
        QTreeWidgetItem *item = list_->topLevelItem(i);
        if (const State s = stateOf(item); s != State::Running && s != State::Waiting)
            removeRow(item);
    }
    refreshButtons();
}

// Buttons only light up when they'd do something for the selected rows.
void DownloadPage::refreshButtons()
{
    bool any = false, failed = false, active = false, done = false, finishedAny = false, idleAny = false;
    for (QTreeWidgetItem *item : list_->selectedItems()) {
        const State s = stateOf(item);
        any = true;
        failed |= s == State::Failed || s == State::Stopped;
        active |= s == State::Running || s == State::Waiting;
        done |= s == State::Done;
    }
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const State s = stateOf(list_->topLevelItem(i));
        finishedAny |= s == State::Done;
        idleAny |= s != State::Running && s != State::Waiting;
    }
    remove_->setEnabled(any);
    clearAll_->setEnabled(idleAny);
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
        const QTreeWidgetItem *item = list_->topLevelItem(i);
        const State s = stateOf(item);
        waiting += s == State::Waiting;
        if (s == State::Running) {
            ++downloading;
            speed += item->data(Speed, Qt::UserRole).toDouble();
        }
    }
    emit activityChanged(downloading, waiting, speed);
}
