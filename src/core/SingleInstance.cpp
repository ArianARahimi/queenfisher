#include "SingleInstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUuid>

#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
// A second launch says "show", or "show <token>" with its launcher's activation token, then a line end.
const QByteArray showMessage = "show";
constexpr qsizetype maxMessage = 300;

bool validToken(const QByteArray &token)
{
    if (token.isEmpty() || token.size() > 255)
        return false;
    for (char c : token)
        if (c < 0x21 || c > 0x7e)
            return false;
    return true;
}

// False when it isn't a show request; token is left empty when none was sent.
bool parseShow(QByteArray message, QString &token)
{
    if (message.endsWith('\n'))
        message.chop(1);
    if (message == showMessage)
        return true;
    if (!message.startsWith(showMessage + ' '))
        return false;
    const QByteArray sent = message.mid(showMessage.size() + 1);
    if (!validToken(sent))
        return false;
    token = QString::fromLatin1(sent);
    return true;
}
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
#ifdef Q_OS_WIN
    AllowSetForegroundWindow(ASFW_ANY);   // the user launched this, so the open window may take the front
#endif
    const QByteArray token = qgetenv("XDG_ACTIVATION_TOKEN");
    socket.write((validToken(token) ? showMessage + ' ' + token : showMessage) + '\n');
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
            auto done = std::make_shared<bool>(false);
            auto finish = [this, socket, got, done] {
                if (*done)
                    return;
                *done = true;
                QString token;
                if (parseShow(*got, token))
                    emit showRequested(token);
                socket->disconnectFromServer();
            };
            connect(socket, &QLocalSocket::readyRead, this, [socket, got, finish] {
                got->append(socket->readAll());
                if (got->contains('\n') || got->size() > maxMessage)
                    finish();   // said its line, or too much: done either way
            });
            connect(socket, &QLocalSocket::disconnected, this, finish);   // an older copy sends "show" with no line end
        }
    });
    return true;
}
