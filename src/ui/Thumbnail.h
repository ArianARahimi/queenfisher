#pragma once

#include <QPixmap>
#include <QWidget>

// 16:9 preview picture in a sunken frame, with a hint while empty.
class Thumbnail : public QWidget
{
    Q_OBJECT

public:
    explicit Thumbnail(QWidget *parent = nullptr);
    void setPicture(const QPixmap &picture, const QString &duration = {});
    void setHint(const QString &hint);   // shown when there's no picture

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QPixmap picture_;
    QString duration_;
    QString hint_ = "Paste a link to see a preview";
};
