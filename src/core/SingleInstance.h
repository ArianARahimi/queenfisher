#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

class QLocalServer;

// What the desktop gave this launch so its window may come to the front.
struct LaunchTokens
{
    QByteArray wayland;   // XDG_ACTIVATION_TOKEN
    QByteArray x11;       // DESKTOP_STARTUP_ID
    static LaunchTokens fromEnvironment();   // read before QApplication: Qt's X11 code clears its variable
};

// One Queenfisher per user: a second launch asks the running one to show its window, then exits.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(
        const QString &name = defaultName(), const LaunchTokens &tokens = LaunchTokens::fromEnvironment(), QObject *parent = nullptr);
    bool alreadyRunning() const { return alreadyRunning_; }   // true: that one was asked to show
    static QString defaultName();   // per user; a socket in the private runtime folder on Linux and macOS
    static QString userKey();       // random, kept in this user's settings; part of the Windows name

signals:
    // The other launch's tokens, empty when it had none, so this window may take the front.
    void showRequested(const QByteArray &waylandToken, const QByteArray &startupId);

private:
    bool askRunning(const LaunchTokens &tokens) const;
    bool listen();

    QString name_;
    QLocalServer *server_ = nullptr;
    bool alreadyRunning_ = false;
};
