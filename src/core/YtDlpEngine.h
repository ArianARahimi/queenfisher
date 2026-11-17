#pragma once

#include "Engine.h"

#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>

class QNetworkReply;

// Queenfisher's own copy of yt-dlp, fetched from yt-dlp's GitHub releases.
// Every file is checked against the release checksums, and the checksums against
// the yt-dlp developers' signature, before it replaces the copy in use.
class YtDlpEngine : public Engine
{
    Q_OBJECT

public:
    enum Channel { Stable, Nightly };

    explicit YtDlpEngine(const QString &dataDir, QObject *parent = nullptr);

    QString path() const override;
    QString version() const override;
    bool busy() const override { return busy_; }
    void repair(Done done) override;

    void checkDaily();   // installs on first run, otherwise updates at most once a day; call when idle
    void updateNow();                  // check the current channel right away
    void switchChannel(Channel channel);
    Channel channel() const { return stamp_.channel; }
    qint64 lastChecked() const { return stamp_.checked; }   // ms since epoch, 0 = never
    QString location() const { return managedPath(); }

    // Pure helpers, public for tests.
    static QString assetName();   // release file for this computer, empty if unsupported
    static QString checksumFor(const QByteArray &sums, const QString &asset);
    static int compareVersions(const QString &a, const QString &b, int parts = 99);

private:
    struct Stamp { QString version; Channel channel = Stable; qint64 checked = 0; };

    void run(Channel channel, bool force, Done done);
    void install(Channel channel, const QString &tag);
    void finish(bool changed, const QString &message);
    void fetch(const QUrl &url, qint64 limit, std::function<void(QNetworkReply *)> then);
    void latestTag(Channel channel, std::function<void(const QString &tag)> then);
    void smokeTest(const QString &file, const QString &tag);

    QString managedPath() const;
    void loadStamp();
    void saveStamp() const;

    QString dir_;
    Stamp stamp_;
    bool busy_ = false;
    QList<Done> waiting_;
    QNetworkAccessManager net_;
    QByteArray sums_;
};
