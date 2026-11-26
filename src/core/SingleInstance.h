#pragma once

#include <QObject>
#include <QString>

class QLocalServer;

// One Queenfisher per user: a second launch asks the running one to show its window, then exits.
class SingleInstance : public QObject
{
    Q_OBJECT

public:
    explicit SingleInstance(const QString &name = defaultName(), QObject *parent = nullptr);
    bool alreadyRunning() const { return alreadyRunning_; }   // true: that one was asked to show
    static QString defaultName();   // per user; a socket in the private runtime folder on Linux and macOS
    static QString userKey();       // random, kept in this user's settings; part of the Windows name

signals:
    // activationToken: the launcher's Wayland token, so the desktop lets the window come to the front
    void showRequested(const QString &activationToken);

private:
    bool askRunning() const;
    bool listen();

    QString name_;
    QLocalServer *server_ = nullptr;
    bool alreadyRunning_ = false;
};
