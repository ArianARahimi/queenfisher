#pragma once

#include <QWidget>

class Library;
class QLineEdit;
class QToolButton;
class QTreeWidget;

// The Library tab: everything QFisher has finished downloading, newest first.
class LibraryPage : public QWidget
{
    Q_OBJECT

public:
    explicit LibraryPage(Library *library, QWidget *parent = nullptr);

private:
    void refill();
    void filter(const QString &text);
    int selectedIndex() const;
    void refreshButtons();

    Library *library_;
    QLineEdit *search_;
    QTreeWidget *list_;
    QToolButton *launch_, *explore_, *forget_;
};
