#include "Widgets.h"

#include <QHBoxLayout>
#include <QLabel>
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
