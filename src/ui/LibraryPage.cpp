#include "LibraryPage.h"
#include "Widgets.h"
#include "core/Library.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

enum Column { Name, Format, Size, Finished, Where };
constexpr int indexRole = Qt::UserRole;   // position in Library::entries()

}

LibraryPage::LibraryPage(Library *library, QWidget *parent) : QWidget(parent), library_(library)
{
    setObjectName("page");

    search_ = new QLineEdit;
    search_->setPlaceholderText("Search your downloads");
    search_->setClearButtonEnabled(true);

    list_ = new QTreeWidget;
    list_->setHeaderLabels({"Name", "Format", "Size", "Finished", "Folder"});
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->setSortingEnabled(false);
    QHeaderView *header = list_->header();
    header->setStretchLastSection(false);
    header->setSectionResizeMode(Name, QHeaderView::Stretch);
    for (int c : {Format, Size, Finished})
        header->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(Where, QHeaderView::Interactive);
    header->resizeSection(Where, 220);

    launch_ = Widgets::action("launch", "Launch", "Open the selected file");
    explore_ = Widgets::action("explore", "Explore", "Show the folder the file is in");
    forget_ = Widgets::action("remove", "Remove", "Take it off the library (the file stays)");

    auto *row = new QHBoxLayout;
    row->addStretch(1);
    for (QToolButton *b : {launch_, explore_, forget_})
        row->addWidget(b);
    row->addStretch(1);

    auto *title = new QLabel("Finished downloads");
    title->setObjectName("paneTitle");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(4);
    layout->addWidget(search_);
    layout->addWidget(title);
    layout->addWidget(list_, 1);
    layout->addLayout(row);

    connect(library_, &Library::changed, this, &LibraryPage::refill);
    connect(search_, &QLineEdit::textChanged, this, &LibraryPage::filter);
    connect(list_, &QTreeWidget::itemSelectionChanged, this, &LibraryPage::refreshButtons);
    connect(list_, &QTreeWidget::itemDoubleClicked, launch_, &QToolButton::click);
    connect(launch_, &QToolButton::clicked, this, [this] {
        if (const int i = selectedIndex(); i >= 0)
            QDesktopServices::openUrl(QUrl::fromLocalFile(library_->entries()[i].file));
    });
    connect(explore_, &QToolButton::clicked, this, [this] {
        if (const int i = selectedIndex(); i >= 0)
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(library_->entries()[i].file).absolutePath()));
    });
    connect(forget_, &QToolButton::clicked, this, [this] { library_->remove(selectedIndex()); });
    refill();
}

// Opening the tab drops files that were moved or deleted since.
void LibraryPage::showEvent(QShowEvent *event)
{
    library_->forgetMissing();
    QWidget::showEvent(event);
}

void LibraryPage::refill()
{
    list_->clear();
    const QList<Library::Entry> &entries = library_->entries();
    for (int i = 0; i < entries.size(); ++i) {
        const Library::Entry &e = entries[i];
        auto *item = new QTreeWidgetItem(list_, {e.title, e.format, e.bytes > 0 ? Widgets::sizeText(e.bytes) : QString(),
                                                 QLocale().toString(e.finished, QLocale::ShortFormat),
                                                 Widgets::shortPath(QFileInfo(e.file).absolutePath())});
        item->setIcon(Name, Widgets::formatIcon(e.format.startsWith("MP3") || e.format.startsWith("Audio")));
        item->setData(Name, indexRole, i);
        item->setToolTip(Name, e.file);
    }
    filter(search_->text());
    refreshButtons();
}

void LibraryPage::filter(const QString &text)
{
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = list_->topLevelItem(i);
        item->setHidden(!text.isEmpty() && !item->text(Name).contains(text, Qt::CaseInsensitive));
    }
}

int LibraryPage::selectedIndex() const
{
    const QList<QTreeWidgetItem *> items = list_->selectedItems();
    return items.isEmpty() ? -1 : items.first()->data(Name, indexRole).toInt();
}

void LibraryPage::refreshButtons()
{
    const int i = selectedIndex();
    const bool exists = i >= 0 && QFileInfo::exists(library_->entries()[i].file);
    launch_->setEnabled(exists);
    explore_->setEnabled(exists);
    forget_->setEnabled(i >= 0);
}
