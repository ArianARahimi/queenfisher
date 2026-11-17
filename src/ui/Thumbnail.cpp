#include "Thumbnail.h"
#include "Theme.h"

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QUrl>
#include <QVideoFrame>
#include <QVideoSink>

Thumbnail::Thumbnail(QWidget *parent) : QWidget(parent)
{
    setFixedSize(386, 218);  // 384x216 picture + frame

    player_ = new QMediaPlayer(this);
    sink_ = new QVideoSink(this);
    audio_ = new QAudioOutput(this);
    audio_->setVolume(0.7f);
    player_->setVideoSink(sink_);
    player_->setAudioOutput(audio_);
    connect(player_, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus s) {
        if (s == QMediaPlayer::EndOfMedia)   // played once: back to the picture, after the player is done
            QMetaObject::invokeMethod(this, &Thumbnail::stopClip, Qt::QueuedConnection);
    });
    connect(sink_, &QVideoSink::videoFrameChanged, this, [this](const QVideoFrame &f) {
        frame_ = f.toImage();
        update();
    });
    connect(player_, &QMediaPlayer::errorOccurred, this, [this] {
        stopClip();
        setLoadingClip("Couldn't play the clip");
    });
}

void Thumbnail::setPicture(const QPixmap &picture, const QString &badge)
{
    stopClip();
    picture_ = picture;
    badge_ = badge;
    overlay_.clear();
    update();
}

void Thumbnail::setHint(const QString &hint)
{
    stopClip();
    picture_ = {};
    hint_ = hint;
    overlay_.clear();
    setClipAvailable(false);
}

void Thumbnail::setClipAvailable(bool available)
{
    clipAvailable_ = available;
    setCursor(available ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setToolTip(available ? "Click to play a short clip with sound" : QString());
    update();
}

void Thumbnail::setLoadingClip(const QString &text)
{
    overlay_ = text;
    update();
}

void Thumbnail::playClip(const QString &file)
{
    overlay_.clear();
    player_->setSource(QUrl::fromLocalFile(file));
    player_->play();
    setToolTip("Click to stop");
}

void Thumbnail::stopClip()
{
    if (!player_->source().isEmpty()) {
        player_->stop();
        player_->setSource({});
    }
    frame_ = {};
    if (clipAvailable_)
        setToolTip("Click to play a short clip with sound");
    update();
}

bool Thumbnail::playing() const
{
    return !player_->source().isEmpty();
}

void Thumbnail::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && clipAvailable_ && rect().contains(event->position().toPoint()))
        emit clicked();
}

void Thumbnail::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.setPen(Theme::outline);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    const QRect area = rect().adjusted(2, 2, -2, -2);

    if (picture_.isNull() && frame_.isNull()) {
        p.setPen(Theme::dim);
        p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, hint_);
        return;
    }

    p.setClipRect(area);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    if (!frame_.isNull()) {
        // Clip frames keep their shape, with black bars if they aren't 16:9.
        QRect target(QPoint(0, 0), frame_.size().scaled(area.size(), Qt::KeepAspectRatio));
        target.moveCenter(area.center());
        p.drawImage(target, frame_);
    } else {
        const QPixmap scaled = picture_.scaled(area.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        p.drawPixmap(area.center() - scaled.rect().center(), scaled);
    }

    QFont f = font();
    f.setBold(true);
    p.setFont(f);
    // Length badge in the corner, like video sites show it.
    if (!badge_.isEmpty() && frame_.isNull()) {
        const QRect text = p.fontMetrics().boundingRect(badge_).adjusted(-5, -2, 5, 2);
        const QRect badge = text.translated(area.bottomRight() - text.bottomRight() - QPoint(6, 6));
        p.fillRect(badge, QColor(0, 0, 0, 200));
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, badge_);
    }
    if (!overlay_.isEmpty()) {
        p.fillRect(area, QColor(0, 0, 0, 150));
        p.setPen(Qt::white);
        p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, overlay_);
    } else if (clipAvailable_ && frame_.isNull()) {
        drawPlayButton(p, area);
    }
}

// A glossy round play button, bottom left, like the media buttons of the time.
void Thumbnail::drawPlayButton(QPainter &p, const QRect &area) const
{
    const QRect b(area.left() + 8, area.bottom() - 8 - 30, 30, 30);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(Theme::outline, 1));
    p.setBrush(Qt::NoBrush);
    Theme::drawGloss(p, b, QColor(0x2d, 0x6b, 0xc8), 15);
    p.drawEllipse(QRectF(b).adjusted(0.5, 0.5, -0.5, -0.5));
    const QPointF c = QRectF(b).center() + QPointF(1.5, 0);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    p.drawPolygon(QPolygonF({c + QPointF(-5, -7), c + QPointF(7, 0), c + QPointF(-5, 7)}));
}
