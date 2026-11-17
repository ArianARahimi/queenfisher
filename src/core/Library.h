#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

// Finished downloads, remembered between runs in a small JSON file.
class Library : public QObject
{
    Q_OBJECT

public:
    struct Entry
    {
        QString title, url, file, format;
        qint64 bytes = 0;
        QDateTime finished;
    };

    explicit Library(QString file, QObject *parent = nullptr);
    const QList<Entry> &entries() const { return entries_; }   // newest first
    void add(const Entry &entry);
    void remove(int index);
    void forgetMissing();   // drops entries whose file was moved or deleted

signals:
    void changed();

private:
    void load();
    void save() const;

    QString file_;
    QList<Entry> entries_;
};
