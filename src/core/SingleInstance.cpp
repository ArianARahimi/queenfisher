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
// A second launch says "show", then " wayland=<token>" and " x11=<id>" when it has them, then a line end.
const QByteArray showMessage = "show";
const QByteArray waylandKey = "wayland=";
const QByteArray x11Key = "x11=";
constexpr qsizetype maxToken = 255;
constexpr qsizetype maxMessage = 600;

// Printable, no spaces, quotes or backslashes: safe in the message and in X11's startup notices.
bool validToken(const QByteArray &token)
{
    if (token.isEmpty() || token.size() > maxToken)
        return false;
    for (char c : token)
        if (c < 0x21 || c > 0x7e || c == '"' || c == '\\')
            return false;
    return true;
}

// False when it isn't a show request; tokens that weren't sent stay empty.
bool parseShow(QByteArray message, LaunchTokens &tokens)
{
    if (message.endsWith('\n'))
        message.chop(1);
    const QList<QByteArray> words = message.split(' ');
    if (words.first() != showMessage)
        return false;
    for (qsizetype i = 1; i < words.size(); ++i) {
        const QByteArray &word = words[i];
        QByteArray *slot = word.startsWith(waylandKey) ? &tokens.wayland : word.startsWith(x11Key) ? &tokens.x11 : nullptr;
        if (!slot || !slot->isEmpty())
            return false;   // unknown or repeated
        *slot = word.mid(word.indexOf('=') + 1);
        if (!validToken(*slot))
            return false;
    }
    return true;
}
}

LaunchTokens LaunchTokens::fromEnvironment() { return {qgetenv("XDG_ACTIVATION_TOKEN"), qgetenv("DESKTOP_STARTUP_ID")}; }

SingleInstance::SingleInstance(const QString &name, const LaunchTokens &tokens, QObject *parent) : QObject(parent), name_(name)
{
    if (askRunning(tokens)) {
        alreadyRunning_ = true;
        return;
    }
    if (listen())
        return;
    // Taken: another copy may have started a moment ago, or a crash left the socket behind.
    if (askRunning(tokens)) {
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

bool SingleInstance::askRunning(const LaunchTokens &tokens) const
{
    QLocalSocket socket;
    socket.connectToServer(name_);
    if (!socket.waitForConnected(1000))
        return false;
#ifdef Q_OS_WIN
    // The user launched this, so the open Queenfisher may take the front; no other program may.
    ULONG server = 0;
    if (GetNamedPipeServerProcessId(reinterpret_cast<HANDLE>(socket.socketDescriptor()), &server))
        AllowSetForegroundWindow(server);
#endif
    QByteArray message = showMessage;
    if (validToken(tokens.wayland))
        message += ' ' + waylandKey + tokens.wayland;
    if (validToken(tokens.x11))
        message += ' ' + x11Key + tokens.x11;
    socket.write(message + '\n');
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
                LaunchTokens tokens;
                if (parseShow(*got, tokens))
                    emit showRequested(tokens.wayland, tokens.x11);
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
