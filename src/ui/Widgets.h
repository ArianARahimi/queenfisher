#pragma once

#include <QFrame>
#include <QToolButton>

class QLabel;
class QLayout;

// Small building blocks of the Vista-era layout.
namespace Widgets {

// Sidebar box with a grey header strip in lime text, like the filter boxes of 2008 file-sharing apps.
QFrame *sideBox(const QString &title, QLayout *content);

// Big icon with its label underneath, for the rows of actions under each list.
QToolButton *action(const QString &icon, const QString &text, const QString &tip);

// Icon for a list row: the film for video, the music note redrawn in the theme's lime for audio.
QIcon formatIcon(bool audio);

// File sizes as the lists show them: "820 KB", "33.3 MB", "1.21 GB".
QString sizeText(double bytes);

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
