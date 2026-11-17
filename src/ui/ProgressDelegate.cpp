#include "ProgressDelegate.h"
#include "Theme.h"

#include <QPainter>

void ProgressDelegate::paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QStyledItemDelegate::paint(p, option, index);  // row background and selection
    const double fraction = index.data(Qt::UserRole).toDouble();
    const QRect bar = option.rect.adjusted(4, (option.rect.height() - 12) / 2, -4, 0);
    p->save();
    Theme::drawBlocks(*p, QRect(bar.topLeft(), QSize(bar.width(), 12)), fraction);
    p->restore();
}

QSize ProgressDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    return QStyledItemDelegate::sizeHint(option, index).expandedTo({150, 22});
}
