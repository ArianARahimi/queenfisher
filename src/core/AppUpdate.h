#pragma once

#include <QNetworkAccessManager>
#include <QObject>

// Asks GitHub whether a newer Queenfisher is out. It only looks: nothing is downloaded or
// installed, the user gets a link to the download page instead.
class AppUpdate : public QObject
{
    Q_OBJECT

public:
    explicit AppUpdate(QString current, QObject *parent = nullptr);

    void checkDaily();   // at most once a day
    void checkNow();
    QString newer() const { return newer_; }   // version found, empty if none

    static inline const QString downloadPage = "https://composersoftware.net/projects/queenfisher.html";
    // "v0.2.0" against "0.1.0": the version without the v if it's newer, else empty. Public for tests.
    static QString newerVersion(const QString &tag, const QString &current);

signals:
    void checked(bool ok);   // ok: GitHub answered; newer() says what it found

private:
    QString current_;
    QString newer_;
    bool busy_ = false;
    QNetworkAccessManager net_;
};
