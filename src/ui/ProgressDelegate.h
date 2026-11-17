#pragma once

#include <QStyledItemDelegate>

// Draws a segmented bar from the fraction stored in Qt::UserRole (0..1, below 0 = none).
class ProgressDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
