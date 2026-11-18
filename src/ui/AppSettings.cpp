#include "AppSettings.h"

#include <QSettings>
#include <algorithm>

AppSettings &AppSettings::get()
{
    static AppSettings settings;
    return settings;
}

void AppSettings::save(const char *key, const QVariant &value)
{
    QSettings s;
    if (s.value(key) == value)
        return;
    s.setValue(key, value);
    emit changed();
}

int AppSettings::downloadsAtOnce() const
{
    return std::clamp(QSettings().value("atOnce", maxAtOnce).toInt(), 1, maxAtOnce);
}

void AppSettings::setDownloadsAtOnce(int n)
{
    save("atOnce", std::clamp(n, 1, maxAtOnce));
}

AppSettings::Existing AppSettings::existing() const
{
    const QString v = QSettings().value("existing", "ask").toString();
    return v == "keep" ? Existing::Keep : v == "replace" ? Existing::Replace : Existing::Ask;
}

void AppSettings::setExisting(Existing e)
{
    save("existing", e == Existing::Keep ? "keep" : e == Existing::Replace ? "replace" : "ask");
}

bool AppSettings::browserSignIn() const { return QSettings().value("browserSignIn", true).toBool(); }
void AppSettings::setBrowserSignIn(bool on) { save("browserSignIn", on); }

bool AppSettings::minimizeToTray() const { return QSettings().value("minimizeToTray", true).toBool(); }
void AppSettings::setMinimizeToTray(bool on) { save("minimizeToTray", on); }

bool AppSettings::updateCheck() const { return QSettings().value("updateCheck", true).toBool(); }
void AppSettings::setUpdateCheck(bool on) { save("updateCheck", on); }

QString AppSettings::skippedVersion() const { return QSettings().value("skippedVersion").toString(); }
void AppSettings::setSkippedVersion(const QString &version) { save("skippedVersion", version); }
