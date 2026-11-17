#include "MainWindow.h"
#include "LcdDisplay.h"
#include "SegmentBar.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

bool isWebLink(const QString &text)
{
    const QUrl url(text.trimmed());
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty();
}

QPushButton *button(const QString &text, const QString &tip)
{
    auto *b = new QPushButton(text);
    b->setToolTip(tip);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("QFisher");
    resize(760, 560);
    setMinimumSize(560, 420);

    display_ = new LcdDisplay;
    progress_ = new SegmentBar;

    link_ = new QLineEdit;
    link_->setPlaceholderText("Paste a link to a video, song or playlist");
    link_->setClearButtonEnabled(true);
    auto *paste = button("PASTE", "Paste the link you copied");
    add_ = button("ADD", "Add this link to the list");
    add_->setEnabled(false);

    auto *start = button("► START", "Start downloading the list");
    // CP437 has no pause sign, so the double bar stands in for it.
    auto *pause = button("║ PAUSE", "Pause after the current part");
    auto *stop = button("■ STOP", "Stop all downloads");
    auto *remove = button("REMOVE", "Remove the selected downloads from the list");
    auto *folder = button("FOLDER", "Open the folder downloads are saved to");
    for (QPushButton *b : {start, pause, stop, remove})
        b->setEnabled(false);  // nothing to download yet

    queue_ = new QTreeWidget;
    queue_->setColumnCount(2);
    queue_->setHeaderHidden(true);
    queue_->setRootIsDecorated(false);
    queue_->setAlternatingRowColors(true);
    queue_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    queue_->header()->setStretchLastSection(false);
    queue_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    queue_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);

    status_ = new QLabel;
    status_->setObjectName("status");

    auto *linkRow = new QHBoxLayout;
    linkRow->setSpacing(6);
    linkRow->addWidget(link_, 1);
    linkRow->addWidget(paste);
    linkRow->addWidget(add_);

    auto *transport = new QHBoxLayout;
    transport->setSpacing(4);
    for (QPushButton *b : {start, pause, stop})
        transport->addWidget(b);
    transport->addSpacing(14);
    transport->addWidget(remove);
    transport->addStretch(1);
    transport->addWidget(folder);

    auto *central = new QWidget;
    central->setObjectName("central");
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(10, 10, 10, 8);
    layout->setSpacing(8);
    layout->addWidget(display_);
    layout->addWidget(progress_);
    layout->addLayout(linkRow);
    layout->addLayout(transport);
    layout->addWidget(queue_, 1);
    layout->addWidget(status_);
    setCentralWidget(central);

    connect(link_, &QLineEdit::textChanged, this, [this](const QString &t) { add_->setEnabled(isWebLink(t)); });
    connect(link_, &QLineEdit::returnPressed, this, [this] { if (add_->isEnabled()) addLink(); });
    connect(add_, &QPushButton::clicked, this, &MainWindow::addLink);
    connect(paste, &QPushButton::clicked, this, [this] {
        link_->setText(QGuiApplication::clipboard()->text().trimmed());
        link_->setFocus();
    });

    refreshCounts();
}

void MainWindow::addLink()
{
    const int number = queue_->topLevelItemCount() + 1;
    auto *item = new QTreeWidgetItem(queue_, {QString("%1. %2").arg(number).arg(link_->text().trimmed()), "Waiting"});
    item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
    queue_->scrollToItem(item);
    link_->clear();
    refreshCounts();
}

void MainWindow::refreshCounts()
{
    const int count = queue_->topLevelItemCount();
    display_->setIndicators(false, false, count);
    status_->setText(count == 0 ? "No downloads yet" : QString("%1 in the list").arg(count));
}
