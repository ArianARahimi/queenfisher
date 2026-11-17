#include "Theme.h"

#include <QApplication>
#include <QFontDatabase>
#include <QPainter>
#include <QPalette>
#include <QStyleFactory>

namespace {

QString family = "monospace";

// %1 = pixel font family.
const char *styleSheet = R"(
QMainWindow, QWidget#central {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #474b5a, stop:1 #2a2c36);
}
QPushButton {
    font-family: "%1"; font-size: 16px;
    color: #e4e4e4;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #626678, stop:1 #393c48);
    border: 1px solid; border-color: #8a8fa4 #121318 #121318 #8a8fa4;
    padding: 4px 10px; min-height: 18px;
}
QPushButton:hover { color: #ffff55; }
QPushButton:pressed {
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #34363f, stop:1 #4b4e5c);
    border-color: #121318 #8a8fa4 #8a8fa4 #121318;
    padding: 5px 9px 3px 11px;
}
QPushButton:disabled { color: #7a7d88; }
QLineEdit {
    color: #55ff55; background: #050905; selection-background-color: #0000aa;
    border: 2px solid; border-color: #121318 #868ba0 #868ba0 #121318;
    padding: 4px 6px; font-size: 14px;
}
QTreeWidget {
    color: #55ff55; background: #000000; alternate-background-color: #050c05;
    border: 2px solid; border-color: #121318 #868ba0 #868ba0 #121318;
    font-size: 14px; outline: 0;
}
QTreeWidget::item { padding: 3px 2px; }
QTreeWidget::item:selected { background: #0000aa; color: #ffffff; }
QLabel#status { font-family: "%1"; font-size: 16px; color: #b8bccb; }
QScrollBar:vertical { background: #1b1c22; width: 14px; margin: 0; }
QScrollBar::handle:vertical {
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #626678, stop:1 #393c48);
    border: 1px solid; border-color: #8a8fa4 #121318 #121318 #8a8fa4; min-height: 24px;
}
QScrollBar::add-line, QScrollBar::sub-line { height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
)";

}

namespace Theme {

void apply(QApplication &app)
{
    const int id = QFontDatabase::addApplicationFont(":/fonts/Px437_IBM_VGA_8x16.ttf");
    if (id >= 0)
        family = QFontDatabase::applicationFontFamilies(id).value(0, family);

    // Fusion draws the same everywhere, so the style sheet looks alike on all three systems.
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette pal = app.palette();
    pal.setColor(QPalette::Window, QColor(0x33, 0x35, 0x40));
    pal.setColor(QPalette::WindowText, QColor(0xe4, 0xe4, 0xe4));
    pal.setColor(QPalette::Base, Qt::black);
    pal.setColor(QPalette::Text, lcdLit);
    pal.setColor(QPalette::Highlight, QColor(0, 0, 0xaa));
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::ToolTipBase, Qt::black);
    pal.setColor(QPalette::ToolTipText, lcdLit);
    app.setPalette(pal);
    app.setStyleSheet(QString(styleSheet).arg(family));
}

QFont pixelFont()
{
    QFont f(family);
    f.setPixelSize(16);
    f.setStyleStrategy(QFont::NoAntialias);
    f.setHintingPreference(QFont::PreferNoHinting);
    return f;
}

void drawInset(QPainter &p, const QRect &r)
{
    p.fillRect(r, lcdBack);
    p.setPen(bevelDark);
    p.drawLine(r.topLeft(), r.topRight());
    p.drawLine(r.topLeft(), r.bottomLeft());
    p.setPen(bevelLight);
    p.drawLine(r.bottomLeft(), r.bottomRight());
    p.drawLine(r.topRight(), r.bottomRight());
}

}
