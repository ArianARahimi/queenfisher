#include "AppUpdate.h"
#include "YtDlpEngine.h"

#include <QDateTime>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSettings>

AppUpdate::AppUpdate(QString current, QObject *parent) : QObject(parent), current_(std::move(current)) {}

QString AppUpdate::newerVersion(const QString &tag, const QString &current)
{
    static const QRegularExpression valid(R"(^v?(\d+\.\d+\.\d+)$)");
    const QRegularExpressionMatch m = valid.match(tag);
    if (!m.hasMatch())
        return {};
    const QString version = m.captured(1);
    return YtDlpEngine::compareVersions(version, current) > 0 ? version : QString();
}

void AppUpdate::checkDaily()
{
    const qint64 last = QSettings().value("appUpdateChecked").toLongLong();
    if (QDateTime::currentMSecsSinceEpoch() - last >= 24 * 3600 * 1000LL)
        checkNow();
}

void AppUpdate::checkNow()
{
    if (busy_)
        return;
    busy_ = true;
    // /releases/latest redirects to /releases/tag/<tag>, like the yt-dlp check.
    QNetworkRequest req(QUrl("https://github.com/ArianARahimi/queenfisher/releases/latest"));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "Queenfisher");
    req.setTransferTimeout(30000);
    QNetworkReply *reply = net_.head(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        busy_ = false;
        const QString target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl().path();
        if (!target.contains("/releases/tag/")) {
            emit checked(false);
            return;
        }
        QSettings().setValue("appUpdateChecked", QDateTime::currentMSecsSinceEpoch());
        newer_ = newerVersion(target.section('/', -1), current_);
        emit checked(true);
    });
}
