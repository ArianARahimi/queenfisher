#pragma once

#include <QObject>

// Settings that apply to the whole app, shared by the Settings tab, the Settings menu and
// the pages that use them. Read from QSettings each time, so nothing goes stale.
class AppSettings : public QObject
{
    Q_OBJECT

public:
    enum class Existing { Ask, Keep, Replace };   // a file with that name is already in the folder
    static constexpr int maxAtOnce = 10;

    static AppSettings &get();

    int downloadsAtOnce() const;
    void setDownloadsAtOnce(int n);   // 1 to maxAtOnce
    Existing existing() const;
    void setExisting(Existing e);
    bool browserSignIn() const;       // use the default browser's sign-in when a site asks
    void setBrowserSignIn(bool on);   // off also forgets the sites allowed
    bool signInAllowed(const QString &url) const;   // sign-in on, and the link's site allowed
    void allowSignIn(const QString &url);           // remember the link's site
    static QString siteOf(const QString &url);      // host without "www."
    bool minimizeToTray() const;
    void setMinimizeToTray(bool on);
    bool updateCheck() const;         // ask GitHub once a day for a newer Queenfisher
    void setUpdateCheck(bool on);
    QString skippedVersion() const;   // Skip This Version; a newer one shows
    void setSkippedVersion(const QString &version);

signals:
    void changed();

private:
    using QObject::QObject;
    void save(const char *key, const QVariant &value);
};
