#pragma once

#include <QMainWindow>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class Thumbnail;

// Link box, preview card with download options, then the download list.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QWidget *buildCard();
    void linkChanged(const QString &text);
    void chooseFolder();
    void download();
    void refreshStatus();

    QLineEdit *link_ = nullptr;
    Thumbnail *thumbnail_ = nullptr;
    QLabel *title_ = nullptr;
    QLabel *meta_ = nullptr;
    QComboBox *format_ = nullptr;
    QComboBox *quality_ = nullptr;
    QLineEdit *folder_ = nullptr;
    QCheckBox *subtitles_ = nullptr;
    QCheckBox *playlist_ = nullptr;
    QPushButton *download_ = nullptr;
    QTreeWidget *list_ = nullptr;
    QLabel *status_ = nullptr;
};
