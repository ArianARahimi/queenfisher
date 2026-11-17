#pragma once

#include <QFrame>
#include <QToolButton>

class QLabel;
class QLayout;

// Small building blocks of the Vista-era layout.
namespace Widgets {

// Sidebar box with a lime green header strip, like the filter boxes of 2008 file-sharing apps.
QFrame *sideBox(const QString &title, QLayout *content);

// Big icon with its label underneath, for the rows of actions under each list.
QToolButton *action(const QString &icon, const QString &text, const QString &tip);

}

// Lime green notice bar with an icon: what QFisher wants you to know right now.
class Banner : public QFrame
{
    Q_OBJECT

public:
    enum Kind { Info, Warning };
    explicit Banner(QWidget *parent = nullptr);
    void setNotice(Kind kind, const QString &text);

private:
    QLabel *icon_;
    QLabel *text_;
};
