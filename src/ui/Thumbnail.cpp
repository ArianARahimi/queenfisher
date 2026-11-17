#include "Thumbnail.h"
#include "Theme.h"

#include <QPainter>

Thumbnail::Thumbnail(QWidget *parent) : QWidget(parent)
{
    setFixedSize(292, 166);  // 288x162 picture + frame
}

void Thumbnail::setPicture(const QPixmap &picture, const QString &duration)
{
    picture_ = picture;
    duration_ = duration;
    update();
}

void Thumbnail::setHint(const QString &hint)
{
    picture_ = {};
    hint_ = hint;
    update();
}

void Thumbnail::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    Theme::drawSunken(p, rect().adjusted(0, 0, -1, -1), Qt::black);
    const QRect area = rect().adjusted(2, 2, -2, -2);

    if (picture_.isNull()) {
        p.setPen(QColor(0xc0, 0xc0, 0xc0));
        p.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, hint_);
        return;
    }

    p.setRenderHint(QPainter::SmoothPixmapTransform);
    const QPixmap scaled = picture_.scaled(area.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    p.setClipRect(area);
    p.drawPixmap(area.center() - scaled.rect().center(), scaled);

    // Length badge in the corner, like video sites show it.
    if (!duration_.isEmpty()) {
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        const QRect text = p.fontMetrics().boundingRect(duration_).adjusted(-5, -2, 5, 2);
        const QRect badge = text.translated(area.bottomRight() - text.bottomRight() - QPoint(6, 6));
        p.fillRect(badge, QColor(0, 0, 0, 200));
        p.setPen(Qt::white);
        p.drawText(badge, Qt::AlignCenter, duration_);
    }
}
