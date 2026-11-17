#pragma once

#include <QMainWindow>

class LcdDisplay;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class SegmentBar;

// Display on top, link box, transport buttons, then the download list.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void addLink();
    void refreshCounts();

    LcdDisplay *display_ = nullptr;
    SegmentBar *progress_ = nullptr;
    QLineEdit *link_ = nullptr;
    QPushButton *add_ = nullptr;
    QTreeWidget *queue_ = nullptr;
    QLabel *status_ = nullptr;
};
