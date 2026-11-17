#include "AboutPage.h"

#include <QApplication>
#include <QFrame>
#include <QLabel>
#include <QVBoxLayout>

AboutPage::AboutPage(QWidget *parent) : QWidget(parent)
{
    setObjectName("page");

    auto *name = new QLabel("Queenfisher " + QApplication::applicationVersion());
    name->setObjectName("wordmark");

    auto *text = new QLabel(
        "Free media downloader for Windows, macOS and Linux. Paste a link, pick a quality, done.<br><br>"
        "Licensed under the GNU GPL, version 3 or later.<br>"
        "Downloads are done by <a href='https://github.com/yt-dlp/yt-dlp'>yt-dlp</a> (public domain).<br>"
        "Icons from the Tango Desktop Project (public domain).");
    text->setWordWrap(true);
    text->setOpenExternalLinks(true);
    text->setTextInteractionFlags(Qt::TextBrowserInteraction);

    auto *pane = new QFrame;
    pane->setObjectName("previewPane");
    auto *paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(16, 14, 16, 14);
    paneLayout->setSpacing(10);
    paneLayout->addWidget(name);
    paneLayout->addWidget(text);
    paneLayout->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->addWidget(pane);
}
