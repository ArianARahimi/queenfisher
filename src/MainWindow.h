#pragma once

#include <QMainWindow>

class QLineEdit;
class QPushButton;
class QTreeWidget;

// Link box on top, download queue below.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void addLink();

    QLineEdit *link_ = nullptr;
    QPushButton *add_ = nullptr;
    QTreeWidget *queue_ = nullptr;
};
