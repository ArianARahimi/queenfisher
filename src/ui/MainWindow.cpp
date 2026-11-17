#include "MainWindow.h"
#include "ProgressDelegate.h"
#include "Thumbnail.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

enum Column { Title, Format, Size, Progress, State };

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

}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("QFisher");
    resize(860, 640);
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

    auto *openFolder = new QPushButton("Open folder");
    openFolder->setToolTip("Show the downloaded files");
    auto *remove = new QPushButton("Remove");
    remove->setToolTip("Take the selected downloads off the list (files stay)");
    remove->setEnabled(false);
    auto *listButtons = new QHBoxLayout;
    listButtons->addWidget(remove);
    listButtons->addStretch(1);
    listButtons->addWidget(openFolder);

    auto *listBox = new QFrame;
    listBox->setObjectName("card");
    auto *listLayout = new QVBoxLayout(listBox);
    listLayout->setContentsMargins(0, 0, 0, 8);
    listLayout->setSpacing(8);
    listLayout->addWidget(strip("Downloads"));
    auto *listInner = new QVBoxLayout;
    listInner->setContentsMargins(8, 0, 8, 0);
    listInner->setSpacing(8);
    listInner->addWidget(list_, 1);
    listInner->addLayout(listButtons);
    listLayout->addLayout(listInner, 1);

    status_ = new QLabel;
    status_->setObjectName("status");

    auto *central = new QWidget;
    central->setObjectName("central");
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(10);
    layout->addLayout(linkRow);
    layout->addWidget(buildCard());
    layout->addWidget(listBox, 1);
    layout->addWidget(status_);
    setCentralWidget(central);

    connect(link_, &QLineEdit::textChanged, this, &MainWindow::linkChanged);
    connect(link_, &QLineEdit::returnPressed, this, [this] { if (download_->isEnabled()) download(); });
    connect(paste, &QPushButton::clicked, this, [this] {
        link_->setText(QGuiApplication::clipboard()->text().trimmed());
        link_->setFocus();
    });
    connect(openFolder, &QPushButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(folder_->toolTip()));
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

    // Audio has no picture size to choose.
    connect(format_, &QComboBox::currentIndexChanged, this, [this](int i) { quality_->setEnabled(i < 3); });
    connect(browse, &QPushButton::clicked, this, &MainWindow::chooseFolder);
    connect(download_, &QPushButton::clicked, this, &MainWindow::download);
    return card;
}

void MainWindow::linkChanged(const QString &text)
{
    const bool ok = isWebLink(text);
    download_->setEnabled(ok);
    if (ok) {
        title_->setText(QUrl(text.trimmed()).host());
        meta_->setText("Preview isn't built yet");
        thumbnail_->setHint("Loading preview...");
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
}

void MainWindow::download()
{
    const QString format = format_->currentText().section(' ', 0, 0) == "Audio"
        ? format_->currentText().mid(12).remove(')') : format_->currentText().section(' ', 0, 0);
    const QString quality = quality_->isEnabled() ? " " + quality_->currentText().section(' ', 0, 0) : QString();
    auto *item = new QTreeWidgetItem(list_, {title_->text(), format + quality, "", "", "Waiting"});
    item->setData(Progress, Qt::UserRole, -1.0);
    item->setToolTip(Title, link_->text().trimmed());
    list_->scrollToItem(item);
    link_->clear();
    refreshStatus();
}

void MainWindow::refreshStatus()
{
    const int count = list_->topLevelItemCount();
    const QString where = "saving to " + folder_->text();
    status_->setText(count == 0 ? "Nothing downloading  ·  " + where
                                : QString("%1 in the list  ·  %2").arg(count).arg(where));
}
