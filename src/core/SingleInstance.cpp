#include "SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

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
    // Pipe names are shared by all accounts: with a key no other account can read, none can
    // take the name first and make every launch think Queenfisher is already running.
    return "Queenfisher-" + qEnvironmentVariable("USERNAME") + "-" + userKey();
#else
    return QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + "/queenfisher-instance";
#endif
}

QString SingleInstance::userKey()
{
    QSettings settings;   // the registry under this user on Windows
    QString key = settings.value("instanceKey").toString();
    static const QRegularExpression valid("^[0-9a-f]{32}$");
    if (!valid.match(key).hasMatch()) {
        key = QUuid::createUuid().toString(QUuid::Id128);
        settings.setValue("instanceKey", key);
        settings.sync();
    }
    return key;
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
