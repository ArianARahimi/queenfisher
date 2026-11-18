#pragma once

#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

// A helper program Queenfisher fetches itself when the computer doesn't have it: a zip and
// its SHA-256 from GitHub, unpacked off the window's thread, tried out, and only then put in
// place. Fetched again after 30 days; the old copy keeps working until the new one does.
class FetchedTool : public QObject
{
    Q_OBJECT

public:
    QString location() const;   // folder holding the programs; empty until they're there
    bool busy() const { return busy_; }
    QString lastError() const { return lastError_; }   // why it's missing; empty while a copy works
    void ensure();              // fetch if wanted and missing or old; call when idle

    // The part that doesn't need the network, public for tests: checks the zip's SHA-256,
    // unpacks it with `unzipper` (bsdtar, tar, unzip or python3) and keeps only what's needed.
    bool install(const QString &zip, const QString &expectedSha256, const QString &unzipper, QString *why);
    static QString exe(const QString &name);   // "deno.exe" on Windows, "deno" elsewhere
    static QString unzipper();                 // what this computer can unpack a zip with; empty if nothing

signals:
    void statusChanged(const QString &text);
    void logLine(const QString &line);
    void readyChanged();   // location() or busy() changed

protected:
    struct Source {
        QUrl sums;      // checksum list naming the asset
        QUrl zip;
        QString asset;  // the zip's name in that list
        qint64 limit;   // refuse downloads bigger than this
    };
    FetchedTool(const QString &dir, const QString &name, const QString &size, QObject *parent);
    virtual bool wanted() const = 0;                  // should Queenfisher fetch it on this computer
    virtual bool source(Source *s) const = 0;         // false: no build for this computer
    virtual bool keep(const QFileInfo &f) const = 0;  // files from the zip worth keeping
    virtual QStringList programs() const = 0;         // must be there and run
    virtual QString versionFlag() const = 0;          // "-version" or "--version"
    virtual QString checksum(const QByteArray &sums, const QString &asset) const;   // the asset's SHA-256 in lower case

private:
    void download(const Source &s, const QString &expected);
    void finish(bool ok, const QString &message);
    bool stale() const;

    QString dir_, name_, size_;
    bool busy_ = false;
    QString lastError_;
    qint64 lastTry_ = 0;
    QNetworkAccessManager net_;
};
