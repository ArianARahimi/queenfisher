#include "DownloadPage.h"
#include "AppSettings.h"
#include "ProgressDelegate.h"
#include "Theme.h"
#include "Thumbnail.h"
#include "Widgets.h"
#include "core/DownloadJob.h"
#include "core/Deno.h"
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
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardPaths>
#include <QTime>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#pragma region Building the page
namespace {

enum Column { Name, Format, Size, Status, Progress, Speed, Time };
enum class State { Waiting, Running, Done, Failed, Stopped };
constexpr int stateRole = Qt::UserRole + 10;   // State of a row, on the Name column
constexpr int heights[] = {0, 2160, 1440, 1080, 720, 480};   // matches the Quality list
constexpr int gifSeconds = 60;   // longest video offered as a GIF

bool isWebLink(const QString &text)
{
    const QUrl url(text.trimmed());
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty();
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

DownloadPage::DownloadPage(YtDlpEngine *engine, Library *library, Ffmpeg *ffmpeg, Deno *deno, QWidget *parent)
    : QWidget(parent), engine_(engine), library_(library), ffmpeg_(ffmpeg), deno_(deno)
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
    previewDelay_.setInterval(250);
    connect(&previewDelay_, &QTimer::timeout, this, &DownloadPage::startPreview);
    engineRetry_.setSingleShot(true);
    engineRetry_.setInterval(65000);   // safely past the engine's own minute between automatic checks
    connect(&engineRetry_, &QTimer::timeout, this, &DownloadPage::startNext);
    connect(&AppSettings::get(), &AppSettings::changed, this, &DownloadPage::startNext);   // more at once: start them
    connect(engine_, &Engine::readyChanged, this, &DownloadPage::engineChanged);
    connect(engine_, &Engine::statusChanged, this, [this](const QString &text) {
        if (engine_->busy())
            banner_->setNotice(text);
    });
    connect(engine_, &Engine::logLine, this, [this](const QString &line) { log(line); });
    for (FetchedTool *tool : {static_cast<FetchedTool *>(ffmpeg_), static_cast<FetchedTool *>(deno_)}) {
        if (!tool)
            continue;
        connect(tool, &FetchedTool::statusChanged, this, [this, tool](const QString &text) {
            if (tool->busy() && !engine_->busy())
                banner_->setNotice(text);
        });
        connect(tool, &FetchedTool::logLine, this, [this](const QString &line) { log(line); });
        connect(tool, &FetchedTool::readyChanged, this, &DownloadPage::engineChanged);   // waiting work goes on
    }

    linkChanged({});
    restoreSettings();
    askOverwrite = [this](const QString &file) {
        QMessageBox box(QMessageBox::Question, "Already downloaded",
                        QString("\"%1\" is already in %2.").arg(QFileInfo(file).fileName(), Widgets::shortPath(QFileInfo(file).path())),
                        QMessageBox::NoButton, window());
        QPushButton *replace = box.addButton("Overwrite", QMessageBox::AcceptRole);
        box.setDefaultButton(box.addButton("Keep", QMessageBox::RejectRole));   // Enter and Esc keep it
        box.setEscapeButton(box.defaultButton());
        box.exec();
        return box.clickedButton() == replace;
    };
    askSignIn = [this](const QString &site) {
        QMessageBox box(QMessageBox::Question, "Browser sign-in", site + " wants a sign-in.", QMessageBox::NoButton, window());
        box.setTextFormat(Qt::PlainText);
        box.setInformativeText("Use your default browser's sign-in for " + site
                               + "? Queenfisher remembers this; turning browser sign-in off in Settings forgets it.");
        QPushButton *allow = box.addButton("Use for This Site", QMessageBox::AcceptRole);
        box.setDefaultButton(box.addButton("Not Now", QMessageBox::RejectRole));   // Enter and Esc say no
        box.setEscapeButton(box.defaultButton());
        box.exec();
        return box.clickedButton() == allow;
    };
    engineChanged();
}

// Everything picked last time comes back: format, quality, folder, extras and the split
// between preview and downloads (the log always starts closed). Saved the moment it changes.
void DownloadPage::restoreSettings()
{
    QSettings s;
    // Saved by name; older versions saved the row of a five-format list.
    const QVariant saved = s.value("format", "mp4");
    bool oldRow = false;
    const int row = saved.toInt(&oldRow);
    const Options::Format oldFormats[] = {Options::Mp4, Options::Mkv, Options::Webm, Options::Mp3, Options::AudioOriginal};
    selectFormat(oldRow ? (row >= 0 && row < 5 ? oldFormats[row] : Options::Mp4) : Options::fromKey(saved.toString()));
    const int quality = s.value("quality", 0).toInt();   // Best available
    if (quality >= 0 && quality < quality_->count())
        quality_->setCurrentRow(quality);
    const QString folder = s.value("folder").toString();
    if (!folder.isEmpty() && QFileInfo(folder).isDir()) {   // a folder that's gone falls back to Downloads
        folder_->setText(Widgets::shortPath(folder));
        folder_->setToolTip(folder);
    }
    subtitles_->setChecked(s.value("subtitles", false).toBool());
    playlist_->setChecked(s.value("playlist", false).toBool());
    sponsors_->setChecked(s.value("sponsors", false).toBool());
    split_->restoreState(s.value("split").toByteArray());

    connect(format_, &QListWidget::currentRowChanged, this, [this] { QSettings().setValue("format", Options::key(currentFormat())); });
    connect(quality_, &QListWidget::currentRowChanged, this, [](int row) { QSettings().setValue("quality", row); });
    connect(subtitles_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("subtitles", on); });
    connect(playlist_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("playlist", on); });
    connect(sponsors_, &QCheckBox::toggled, this, [](bool on) { QSettings().setValue("sponsors", on); });
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
    Widgets::addHeading(format_, "Video");
    for (int i = 0; i < Options::FormatCount; ++i) {   // each row knows its format; video, a line, sound
        const auto f = Options::Format(i);
        if (f == Options::Mp3) {
            Widgets::addDivider(format_);
            Widgets::addHeading(format_, "Audio");
        }
        auto *item = new QListWidgetItem(Widgets::formatIcon(Options::isAudio(f)), Options::name(f), format_);
        item->setData(Qt::UserRole, i);
    }
    formatItem(Options::Gif)->setToolTip("For clips up to a minute: 480 px wide, no sound");
    selectFormat(Options::Mp4);

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

    auto *openFolder = new QPushButton("Open Downloads Directory");
    auto *side = new QWidget;
    side->setFixedWidth(210);
    auto *layout = new QVBoxLayout(side);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    qualityBox_ = Widgets::sideBox("Quality", qualityBox);
    layout->addWidget(Widgets::sideBox("Format", formatBox), 1);
    layout->addWidget(qualityBox_);
    layout->addWidget(openFolder);

    // Audio and GIF have no picture size to choose (the Quality box greys out); subtitles only
    // go into MP4, MKV and WebM.
    connect(format_, &QListWidget::currentRowChanged, this, [this] {
        const Options::Format f = currentFormat();
        qualityBox_->setEnabled(Options::hasQuality(f));
        subtitles_->setEnabled(Options::takesSubtitles(f));
    });
    connect(format_, &QListWidget::currentRowChanged, this, &DownloadPage::showQuality);
    connect(quality_, &QListWidget::currentRowChanged, this, [this](int row) { wantedQuality_ = qMax(0, row); });
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
    // Titles and channel names come from the site: plain text, so markup in them shows as typed.
    title_ = new QLabel;
    title_->setTextFormat(Qt::PlainText);
    title_->setWordWrap(true);
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.3);
    big.setBold(true);
    title_->setFont(big);
    meta_ = new QLabel;
    meta_->setTextFormat(Qt::PlainText);
    meta_->setObjectName("meta");
    meta_->setWordWrap(true);

    // Where it goes and the extras, above the title, in two compact lines.
    const QString downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    folder_ = new QLineEdit(Widgets::shortPath(downloads));
    folder_->setToolTip(downloads);
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
    sponsors_ = new QCheckBox("Skip sponsors");
    sponsors_->setToolTip("Cut sponsored segments out of YouTube videos, using SponsorBlock's community list");
    auto *extrasRow = new QHBoxLayout;
    extrasRow->setSpacing(16);
    extrasRow->addWidget(subtitles_);
    extrasRow->addWidget(playlist_);
    extrasRow->addWidget(sponsors_);
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
#pragma endregion

#pragma region Link and preview
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
    folder_->setText(Widgets::shortPath(dir));
    folder_->setToolTip(dir);
    QSettings().setValue("folder", dir);
    emit folderChanged(dir);
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

void DownloadPage::notify(const QString &text) { banner_->setNotice(text); }

// The notice bar: yt-dlp or ffmpeg being fetched (their own messages), or yt-dlp missing.
// Missing tools get a popup when the window opens instead (MainWindow).
bool DownloadPage::toolsBusy() const
{
    return engine_->busy() || (ffmpeg_ && ffmpeg_->busy()) || (deno_ && deno_->busy());
}

void DownloadPage::engineChanged()
{
    if (toolsBusy())
        return;   // statusChanged shows the progress
    showNotice();
    if (isWebLink(link_->text()) && previewTitle_.isEmpty() && !preview_)
        startPreview();   // a preview was waiting for yt-dlp
    startNext();
}

void DownloadPage::linkChanged(const QString &text)
{
    const bool ok = isWebLink(text);
    lastPreview_ = {};
    previewPending_ = ok;
    showQuality();   // also enables Download for a web link
    delete preview_;   // a new link makes the old answer useless
    previewTitle_.clear();
    previewDelay_.stop();
    if (ok) {
        const QString id = youTubeId(text);
        if (id != quickPictureId_) {   // keep the picture when only the link's extras changed
            if (id != quickId_)
                quickId_.clear();       // not this video's request in flight: fetch again
            quickPictureId_.clear();
            title_->setText(QUrl(text.trimmed()).host());
            meta_->setText("Loading preview...");
            thumbnail_->setHint("Loading preview...");
        }
        quickPreview(text);
        previewDelay_.start();
    } else {
        quickId_.clear();
        quickPictureId_.clear();
        title_->setText("No video yet");
        meta_->setText(text.trimmed().isEmpty() ? QString() : "Invalid link");
        thumbnail_->setHint("Paste link to see preview");
    }
}

// YouTube: picture, title and channel straight from YouTube in a fraction of a second, while
// yt-dlp (a few seconds) works out the length, qualities and size.
void DownloadPage::quickPreview(const QString &link)
{
    const QString id = youTubeId(link);
    if (id.isEmpty() || id == quickId_)
        return;
    quickId_ = id;
    const auto request = [](const QUrl &url, int timeout) {
        QNetworkRequest req(url);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setTransferTimeout(timeout);
        return req;
    };
    const auto capped = [](QNetworkReply *reply, qint64 limit) {
        connect(reply, &QNetworkReply::downloadProgress, reply, [reply, limit](qint64 got, qint64 total) {
            if (got > limit || total > limit)
                reply->abort();
        });
    };

    const QUrl oembed("https://www.youtube.com/oembed?format=json&url="
                      + QUrl::toPercentEncoding("https://www.youtube.com/watch?v=" + id));
    QNetworkReply *meta = net_->get(request(oembed, 10000));
    capped(meta, 64 << 10);
    connect(meta, &QNetworkReply::finished, this, [this, meta, id] {
        meta->deleteLater();
        if (meta->error() || youTubeId(link_->text()) != id || !previewTitle_.isEmpty())
            return;   // failed, the link changed, or yt-dlp's fuller answer is already in
        const QJsonObject o = QJsonDocument::fromJson(meta->readAll()).object();
        if (o.value("title").toString().isEmpty())
            return;
        title_->setText(o.value("title").toString());
        meta_->setText(o.value("author_name").toString() + "  ·  youtube.com");
    });

    // The biggest picture YouTube makes, or the smaller one every video has.
    quickPicture(id, {"maxresdefault", "hqdefault"});
}

void DownloadPage::quickPicture(const QString &id, QStringList sizes)
{
    if (sizes.isEmpty())
        return;
    QNetworkRequest req(QUrl("https://i.ytimg.com/vi/" + id + "/" + sizes.takeFirst() + ".jpg"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(10000);
    QNetworkReply *reply = net_->get(req);
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64 total) {
        if (got > (8 << 20) || total > (8 << 20))
            reply->abort();   // a thumbnail is never this big
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, sizes] {
        reply->deleteLater();
        if (youTubeId(link_->text()) != id || thumbnail_->hasPicture())
            return;   // the link changed, or a picture is already up
        const QPixmap pixmap = reply->error() ? QPixmap() : Thumbnail::decode(reply->readAll());
        if (!pixmap.isNull()) {
            thumbnail_->setPicture(pixmap);
            quickPictureId_ = id;
        } else {
            quickPicture(id, sizes);   // next size down
        }
    });
}

void DownloadPage::startPreview()
{
    const QString url = link_->text().trimmed();
    if (!isWebLink(url))
        return;
    if (toolsBusy()) {   // without Deno, YouTube shows only low qualities
        meta_->setText(engine_->busy() ? "Waiting for yt-dlp..." : "Waiting for Queenfisher's tools...");
        return;   // engineChanged() retries
    }
    delete preview_;
    preview_ = new PreviewJob(url, playlist_->isChecked(), engine_, this);
    if (deno_)
        preview_->setDeno(deno_->program());
    preview_->setBrowserSignIn(AppSettings::get().signInAllowed(url));
    connect(preview_, &PreviewJob::logLine, this, [this](const QString &line) { log(line); });
    connect(preview_, &PreviewJob::signInWanted, this, [this, url] {
        QTimer::singleShot(0, this, [this, url] {   // once the preview has finished failing
            if (link_->text().trimmed() == url && offerSignIn(url))
                startPreview();
        });
    });
    connect(preview_, &PreviewJob::ready, this, [this, job = preview_.data()](const PreviewInfo &info) {
        if (!job->cookies().isEmpty()) {
            signInUrl_ = job->url();
            banner_->setNotice("This site wanted a sign-in, so Queenfisher is using your browser's.");
        }
        job->deleteLater();
        showPreview(info);
    });
    connect(preview_, &PreviewJob::failed, this, [this, job = preview_.data()](const QString &message) {
        job->deleteLater();
        previewPending_ = false;   // unknown qualities: every row can be picked again
        showQuality();
        meta_->setText(message);
        if (quickPictureId_.isEmpty())   // a picture straight from YouTube still beats nothing
            thumbnail_->setHint("No preview");
        banner_->setNotice(message);
    });
    preview_->start();
}

void DownloadPage::showPreview(const PreviewInfo &info)
{
    previewTitle_ = info.title;
    lastPreview_ = info;
    previewPending_ = false;
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
    if (!quickPictureId_.isEmpty() && quickPictureId_ == youTubeId(link_->text()) && thumbnail_->hasPicture()) {
        thumbnail_->setBadge(badge);   // the picture came straight from YouTube already
        return;
    }
    if (info.thumbnail.isEmpty()) {
        thumbnail_->setHint("No picture");
        return;
    }
    thumbnail_->setHint("Loading picture...");
    const QUrl address(info.thumbnail);
    const QString url = link_->text().trimmed();
    // A name is looked up first: a public-looking one can still point at this computer.
    QHostInfo::lookupHost(address.host(), this, [this, address, url, badge](const QHostInfo &host) {
        if (link_->text().trimmed() != url)
            return;   // the link changed meanwhile
        if (publicAddresses(host.addresses()))
            loadThumbnail(address, url, badge);
        else
            thumbnail_->setHint("No picture");
    });
}

void DownloadPage::loadThumbnail(const QUrl &address, const QString &url, const QString &badge)
{
    QNetworkRequest req(address);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    req.setTransferTimeout(20000);
    QNetworkReply *reply = net_->get(req);
    // Each hop gets the same checks as the address itself: a public link can redirect inwards.
    connect(reply, &QNetworkReply::redirected, reply, [reply](const QUrl &to) {
        if (to.scheme() != "https" || !publicHost(to)) {
            reply->abort();
            return;
        }
        QHostInfo::lookupHost(to.host(), reply, [reply](const QHostInfo &host) {
            if (publicAddresses(host.addresses()))
                emit reply->redirectAllowed();
            else
                reply->abort();
        });
    });
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 got, qint64 total) {
        if (got > (8 << 20) || total > (8 << 20))
            reply->abort();   // a thumbnail is never this big
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, badge] {
        reply->deleteLater();
        if (link_->text().trimmed() != url)
            return;   // the link changed while the picture loaded
        const QPixmap picture = reply->error() ? QPixmap() : Thumbnail::decode(reply->readAll());
        if (picture.isNull())
            thumbnail_->setHint("No picture");
        else
            thumbnail_->setPicture(picture, badge);
    });
}

void DownloadPage::showNotice()
{
    if (toolsBusy())
        return;   // they show their own progress
    const bool noYtDlp = engine_->version().isEmpty();
    // A first failure isn't shown yet, but it's retried all the same.
    const bool fetchFailed = (ffmpeg_ && ffmpeg_->failing()) || (deno_ && deno_->failing());
    banner_->setNotice(noYtDlp ? QString("yt-dlp isn't available yet. Check your connection; Queenfisher will try again.")
                               : QString());
    if (noYtDlp || fetchFailed)
        engineRetry_.start();   // startNext() asks again when nothing is downloading
    else
        engineRetry_.stop();
}

// "Up to 1080p · about 51 MB" for the current choices; quality rows the video doesn't have
// are greyed out. Nothing for playlists or before a preview.
void DownloadPage::showQuality()
{
    const Options::Format format = currentFormat();
    const int best = bestHeight(lastPreview_, format);
    // While the preview loads nothing is known, so only "Best available" (always right) can be
    // picked; afterwards, only what the video has.
    int shown = wantedQuality_, tallest = 0;
    for (int row = quality_->count() - 1; row >= 1; --row) {
        QListWidgetItem *item = quality_->item(row);
        const bool offered = !previewPending_ && (!best || heights[row] <= best);
        item->setFlags(offered ? item->flags() | Qt::ItemIsEnabled : item->flags() & ~Qt::ItemIsEnabled);
        if (offered)
            tallest = row;
    }
    // A pick the video lacks shows as the tallest it has, which is what yt-dlp gets anyway.
    // Not saved, so the next video still gets the user's own pick.
    if (best && !previewPending_ && shown > 0 && heights[shown] > best)
        shown = tallest ? tallest : 0;
    if (quality_->currentRow() != shown) {
        const QSignalBlocker quiet(quality_);
        quality_->setCurrentRow(shown);
    }
    const QString audioName = originalAudioName(lastPreview_);   // "Opus" on YouTube
    formatItem(Options::AudioOriginal)->setText(audioName.isEmpty() ? QString("Original audio") : audioName);

    // GIF is for short clips: a longer video would make a huge file.
    const bool tooLongForGif = lastPreview_.seconds > gifSeconds;
    QListWidgetItem *gif = formatItem(Options::Gif);
    gif->setFlags(tooLongForGif ? gif->flags() & ~Qt::ItemIsEnabled : gif->flags() | Qt::ItemIsEnabled);
    const bool blocked = format == Options::Gif && tooLongForGif;
    download_->setEnabled(isWebLink(link_->text()) && !blocked);
    if (blocked) {
        qualityInfo_->setText("GIF only works for videos up to a minute");
        qualityInfo_->setVisible(true);
        return;
    }

    if (lastPreview_.streams.isEmpty()) {
        qualityInfo_->setVisible(false);
        return;
    }
    QStringList parts;
    if (best && Options::hasQuality(format))
        parts << QString("Up to %1p").arg(best);
    const int cap = Options::hasQuality(format) ? heights[qMax(0, quality_->currentRow())] : 0;
    if (const qint64 bytes = estimateSize(lastPreview_, format, cap); bytes > 0)
        parts << "about " + Widgets::sizeText(double(bytes));
    qualityInfo_->setText(parts.join("  ·  "));
    qualityInfo_->setVisible(!parts.isEmpty());
}
#pragma endregion

#pragma region Downloads
// Unfinished downloads wait here, out of the download folder. Pieces left by a crash are
// cleared after a day; younger ones may belong to another Queenfisher still downloading.
QString DownloadPage::partialFolder()
{
    static const QString dir = [] {
        const QString path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/partial";
        QDir().mkpath(path);
        const QDateTime dayAgo = QDateTime::currentDateTime().addDays(-1);
        for (const QFileInfo &f : QDir(path).entryInfoList(QDir::Files | QDir::Hidden))
            if (f.lastModified() < dayAgo)
                QFile::remove(f.absoluteFilePath());
        return path;
    }();
    return dir;
}

Options::Format DownloadPage::currentFormat() const
{
    const QListWidgetItem *item = format_->currentItem();
    return item ? Options::Format(item->data(Qt::UserRole).toInt()) : Options::Mp4;
}

QListWidgetItem *DownloadPage::formatItem(Options::Format f) const
{
    for (int row = 0; row < format_->count(); ++row)
        if (QListWidgetItem *item = format_->item(row); item->data(Qt::UserRole).isValid() && item->data(Qt::UserRole).toInt() == f)
            return item;
    return nullptr;
}

void DownloadPage::selectFormat(Options::Format f)
{
    format_->setCurrentItem(formatItem(f));
}

Options DownloadPage::currentOptions() const
{
    Options o;
    o.url = link_->text().trimmed();
    o.folder = folder_->toolTip();
    o.tempFolder = partialFolder();
    o.format = currentFormat();
    o.maxHeight = heights[qMax(0, quality_->currentRow())];
    o.subtitles = subtitles_->isChecked();
    o.playlist = playlist_->isChecked();
    o.skipSponsors = sponsors_->isChecked();
    if (!o.playlist && lastPreview_.playlistCount < 0)
        o.expectedParts = estimateParts(lastPreview_, o.format, o.maxHeight);
    o.overwrite = AppSettings::get().existing() == AppSettings::Existing::Replace;
    o.browserSignIn = AppSettings::get().signInAllowed(o.url);
    o.needsSignIn = o.browserSignIn && o.url == signInUrl_;   // the preview already found one is needed
    if (ffmpeg_)
        o.ffmpeg = ffmpeg_->location();
    if (deno_)
        o.deno = deno_->program();
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

// A site wants a sign-in it isn't allowed yet: ask once, unless browser sign-in is off.
bool DownloadPage::offerSignIn(const QString &url)
{
    const QString site = AppSettings::siteOf(url);
    if (AppSettings::get().signInAllowed(url))
        return true;   // allowed meanwhile (another row of the same site asked)
    if (!AppSettings::get().browserSignIn() || site.isEmpty() || declinedSites_.contains(site))
        return false;
    if (!askSignIn(site)) {
        declinedSites_.insert(site);
        return false;
    }
    AppSettings::get().allowSignIn(url);
    return true;
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
    connect(job, &DownloadJob::signInWanted, this, [this, item, job] {
        QTimer::singleShot(0, this, [this, item, job] {   // once the row shows it failed
            if (jobs_.value(item) != job || stateOf(item) != State::Failed || !offerSignIn(job->options().url))
                return;
            Options retry = job->options();
            retry.browserSignIn = true;
            retry.needsSignIn = true;
            addJob(item, retry);
            startNext();
        });
    });
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
        const QString done = job->alreadyHad() ? "Already downloaded" : "Complete";   // never a silent skip
        setState(item, ok ? State::Done : stopped ? State::Stopped : State::Failed, ok ? done : message);
        if (const QFileInfo file(job->file()); ok && file.exists())
            item->setText(Size, Widgets::sizeText(double(file.size())));   // the finished file, after any conversion
        log(ok ? done + ": " + job->file() : stopped ? message : "Failed: " + message, number);
        if (ok)
            library_->add({item->text(Name), job->options().url, job->file(), job->options().label(),
                           QFileInfo(job->file()).size(), QDateTime::currentDateTime()});
        running_.remove(job);
        refreshButtons();
        startNext();
        if (ok && job->alreadyHad() && !job->options().overwrite   // asked once the job is done with
            && AppSettings::get().existing() == AppSettings::Existing::Ask)
            QTimer::singleShot(0, this, [this, item, job] {
                if (jobs_.value(item) != job || !askOverwrite(job->file()))
                    return;   // the row went or was redone meanwhile, or the user keeps the file
                Options again = job->options();
                again.overwrite = true;
                addJob(item, again);
                startNext();
            });
    });
}

// Up to "downloads at once" run side by side, top to bottom, and wait while yt-dlp or ffmpeg is
// being fetched, or while there's no yt-dlp at all yet (offline on the first run). The same link
// in the same format waits for the one already running, so the two never share a file.
void DownloadPage::startNext()
{
    const auto sameAsRunning = [this](const Options &o) {
        for (const DownloadJob *r : std::as_const(running_))
            if (r->options().url == o.url && r->options().label() == o.label())
                return true;
        return false;
    };
    const bool ready = !engine_->path().isEmpty();
    if (ready && !toolsBusy()) {
        for (int i = 0; i < list_->topLevelItemCount() && running_.size() < AppSettings::get().downloadsAtOnce(); ++i) {
            QTreeWidgetItem *item = list_->topLevelItem(i);
            DownloadJob *job = jobs_.value(item);
            if (stateOf(item) != State::Waiting || !job || sameAsRunning(job->options()))
                continue;
            running_.insert(job);
            setState(item, State::Running, "Starting...");
            job->start();
        }
    }
    if (running_.isEmpty() && !engine_->busy()) {
        engine_->checkDaily();   // idle: the once-a-day update check, or another try at fetching yt-dlp
        if (ffmpeg_)
            ffmpeg_->ensure();   // Windows: fetches ffmpeg the first time, refreshes it monthly
        if (deno_)
            deno_->ensure();     // where no Deno is installed: the same
    }
    refreshButtons();
    reportActivity();
}

int DownloadPage::downloadsAtOnce() const { return AppSettings::get().downloadsAtOnce(); }

void DownloadPage::setDownloadsAtOnce(int n)
{
    AppSettings::get().setDownloadsAtOnce(n);
    startNext();   // more room: waiting downloads start now; less: running ones finish first
}

void DownloadPage::removeRow(QTreeWidgetItem *item)
{
    DownloadJob *job = jobs_.take(item);
    running_.remove(job);
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
#pragma endregion
