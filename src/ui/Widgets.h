#pragma once

#include <QFrame>
#include <QToolButton>

class QAbstractItemView;
class QListWidget;
class QLabel;
class QLayout;

// Small building blocks of the Vista-era layout.
namespace Widgets {

// Sidebar box with a grey header strip in lime text, like the filter boxes of 2008 file-sharing apps.
QFrame *sideBox(const QString &title, QLayout *content);

// Big icon with its label underneath, for the rows of actions under each list.
QToolButton *action(const QString &icon, const QString &text, const QString &tip);

// A list where a row is picked only by pressing and releasing on that same row: dragging
// across rows, or letting go somewhere else, picks nothing. Rows light up under the mouse.
void pickOnClick(QAbstractItemView *list);

// Icon for a list row: the film for video; for audio the music note, white, lime when selected.
QIcon formatIcon(bool audio);

// A thin line between groups of rows; it can't be picked, hovered or reached with the keys.
void addDivider(QListWidget *list);

// A small grey label over a group of rows; like the divider, it can't be picked.
void addHeading(QListWidget *list, const QString &text);

// File sizes as the lists show them: "820 KB", "33.3 MB", "1.21 GB".
QString sizeText(double bytes);
QString shortPath(const QString &path);   // the home folder as ~, so long paths stay readable

}

// Grey notice bar: what Queenfisher wants you to know right now. Hidden when there's nothing.
class Banner : public QFrame
{
    Q_OBJECT

public:
    explicit Banner(QWidget *parent = nullptr);
    void setNotice(const QString &text);   // empty hides it

private:
    QLabel *text_;
};
