#include "Library.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace {
constexpr int keep = 1000;   // oldest entries drop off after this many
}

Library::Library(QString file, QObject *parent) : QObject(parent), file_(std::move(file))
{
    load();
    forgetMissing();
}

void Library::forgetMissing()
{
    const qsizetype before = entries_.size();
    entries_.removeIf([](const Entry &e) { return !QFileInfo::exists(e.file); });
    if (entries_.size() == before)
        return;
    save();
    emit changed();
}

void Library::add(const Entry &entry)
{
    entries_.removeIf([&](const Entry &e) { return e.file == entry.file; });   // one row per file, newest on top
    entries_.prepend(entry);
    while (entries_.size() > keep)
        entries_.removeLast();
    save();
    emit changed();
}

void Library::remove(int index)
{
    if (index < 0 || index >= entries_.size())
        return;
    entries_.removeAt(index);   // only forgets it; the file stays where it is
    save();
    emit changed();
}

void Library::load()
{
    QFile f(file_);
    if (!f.open(QIODevice::ReadOnly))
        return;
    for (const auto &v : QJsonDocument::fromJson(f.readAll()).array()) {
        const QJsonObject o = v.toObject();
        entries_.append({o["title"].toString(), o["url"].toString(), o["file"].toString(), o["format"].toString(),
                         qint64(o["bytes"].toDouble()), QDateTime::fromString(o["finished"].toString(), Qt::ISODate)});
    }
}

// QSaveFile writes to a temporary file first, so a crash can't leave half a library.
void Library::save() const
{
    QDir().mkpath(QFileInfo(file_).absolutePath());
    QJsonArray list;
    for (const Entry &e : entries_)
        list.append(QJsonObject{{"title", e.title}, {"url", e.url}, {"file", e.file}, {"format", e.format},
                                {"bytes", double(e.bytes)}, {"finished", e.finished.toString(Qt::ISODate)}});
    QSaveFile f(file_);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(list).toJson(QJsonDocument::Compact));
        f.commit();
    }
}
