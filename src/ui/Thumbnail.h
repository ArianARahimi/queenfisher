#pragma once

#include <QImage>
#include <QPixmap>
#include <QWidget>

class QMediaPlayer;
class QVideoSink;

// 16:9 preview picture in a sunken frame. When a clip is available, clicking plays it
// silently on a loop inside the same frame; clicking again goes back to the picture.
class Thumbnail : public QWidget
{
    Q_OBJECT

public:
    explicit Thumbnail(QWidget *parent = nullptr);
    void setPicture(const QPixmap &picture, const QString &badge = {});
    void setHint(const QString &hint);         // shown when there's no picture; stops any clip
    void setClipAvailable(bool available);     // shows the play button
    void setLoadingClip(const QString &text);  // "Loading clip..." over the picture
    void playClip(const QString &file);
    void stopClip();
    bool playing() const;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void drawPlayButton(class QPainter &p, const QRect &area) const;

    QPixmap picture_;
    QImage frame_;          // current clip frame
    QString badge_;
    QString hint_ = "Paste a link to see a preview";
    QString overlay_;       // loading or error text over the picture
    bool clipAvailable_ = false;
    QMediaPlayer *player_ = nullptr;
    QVideoSink *sink_ = nullptr;
};
