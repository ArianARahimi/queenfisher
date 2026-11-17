#include "SegmentBar.h"
#include "Theme.h"

#include <QPainter>

SegmentBar::SegmentBar(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(16);
}

void SegmentBar::setFraction(double fraction)
{
    fraction_ = fraction < 0 ? -1 : qMin(fraction, 1.0);
    update();
}

void SegmentBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    Theme::drawInset(p, rect().adjusted(0, 0, -1, -1));

    const QRect inner = rect().adjusted(3, 3, -3, -3);
    constexpr int block = 6, gap = 2;
    const int count = (inner.width() + gap) / (block + gap);
    const int lit = fraction_ < 0 ? 0 : int(fraction_ * count + 0.5);
    for (int i = 0; i < count; ++i) {
        const QRect r(inner.left() + i * (block + gap), inner.top(), block, inner.height());
        p.fillRect(r, i < lit ? Theme::lcdLit : Theme::lcdUnlit);
    }
}
