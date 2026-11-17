#pragma once

#include <QObject>
#include <QString>

#include <functional>

// Whatever runs the downloads. Downloads only talk to this, so the program behind it
// (QFisher's own yt-dlp, a system copy, a test fake) can change without touching them.
class Engine : public QObject
{
    Q_OBJECT

public:
    using Done = std::function<void(bool changed, const QString &message)>;
    using QObject::QObject;

    virtual QString path() const = 0;      // program to run, empty if none yet
    virtual QString version() const = 0;   // for the log and status line
    virtual bool busy() const = 0;         // installing or updating right now

    // Called when a download fails in a way a newer engine might fix. Reports whether
    // something changed (newer version, other channel); the caller retries only then.
    virtual void repair(Done done) = 0;

signals:
    void statusChanged(const QString &text);
    void logLine(const QString &line);
    void readyChanged();   // path(), version() or busy() changed
};
