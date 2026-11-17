#include "EnginePage.h"
#include "Widgets.h"
#include "core/YtDlpEngine.h"

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QUrl>
#include <QVBoxLayout>

EnginePage::EnginePage(YtDlpEngine *engine, QWidget *parent) : QWidget(parent), engine_(engine)
{
    setObjectName("page");
    banner_ = new Banner;
    version_ = new QLabel;
    channel_ = new QLabel;
    checked_ = new QLabel;
    location_ = new QLabel;
    location_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    location_->setWordWrap(true);
    auto *key = new QLabel("AC0C BBE6 848D 6A87 3464  AF4E 57CF 6593 3B5A 7581");
    key->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(14);
    form->setVerticalSpacing(8);
    form->addRow("Version:", version_);
    form->addRow("Channel:", channel_);
    form->addRow("Last checked:", checked_);
    form->addRow("Location:", location_);
    form->addRow("Signing key:", key);

    auto *about = new QLabel(
        "QFisher keeps its own copy of yt-dlp. Every update comes from yt-dlp's GitHub releases and is only "
        "used after its signature and checksum check out. It checks once a day, between downloads. When a "
        "site changes and downloads start failing, QFisher updates on its own, and if that isn't enough it "
        "switches to the nightly build until the regular release catches up.");
    about->setWordWrap(true);
    about->setObjectName("meta");

    auto *pane = new QFrame;
    pane->setObjectName("previewPane");
    auto *paneLayout = new QVBoxLayout(pane);
    paneLayout->setContentsMargins(10, 10, 10, 10);
    paneLayout->setSpacing(10);
    paneLayout->addWidget(banner_);
    paneLayout->addLayout(form);
    paneLayout->addWidget(about);
    paneLayout->addStretch(1);

    check_ = Widgets::action("retry", "Check Now", "Look for a newer yt-dlp right away");
    nightly_ = Widgets::action("download", "Use Nightly", "Switch to the nightly build, which gets site fixes first");
    stable_ = Widgets::action("download", "Use Regular", "Switch back to the regular release");
    auto *open = Widgets::action("explore", "Explore", "Show the folder QFisher keeps yt-dlp in");

    auto *row = new QHBoxLayout;
    row->addStretch(1);
    for (QToolButton *b : {check_, nightly_, stable_, open})
        row->addWidget(b);
    row->addStretch(1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addWidget(pane, 1);
    layout->addLayout(row);

    connect(check_, &QToolButton::clicked, engine_, &YtDlpEngine::updateNow);
    connect(nightly_, &QToolButton::clicked, this, [this] { engine_->switchChannel(YtDlpEngine::Nightly); });
    connect(stable_, &QToolButton::clicked, this, [this] { engine_->switchChannel(YtDlpEngine::Stable); });
    connect(open, &QToolButton::clicked, this, [this] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(engine_->location()).absolutePath()));
    });
    connect(engine_, &Engine::readyChanged, this, &EnginePage::refresh);
    connect(engine_, &Engine::statusChanged, this, [this](const QString &text) {
        banner_->setNotice(Banner::Info, text);
    });
    refresh();
}

void EnginePage::refresh()
{
    const bool busy = engine_->busy();
    const bool own = QFileInfo::exists(engine_->location());
    version_->setText(engine_->version().isEmpty() ? QString("Not installed yet") : engine_->version());
    channel_->setText(!own ? QString("-")
                     : engine_->channel() == YtDlpEngine::Nightly ? QString("Nightly (gets site fixes first)")
                                                                  : QString("Regular release"));
    checked_->setText(engine_->lastChecked() > 0
                          ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(engine_->lastChecked()), QLocale::ShortFormat)
                          : QString("Never"));
    location_->setText(own ? engine_->location() : engine_->path().isEmpty() ? QString("-") : engine_->path());
    check_->setEnabled(!busy);
    nightly_->setEnabled(!busy && !(own && engine_->channel() == YtDlpEngine::Nightly));
    stable_->setEnabled(!busy && own && engine_->channel() == YtDlpEngine::Nightly);
    if (!busy)
        banner_->setNotice(own ? Banner::Info : Banner::Warning,
                           own ? "yt-dlp is ready and checked against its developers' signature."
                               : "QFisher hasn't got its own yt-dlp yet. It will fetch one when it can reach GitHub.");
}
