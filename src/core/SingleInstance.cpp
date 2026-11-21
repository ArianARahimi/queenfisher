#include "SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>

#include <memory>

namespace {
const QByteArray showMessage = "show";   // the only thing a second launch may say
}

SingleInstance::SingleInstance(const QString &name, QObject *parent) : QObject(parent), name_(name)
{
    if (askRunning()) {
        alreadyRunning_ = true;
        return;
    }
    if (listen())
        return;
    // Taken: another copy may have started a moment ago, or a crash left the socket behind.
    if (askRunning()) {
        alreadyRunning_ = true;
        return;
    }
    QLocalServer::removeServer(name_);
    listen();
}

QString SingleInstance::defaultName()
{
#ifdef Q_OS_WIN
    return "Queenfisher-" + qEnvironmentVariable("USERNAME");
#else
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/queenfisher-instance";
#endif
}

bool SingleInstance::askRunning() const
{
    QLocalSocket socket;
    socket.connectToServer(name_);
    if (!socket.waitForConnected(1000))
        return false;
    socket.write(showMessage);
    const bool sent = socket.waitForBytesWritten(1000);
    socket.disconnectFromServer();
    return sent;
}

bool SingleInstance::listen()
{
    delete server_;
    server_ = new QLocalServer(this);
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    if (!server_->listen(name_))
        return false;
    connect(server_, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = server_->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            auto got = std::make_shared<QByteArray>();
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, got] {
                got->append(socket->readAll());
                if (*got == showMessage)
                    emit showRequested();
                if (got->size() >= showMessage.size())
                    socket->disconnectFromServer();   // said its word, or something else: done either way
            });
        }
    });
    return true;
}
