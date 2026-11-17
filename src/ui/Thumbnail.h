#pragma once

#include <QPixmap>
#include <QWidget>

// Preview picture. Grows and shrinks with the window; the picture keeps its shape, with the
// length badge in its corner.
class Thumbnail : public QWidget
{
    Q_OBJECT

public:
    explicit Thumbnail(QWidget *parent = nullptr);
    void setPicture(const QPixmap &picture, const QString &badge = {});
    void setHint(const QString &hint);   // shown when there's no picture
    QSize sizeHint() const override { return {384, 216}; }
    QSize minimumSizeHint() const override { return {192, 108}; }

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    QRect area() const { return rect().adjusted(2, 2, -2, -2); }
    void rescale();

    QPixmap original_;
    QPixmap scaled_;   // original_ fitted to the current size, redone only when that changes
    QString badge_;
    QString hint_ = "Paste a link to see a preview";
};
