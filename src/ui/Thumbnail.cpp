#include "Thumbnail.h"
#include "Theme.h"

#include <QPainter>

Thumbnail::Thumbnail(QWidget *parent) : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void Thumbnail::setPicture(const QPixmap &picture, const QString &badge)
{
    original_ = picture;
    badge_ = badge;
    rescale();
    update();
}

void Thumbnail::setHint(const QString &hint)
{
    original_ = scaled_ = {};
    badge_.clear();
    hint_ = hint;
    update();
}

void Thumbnail::setBadge(const QString &badge)
{
    badge_ = badge;
    update();
}

void Thumbnail::resizeEvent(QResizeEvent *)
{
    rescale();
}

void Thumbnail::rescale()
{
    if (original_.isNull())
        return;
    const qreal dpr = devicePixelRatioF();
    scaled_ = original_.scaled(area().size() * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled_.setDevicePixelRatio(dpr);
}

void Thumbnail::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    p.setPen(Theme::outline);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    if (scaled_.isNull()) {
        p.setPen(Theme::dim);
        p.drawText(area(), Qt::AlignCenter | Qt::TextWordWrap, hint_);
        return;
    }
    QRect picture(QPoint(0, 0), scaled_.deviceIndependentSize().toSize());
    picture.moveCenter(area().center());
    p.drawPixmap(picture.topLeft(), scaled_);

    // Length badge in the picture's corner, like video sites show it.
    if (!badge_.isEmpty()) {
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QRect text = p.fontMetrics().boundingRect(badge_).adjusted(-5, -2, 5, 2);
        const QRect badge = text.translated(picture.bottomRight() - text.bottomRight() - QPoint(6, 6));
        p.fillRect(badge, QColor(0, 0, 0, 200));
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, badge_);
    }
}
