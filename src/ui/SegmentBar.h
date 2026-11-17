#pragma once

#include <QWidget>

// Progress bar made of LCD blocks.
class SegmentBar : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentBar(QWidget *parent = nullptr);
    void setFraction(double fraction);   // 0..1, below 0 = idle
    QSize sizeHint() const override { return {200, 16}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double fraction_ = -1;
};
