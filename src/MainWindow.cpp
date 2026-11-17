#include "MainWindow.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("QFisher");
    resize(820, 480);

    link_ = new QLineEdit;
    link_->setPlaceholderText("Paste a link to a video, song or playlist");
    link_->setClearButtonEnabled(true);

    add_ = new QPushButton("Add");
    add_->setDefault(true);
    add_->setEnabled(false);

    queue_ = new QTreeWidget;
    queue_->setHeaderLabels({"Title", "Status", "Progress"});
    queue_->setRootIsDecorated(false);
    queue_->setAlternatingRowColors(true);
    queue_->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    auto *top = new QHBoxLayout;
    top->addWidget(link_);
    top->addWidget(add_);

    auto *central = new QWidget;
    auto *layout = new QVBoxLayout(central);
    layout->addLayout(top);
    layout->addWidget(queue_);
    setCentralWidget(central);

    // Add stays disabled until the box holds a web link.
    connect(link_, &QLineEdit::textChanged, this, [this](const QString &text) {
        const QUrl url(text.trimmed());
        add_->setEnabled(url.isValid() && (url.scheme() == "https" || url.scheme() == "http"));
    });
    connect(add_, &QPushButton::clicked, this, &MainWindow::addLink);
    connect(link_, &QLineEdit::returnPressed, this, [this] { if (add_->isEnabled()) addLink(); });
}

void MainWindow::addLink()
{
    auto *item = new QTreeWidgetItem(queue_, {link_->text().trimmed(), "Waiting", ""});
    queue_->scrollToItem(item);
    link_->clear();
}
