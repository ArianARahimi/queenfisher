#pragma once

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

// A helper program Queenfisher fetches itself when the computer doesn't have it: one pinned
// zip from GitHub, checked against the SHA-256 built into Queenfisher, unpacked off the
// window's thread, tried out, and only then put in place. Fetched again when a newer
// Queenfisher pins a different build; the old copy keeps working until the new one does.
class FetchedTool : public QObject
{
    Q_OBJECT

public:
    QString location() const;   // folder holding the checked programs; empty until they're there
    bool busy() const { return busy_; }
    QString lastError() const { return lastError_; }   // why it's missing; empty while a copy works
    void ensure();              // fetch if wanted and missing or old; call when idle

    // The part that doesn't need the network, public for tests: checks the zip's SHA-256,
    // unpacks it with `unzipper` (bsdtar, tar, unzip or python3) and keeps only what's needed.
    bool install(const QString &zip, const QString &expectedSha256, const QString &unzipper, QString *why);
    bool installed(const QString &sha256) const;   // the copy in place came from this zip
    bool intact() const;   // every file in place is one install() checked, and nothing else is there
    static QString exe(const QString &name);   // "deno.exe" on Windows, "deno" elsewhere
    static QString unzipper();                 // what this computer can unpack a zip with; empty if nothing

signals:
    void statusChanged(const QString &text);
    void logLine(const QString &line);
    void readyChanged();   // location() or busy() changed

protected:
    struct Source {
        QUrl zip;
        QString sha256;   // pinned: a zip with any other hash is refused
        qint64 limit;     // refuse downloads bigger than this
    };
    FetchedTool(const QString &dir, const QString &name, const QString &size, QObject *parent);
    virtual bool wanted() const = 0;                  // should Queenfisher fetch it on this computer
    virtual bool source(Source *s) const = 0;         // false: no build for this computer
    virtual bool keep(const QFileInfo &f) const = 0;  // files from the zip worth keeping
    virtual QStringList programs() const = 0;         // must be there and run
    virtual QString versionFlag() const = 0;          // "-version" or "--version"

private:
    void download(const Source &s);
    void finish(bool ok, const QString &message);
    void watch() const;   // a change to the folder or its files means checking it again

    QString dir_, name_, size_;
    mutable int intact_ = -1;   // last intact() answer; -1 = not checked since the folder changed
    mutable QFileSystemWatcher watcher_;
    bool busy_ = false;
    QString lastError_;
    qint64 lastTry_ = 0;
    int failures_ = 0;   // in a row
    QNetworkAccessManager net_;
};
