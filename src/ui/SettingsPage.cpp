#include "SettingsPage.h"
#include "AppSettings.h"
#include "DownloadPage.h"
#include "Widgets.h"
#include "core/YtDlpEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {

QLabel *note(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("paneTitle");   // the quiet grey of side notes
    l->setWordWrap(true);
    return l;
}

QGridLayout *grid()
{
    auto *g = new QGridLayout;
    g->setHorizontalSpacing(10);
    g->setVerticalSpacing(8);
    g->setColumnStretch(1, 1);
    return g;
}

}

SettingsPage::SettingsPage(DownloadPage *downloads, YtDlpEngine *engine, bool trayAvailable, QWidget *parent)
    : QWidget(parent), downloads_(downloads), engine_(engine)
{
    setObjectName("page");

    // Downloads
    folder_ = new QLineEdit;
    folder_->setReadOnly(true);
    auto *change = new QPushButton("Change...");
    auto *folderRow = new QHBoxLayout;
    folderRow->addWidget(folder_, 1);
    folderRow->addWidget(change);
    atOnce_ = new QComboBox;
    for (int n = 1; n <= AppSettings::maxAtOnce; ++n)
        atOnce_->addItem(QString::number(n), n);
    existing_ = new QComboBox;
    existing_->addItem("Ask me", int(AppSettings::Existing::Ask));
    existing_->addItem("Keep the one I have", int(AppSettings::Existing::Keep));
    existing_->addItem("Replace it", int(AppSettings::Existing::Replace));
    auto *dl = grid();
    dl->addWidget(new QLabel("Save to"), 0, 0);
    dl->addLayout(folderRow, 0, 1);
    dl->addWidget(new QLabel("Downloads at once"), 1, 0);
    dl->addWidget(atOnce_, 1, 1, Qt::AlignLeft);
    dl->addWidget(new QLabel("File already there"), 2, 0);
    dl->addWidget(existing_, 2, 1, Qt::AlignLeft);

    // Sign-in
    signIn_ = new QCheckBox("Use my browser's sign-in when a site asks for one");
    auto *si = new QVBoxLayout;
    si->setSpacing(6);
    si->addWidget(signIn_);
    si->addWidget(note("Only used after a site refuses without it. Off: those downloads fail instead."));

    // Window
    tray_ = new QCheckBox("Minimizing hides Queenfisher in the tray");
    tray_->setEnabled(trayAvailable);
    auto *win = new QVBoxLayout;
    win->setSpacing(6);
    win->addWidget(tray_);
    if (!trayAvailable)
        win->addWidget(note("This desktop has no tray."));

    // yt-dlp
    version_ = new QLabel;
    channel_ = new QComboBox;
    channel_->addItem("Regular", int(YtDlpEngine::Stable));
    channel_->addItem("Nightly", int(YtDlpEngine::Nightly));
    auto *check = new QPushButton("Check for Updates Now");
    auto *yt = grid();
    yt->addWidget(new QLabel("Version"), 0, 0);
    yt->addWidget(version_, 0, 1);
    yt->addWidget(new QLabel("Releases"), 1, 0);
    yt->addWidget(channel_, 1, 1, Qt::AlignLeft);
    yt->addWidget(check, 2, 1, Qt::AlignLeft);
    yt->addWidget(note("Checked once a day. Every update is verified against the yt-dlp developers' signature."), 3, 0, 1, 2);

    auto *left = new QVBoxLayout;
    left->setSpacing(6);
    left->addWidget(Widgets::sideBox("Downloads", dl));
    left->addWidget(Widgets::sideBox("Sign-in", si));
    left->addStretch(1);
    auto *right = new QVBoxLayout;
    right->setSpacing(6);
    right->addWidget(Widgets::sideBox("yt-dlp", yt));
    right->addWidget(Widgets::sideBox("Window", win));
    right->addStretch(1);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(6);
    layout->addLayout(left, 1);
    layout->addLayout(right, 1);

    AppSettings &s = AppSettings::get();
    connect(change, &QPushButton::clicked, downloads_, &DownloadPage::chooseFolder);
    connect(atOnce_, &QComboBox::activated, this, [this] { downloads_->setDownloadsAtOnce(atOnce_->currentData().toInt()); });
    connect(existing_, &QComboBox::activated, this, [this, &s] { s.setExisting(AppSettings::Existing(existing_->currentData().toInt())); });
    connect(signIn_, &QCheckBox::toggled, &s, &AppSettings::setBrowserSignIn);
    connect(tray_, &QCheckBox::toggled, &s, &AppSettings::setMinimizeToTray);
    connect(channel_, &QComboBox::activated, this, [this] { engine_->switchChannel(YtDlpEngine::Channel(channel_->currentData().toInt())); });
    connect(check, &QPushButton::clicked, engine_, &YtDlpEngine::updateNow);

    connect(&s, &AppSettings::changed, this, &SettingsPage::refresh);
    connect(downloads_, &DownloadPage::folderChanged, this, &SettingsPage::refresh);
    connect(engine_, &Engine::readyChanged, this, &SettingsPage::refresh);
    refresh();
}

void SettingsPage::refresh()
{
    const AppSettings &s = AppSettings::get();
    const QSignalBlocker b1(signIn_), b2(tray_);
    folder_->setText(Widgets::shortPath(downloads_->folder()));
    folder_->setToolTip(downloads_->folder());
    atOnce_->setCurrentIndex(atOnce_->findData(s.downloadsAtOnce()));
    existing_->setCurrentIndex(existing_->findData(int(s.existing())));
    signIn_->setChecked(s.browserSignIn());
    tray_->setChecked(s.minimizeToTray());
    const QString v = engine_->version();
    version_->setText(engine_->busy() ? "Updating..." : v.isEmpty() ? "Not installed yet" : v);
    channel_->setCurrentIndex(channel_->findData(int(engine_->channel())));
    channel_->setEnabled(!engine_->busy());
}
