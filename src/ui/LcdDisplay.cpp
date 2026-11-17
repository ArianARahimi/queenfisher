#include "LcdDisplay.h"
#include "Theme.h"

#include <QFontMetrics>
#include <QPainter>

namespace {

constexpr int graphBars = 20;
constexpr int textLeft = 128;    // after the big digits
constexpr int graphWidth = 104;
const QString separator = "  ***  ";

// Segments a-g per digit, bit 0 = a.
constexpr unsigned char segmentMap[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};

}

LcdDisplay::LcdDisplay(QWidget *parent) : QWidget(parent)
{
    setFixedHeight(92);
    setMinimumWidth(480);
    scrollTimer_.setInterval(45);
    connect(&scrollTimer_, &QTimer::timeout, this, [this] { ++scroll_; update(); });
}

void LcdDisplay::setTitle(const QString &title)
{
    title_ = title;
    scroll_ = 0;
    updateScrolling();
    update();
}

void LcdDisplay::setPercent(int percent) { percent_ = qMin(percent, 100); update(); }
void LcdDisplay::setDetail(const QString &line) { detail_ = line; update(); }

void LcdDisplay::setIndicators(bool video, bool audio, int queued)
{
    video_ = video;
    audio_ = audio;
    queued_ = queued;
    update();
}

void LcdDisplay::addSpeedSample(double bytesPerSecond)
{
    speeds_.append(qMax(0.0, bytesPerSecond));
    if (speeds_.size() > graphBars)
        speeds_.removeFirst();
    update();
}

// Only animate while the title doesn't fit, so an idle window costs nothing.
void LcdDisplay::updateScrolling()
{
    const int room = width() - textLeft - graphWidth - 24;
    const bool tooLong = QFontMetrics(Theme::pixelFont()).horizontalAdvance(title_) > room;
    if (tooLong && !scrollTimer_.isActive())
        scrollTimer_.start();
    else if (!tooLong)
        scrollTimer_.stop();
}

void LcdDisplay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    const QRect panel = rect().adjusted(0, 0, -1, -1);
    Theme::drawInset(p, panel);

    // Big percentage: three digits plus a small % sign.
    const QRect digits(10, 12, 96, 44);
    const int value = percent_;
    for (int i = 0; i < 3; ++i) {
        int d = -1;
        if (value >= 0) {
            const int place = i == 0 ? 100 : i == 1 ? 10 : 1;
            d = (value / place) % 10;
            if ((i == 0 && value < 100) || (i == 1 && value < 10))
                d = -2;  // blank leading zero
        }
        drawDigit(p, QRect(digits.left() + i * 32, digits.top(), 26, digits.height()), d);
    }
    p.setFont(Theme::pixelFont());
    p.setPen(value >= 0 ? Theme::lcdLit : Theme::lcdUnlit);
    p.drawText(QPoint(digits.right() + 4, digits.bottom()), "%");

    // Indicator lights under the digits.
    auto light = [&](int x, const QString &label, bool on) {
        p.setPen(on ? Theme::lcdHot : Theme::lcdUnlit);
        p.drawText(QPoint(x, 80), label);
    };
    light(10, "VID", video_);
    light(46, "AUD", audio_);
    p.setPen(queued_ > 0 ? Theme::lcdLit : Theme::lcdUnlit);
    p.drawText(QPoint(82, 80), QString("Q%1").arg(qMin(queued_, 99), 2, 10, QChar('0')));

    // Title (scrolls) and detail lines.
    const QRect text(textLeft, 10, width() - textLeft - graphWidth - 24, 20);
    p.save();
    p.setClipRect(text);
    p.setPen(Theme::lcdLit);
    const QFontMetrics fm(Theme::pixelFont());
    if (scrollTimer_.isActive()) {
        const QString loop = title_ + separator;
        const int span = fm.horizontalAdvance(loop);
        const int x = text.left() - (scroll_ % span);
        p.drawText(QPoint(x, text.top() + 14), loop + loop);
    } else {
        p.drawText(QPoint(text.left(), text.top() + 14), title_);
    }
    p.restore();

    p.setPen(Theme::lcdLit.darker(130));
    p.drawText(QRect(textLeft, 38, text.width(), 20), Qt::AlignLeft | Qt::AlignVCenter,
               fm.elidedText(detail_, Qt::ElideRight, text.width()));

    // Thin rule between the text and the lights, like a printed panel line.
    p.setPen(Theme::lcdUnlit);
    p.drawLine(textLeft, 66, text.right(), 66);
    p.setPen(Theme::lcdLit.darker(170));
    p.drawText(QPoint(textLeft, 80), "SPEED");

    drawGraph(p, QRect(width() - graphWidth - 12, 12, graphWidth, 70));
}

void LcdDisplay::drawDigit(QPainter &p, QRect r, int digit) const
{
    constexpr int t = 5;  // segment thickness
    const int w = r.width(), h = r.height(), mid = h / 2;
    const QRect seg[7] = {
        {t, 0, w - 2 * t, t},                 // a top
        {w - t, t, t, mid - t - 1},           // b upper right
        {w - t, mid + 1, t, mid - t - 1},     // c lower right
        {t, h - t, w - 2 * t, t},             // d bottom
        {0, mid + 1, t, mid - t - 1},         // e lower left
        {0, t, t, mid - t - 1},               // f upper left
        {t, mid - t / 2, w - 2 * t, t},       // g middle
    };
    const unsigned bits = digit >= 0 ? segmentMap[digit] : digit == -1 ? 0x40 : 0;
    for (int i = 0; i < 7; ++i)
        p.fillRect(seg[i].translated(r.topLeft()), (bits >> i) & 1 ? Theme::lcdLit : Theme::lcdUnlit);
}

// Recent speeds as block bars, scaled to the fastest one shown.
void LcdDisplay::drawGraph(QPainter &p, const QRect &r) const
{
    constexpr int barW = 4, gapW = 1, blockH = 2, gapH = 1;
    const int rows = r.height() / (blockH + gapH);
    double top = 1;
    for (double s : speeds_)
        top = qMax(top, s);

    for (int i = 0; i < graphBars; ++i) {
        const int sample = i - (graphBars - int(speeds_.size()));
        const double v = sample >= 0 ? speeds_[sample] / top : 0;
        const int lit = int(v * rows + 0.5);
        const int x = r.left() + i * (barW + gapW);
        for (int row = 0; row < rows; ++row) {
            const int y = r.bottom() - (row + 1) * (blockH + gapH) + gapH;
            QColor c = Theme::lcdUnlit;
            if (row < lit)
                c = row > rows * 3 / 4 ? Theme::lcdHot : Theme::lcdLit;
            p.fillRect(x, y, barW, blockH, c);
        }
    }
}
