#include "Widgets.h"
#include "Theme.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QLabel>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace Widgets {

QFrame *sideBox(const QString &title, QLayout *content)
{
    auto *box = new QFrame;
    box->setObjectName("box");
    auto *header = new QLabel(title);
    header->setObjectName("boxHeader");
    auto *layout = new QVBoxLayout(box);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(header);
    content->setContentsMargins(6, 5, 6, 6);
    layout->addLayout(content);
    return box;
}

QToolButton *action(const QString &icon, const QString &text, const QString &tip)
{
    auto *b = new QToolButton;
    b->setObjectName("action");
    b->setIcon(QIcon(":/icons/" + icon + ".png"));
    b->setIconSize({32, 32});
    b->setText(text);
    b->setToolTip(tip);
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setMinimumWidth(72);
    return b;
}

namespace {

class PickOnClick : public QObject
{
public:
    explicit PickOnClick(QAbstractItemView *list) : QObject(list), list_(list) {}

    bool eventFilter(QObject *, QEvent *e) override
    {
        if (e->type() != QEvent::MouseButtonPress && e->type() != QEvent::MouseButtonRelease
            && e->type() != QEvent::MouseMove && e->type() != QEvent::MouseButtonDblClick)
            return false;
        auto *m = static_cast<QMouseEvent *>(e);
        const QModelIndex at = list_->indexAt(m->position().toPoint());
        if (e->type() == QEvent::MouseMove)
            return m->buttons() != Qt::NoButton;          // no picking while dragging; hover still shows
        if (m->button() != Qt::LeftButton)
            return true;                                  // only the left button picks
        if (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick)
            pressed_ = at;
        else if (at.isValid() && at == pressed_ && (at.flags() & Qt::ItemIsEnabled))
            list_->setCurrentIndex(at);                   // pressed and released on the same row
        return true;
    }

private:
    QAbstractItemView *list_;
    QPersistentModelIndex pressed_;
};

}

void pickOnClick(QAbstractItemView *list)
{
    list->setMouseTracking(true);
    list->viewport()->setAttribute(Qt::WA_Hover);
    list->viewport()->installEventFilter(new PickOnClick(list));
}

namespace {

constexpr int dividerRole = Qt::UserRole + 100;

class DividerDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (!index.data(dividerRole).toBool())
            return QStyledItemDelegate::paint(p, option, index);
        const int y = option.rect.center().y();
        p->setPen(Theme::outline);
        p->drawLine(option.rect.left() + 6, y, option.rect.right() - 6, y);
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        return index.data(dividerRole).toBool() ? QSize(0, 9) : QStyledItemDelegate::sizeHint(option, index);
    }
};

}

void addDivider(QListWidget *list)
{
    if (!dynamic_cast<DividerDelegate *>(list->itemDelegate()))
        list->setItemDelegate(new DividerDelegate(list));
    auto *line = new QListWidgetItem(list);
    line->setFlags(Qt::NoItemFlags);
    line->setData(dividerRole, true);
}

QIcon formatIcon(bool audio)
{
    if (!audio)
        return QIcon(":/icons/video.png");
    QIcon note(Theme::recolour(":/icons/audio.png", Theme::text, true));                   // white in a list
    note.addPixmap(Theme::recolour(":/icons/audio.png", Theme::accent, true), QIcon::Selected);   // lime when picked
    return note;
}

QString sizeText(double bytes)
{
    if (bytes >= 1e9) return QString::number(bytes / 1e9, 'f', 2) + " GB";
    if (bytes >= 1e6) return QString::number(bytes / 1e6, 'f', 1) + " MB";
    return QString::number(bytes / 1e3, 'f', 0) + " KB";
}

}

Banner::Banner(QWidget *parent) : QFrame(parent)
{
    setObjectName("banner");
    text_ = new QLabel;
    text_->setWordWrap(true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(6, 3, 6, 3);
    layout->addWidget(text_, 1);
    setVisible(false);
}

void Banner::setNotice(const QString &text)
{
    text_->setText(text);
    setToolTip(text);
    setVisible(!text.isEmpty());
}
