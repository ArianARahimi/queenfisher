#pragma once

#include <QStyledItemDelegate>

// Draws the progress bar: fraction 0..1 in Qt::UserRole, Theme::BarState in stateRole.
class ProgressDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    static constexpr int stateRole = Qt::UserRole + 1;
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
