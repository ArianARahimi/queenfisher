#pragma once

#include "Engine.h"

#include <QByteArray>
#include <QFileSystemWatcher>
#include <QList>
#include <QNetworkAccessManager>
#include <QPair>
#include <QTimer>

class QNetworkReply;

// Queenfisher's own copy of yt-dlp, fetched from yt-dlp's GitHub releases.
// Every file is checked against the release checksums, and the checksums against
// the yt-dlp developers' signature, before it replaces the copy in use.
// A copy put back by hand (yt-dlp.previous over yt-dlp, to roll back) is used if it's one
// Queenfisher checked before; the version rolled back from isn't installed again by itself.
class YtDlpEngine : public Engine
{
    Q_OBJECT

public:
    enum Channel { Stable, Nightly };

    explicit YtDlpEngine(const QString &dataDir, QObject *parent = nullptr);
    ~YtDlpEngine() override;

    QString path() const override;
    QString version() const override;
    bool busy() const override { return busy_; }
    void repair(Done done) override;

    void checkDaily();   // installs on first run, otherwise updates at most once a day; call when idle
    void updateNow();                  // check the current channel right away
    void switchChannel(Channel channel);
    Channel channel() const { return stamp_.channel; }   // of the copy installed

    // Asked when a newer stable yt-dlp didn't fix a download: true = try nightly. Asked once
    // per run; unset or declined, repair stays on the current channel.
    std::function<bool()> askNightly;

    // Pure helpers, public for tests.
    static QString assetName();   // release file for this computer, empty if unsupported
    static QString checksumFor(const QByteArray &sums, const QString &asset);
    static int compareVersions(const QString &a, const QString &b, int parts = 99);
    static bool isDowngrade(const QString &tag, const QString &installed);   // older by date
    static bool isRolledBack(const QString &tag, const QString &skip);       // skip it or older
    static bool isOlderSignature(qint64 created, qint64 inUse);              // signed before the copy in use
    static QString fileHash(const QString &file);                            // SHA-256 hex, empty if unreadable

private:
    enum Asked { Auto, Update, Switch };   // Update skips nothing; Switch may also go back a version
    using Known = QList<QPair<QString, QString>>;   // SHA-256 -> version, oldest first
    struct Stamp
    {
        QString version;
        Channel channel = Stable;
        qint64 checked = 0;
        Known known;    // every copy this Queenfisher checked and put in place
        QString skip;   // rolled back from this version by hand: not installed again automatically
        qint64 signedAt = 0; // when the copy put in place last was signed, seconds since 1970
    };

    void run(Channel channel, bool force, const Done &done, Asked asked = Auto);
    void offerNightly(const Done &done, const QString &message);
    void recognize();   // the file in place changed: adopt it if it's a known copy, else refuse it
    void remember(const QString &hash, const QString &version);
    void install(Channel channel, const QString &tag, bool sameChannel);
    void finish(bool changed, const QString &message);
    void fetch(const QUrl &url, qint64 limit, const std::function<void(QNetworkReply *)> &then);
    void latestTag(Channel channel, const std::function<void(const QString &tag)> &then);
    void smokeTest(const QString &file, const QString &tag, qint64 signedAt);

    QString managedPath() const;
    void loadStamp();
    void saveStamp() const;

    QString dir_;
    Stamp stamp_;
    bool trusted_ = false;   // the file in place is a known copy
    QFileSystemWatcher watcher_;
    QTimer settle_;          // waits for a copy by hand to finish before looking at it
    bool busy_ = false;
    qint64 lastTry_ = 0;   // when the last check started, ms since epoch
    struct Request
    {
        Channel channel = Stable;
        Done done;
        Asked asked = Auto;
    };
    QList<Done> waiting_;     // callers sharing the run in progress
    QList<Done> nightlyAsked_; // repairs waiting for the answer about nightly
    int nightlyAnswer_ = -1; // -1 not asked yet this run, 0 no, 1 yes
    QList<Request> queued_;   // runs asked for on purpose while busy, each after the last
    QNetworkAccessManager net_;
    QByteArray sums_;
};
