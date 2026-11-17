#pragma once

#include <QWidget>

// The About tab: what QFisher is, its licence and credits.
class AboutPage : public QWidget
{
    Q_OBJECT

public:
    explicit AboutPage(QWidget *parent = nullptr);
};
