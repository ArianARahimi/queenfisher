#include "Theme.h"

#include <QApplication>
#include <QPainter>
#include <QPolygon>
#include <QPalette>
#include <QProxyStyle>
#include <QPushButton>
#include <QStyleFactory>
#include <QStyleOption>

namespace {

// Buttons and check boxes drawn the Windows 3.1 way; everything else is Fusion.
class Win31Style : public QProxyStyle
{
public:
    Win31Style() : QProxyStyle(QStyleFactory::create("Fusion")) {}

    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p, const QWidget *w) const override
    {
        if (pe == PE_PanelButtonCommand) {
            const auto *b = qstyleoption_cast<const QStyleOptionButton *>(opt);
            const bool down = opt->state & (State_Sunken | State_On);
            const bool main = b && (b->features & QStyleOptionButton::DefaultButton);
            drawButton(*p, opt->rect, down, main);
            return;
        }
        if (pe == PE_IndicatorCheckBox) {
            const QRect r = opt->rect.adjusted(0, 0, -1, -1);
            p->fillRect(r, Qt::white);
            p->setPen(Qt::black);
            p->drawRect(r);
            if (opt->state & State_On) {  // an X, not a tick
                p->drawLine(r.topLeft() + QPoint(2, 2), r.bottomRight() - QPoint(2, 2));
                p->drawLine(r.topRight() + QPoint(-2, 2), r.bottomLeft() + QPoint(2, -2));
            }
            return;
        }
        QProxyStyle::drawPrimitive(pe, opt, p, w);
    }

    void drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt, QPainter *p, const QWidget *w) const override
    {
        if (cc != CC_ComboBox) {
            QProxyStyle::drawComplexControl(cc, opt, p, w);
            return;
        }
        // White box with a black outline, plus a bevelled arrow button on the right.
        const bool enabled = opt->state & State_Enabled;
        const QRect r = opt->rect.adjusted(0, 0, -1, -1);
        p->fillRect(r, enabled ? Qt::white : Theme::face);
        p->setPen(Qt::black);
        p->drawRect(r);
        QRect arrow = subControlRect(cc, opt, SC_ComboBoxArrow, w);
        arrow.setTop(r.top());
        arrow.setBottom(r.bottom() + 1);
        arrow.setRight(r.right() + 1);
        drawButton(*p, arrow, opt->state & State_On, false);
        const QPoint c = arrow.center() + QPoint(1, -1);
        p->setPen(Qt::NoPen);
        p->setBrush(enabled ? Qt::black : Theme::shadow);
        p->drawPolygon(QPolygon({c + QPoint(-4, -2), c + QPoint(4, -2), c + QPoint(0, 2)}));
        p->fillRect(c.x() - 4, c.y() + 4, 9, 1, enabled ? Qt::black : Theme::shadow);  // the Windows 3.1 underline
    }

    int pixelMetric(PixelMetric m, const QStyleOption *opt, const QWidget *w) const override
    {
        switch (m) {
        case PM_ButtonShiftHorizontal:
        case PM_ButtonShiftVertical: return 1;
        case PM_IndicatorWidth:
        case PM_IndicatorHeight: return 13;
        default: return QProxyStyle::pixelMetric(m, opt, w);
        }
    }

private:
    // Black outline with clipped corners, 2 px bevel inside; the main button gets a thicker outline.
    static void drawButton(QPainter &p, QRect r, bool down, bool main)
    {
        r.adjust(0, 0, -1, -1);
        p.fillRect(r.adjusted(1, 1, 0, 0), Theme::face);
        p.setPen(Qt::black);
        p.drawLine(r.left() + 1, r.top(), r.right() - 1, r.top());
        p.drawLine(r.left() + 1, r.bottom(), r.right() - 1, r.bottom());
        p.drawLine(r.left(), r.top() + 1, r.left(), r.bottom() - 1);
        p.drawLine(r.right(), r.top() + 1, r.right(), r.bottom() - 1);
        if (main) {
            r.adjust(1, 1, -1, -1);
            p.drawRect(r);
        }
        const QRect in = r.adjusted(1, 1, -1, -1);
        if (down) {
            p.setPen(Theme::shadow);
            p.drawLine(in.topLeft(), in.topRight());
            p.drawLine(in.topLeft(), in.bottomLeft());
            return;
        }
        for (int i = 0; i < 2; ++i) {
            const QRect e = in.adjusted(i, i, -i, -i);
            p.setPen(Qt::white);
            p.drawLine(e.topLeft(), e.topRight() - QPoint(1, 0));
            p.drawLine(e.topLeft(), e.bottomLeft() - QPoint(0, 1));
            p.setPen(Theme::shadow);
            p.drawLine(e.bottomLeft(), e.bottomRight());
            p.drawLine(e.topRight(), e.bottomRight());
        }
    }
};

const char *styleSheet = R"(
QWidget#central { background: #c0c0c0; }
QLabel#strip {
    background: #000080; color: #ffffff; font-weight: bold;
    padding: 3px 6px; qproperty-alignment: AlignCenter;
}
QLabel#meta { color: #404040; }
QLabel#status {
    padding: 2px 6px;
    border: 1px solid; border-color: #808080 #ffffff #ffffff #808080;
}
QFrame#card { background: #c0c0c0; border: 1px solid #000000; }
QLineEdit {
    color: #000000; background: #ffffff; border: 1px solid #000000;
    padding: 3px 5px; selection-background-color: #000080; selection-color: #ffffff;
}
QLineEdit:read-only { background: #e8e8e8; }
QLineEdit:disabled { color: #808080; background: #c0c0c0; }
QPushButton { font-weight: bold; }
QComboBox QAbstractItemView {
    color: #000000; background: #ffffff; border: 1px solid #000000;
    selection-background-color: #000080; selection-color: #ffffff;
}
QTreeWidget {
    color: #000000; background: #ffffff; border: 1px solid #000000; outline: 0;
}
QTreeWidget::item { padding: 3px 2px; }
QTreeWidget::item:selected { background: #000080; color: #ffffff; }
QHeaderView::section {
    color: #000000; font-weight: bold; padding: 3px 6px; background: #c0c0c0;
    border: none; border-right: 1px solid #808080; border-bottom: 1px solid #000000;
}
QToolTip { color: #000000; background: #ffffe1; border: 1px solid #000000; }
)";

}

namespace Theme {

void apply(QApplication &app)
{
    app.setStyle(new Win31Style);
    QPalette pal;
    pal.setColor(QPalette::Window, face);
    pal.setColor(QPalette::WindowText, Qt::black);
    pal.setColor(QPalette::Button, face);
    pal.setColor(QPalette::ButtonText, Qt::black);
    pal.setColor(QPalette::Base, Qt::white);
    pal.setColor(QPalette::AlternateBase, QColor(0xf0, 0xf0, 0xf0));
    pal.setColor(QPalette::Text, Qt::black);
    pal.setColor(QPalette::Highlight, navy);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Light, Qt::white);
    pal.setColor(QPalette::Mid, shadow);
    pal.setColor(QPalette::Dark, shadow);
    pal.setColor(QPalette::Shadow, Qt::black);
    pal.setColor(QPalette::PlaceholderText, shadow);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, shadow);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, shadow);
    pal.setColor(QPalette::Disabled, QPalette::Text, shadow);
    app.setPalette(pal);
    app.setStyleSheet(styleSheet);
}

void drawSunken(QPainter &p, const QRect &r, const QColor &fill)
{
    p.fillRect(r, fill);
    p.setPen(shadow);
    p.drawLine(r.topLeft(), r.topRight());
    p.drawLine(r.topLeft(), r.bottomLeft());
    p.setPen(Qt::white);
    p.drawLine(r.bottomLeft(), r.bottomRight());
    p.drawLine(r.topRight(), r.bottomRight());
}

void drawBlocks(QPainter &p, const QRect &r, double fraction)
{
    p.fillRect(r, empty);
    p.setPen(Qt::black);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    const QRect inner = r.adjusted(2, 2, -2, -2);
    constexpr int block = 6, gap = 2;
    const int count = (inner.width() + gap) / (block + gap);
    const int lit = fraction < 0 ? 0 : int(qMin(fraction, 1.0) * count + 0.5);
    for (int i = 0; i < lit; ++i)
        p.fillRect(inner.left() + i * (block + gap), inner.top(), block, inner.height(), navy);
}

}
