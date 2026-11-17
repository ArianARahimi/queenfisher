#pragma once

#include <QWidget>

class Banner;
class QLabel;
class QToolButton;
class YtDlpEngine;

// The yt-dlp tab: which copy QFisher uses, how it's kept current, and buttons to act now.
class EnginePage : public QWidget
{
    Q_OBJECT

public:
    explicit EnginePage(YtDlpEngine *engine, QWidget *parent = nullptr);

private:
    void refresh();

    YtDlpEngine *engine_;
    Banner *banner_;
    QLabel *version_, *channel_, *checked_, *location_;
    QToolButton *check_, *nightly_, *stable_;
};
