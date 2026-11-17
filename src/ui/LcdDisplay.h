#pragma once

#include <QTimer>
#include <QVector>
#include <QWidget>

// The black display on top: big percentage, scrolling title, speed graph.
class LcdDisplay : public QWidget
{
    Q_OBJECT

public:
    explicit LcdDisplay(QWidget *parent = nullptr);

    void setTitle(const QString &title);        // scrolls when too long
    void setPercent(int percent);               // below 0 shows dashes
    void setDetail(const QString &line);        // speed, size, time left
    void setIndicators(bool video, bool audio, int queued);
    void addSpeedSample(double bytesPerSecond); // feeds the graph, call about once a second

    QSize sizeHint() const override { return {640, 92}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override { updateScrolling(); }

private:
    void drawDigit(class QPainter &p, QRect r, int digit) const;  // 7 segments, -1 = dash
    void drawGraph(QPainter &p, const QRect &r) const;
    void updateScrolling();

    QString title_ = "QFISHER  READY";
    QString detail_ = "Paste a link below to start";
    int percent_ = -1;
    bool video_ = false, audio_ = false;
    int queued_ = 0;
    QVector<double> speeds_;
    int scroll_ = 0;
    QTimer scrollTimer_;
};
