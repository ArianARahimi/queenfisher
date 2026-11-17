#include "Theme.h"

#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPolygon>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>

namespace {

// Buttons, check boxes and drop-downs drawn the Windows 3.1 way; everything else is Fusion.
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
            p->fillRect(r, Theme::field);
            p->setPen(Theme::outline);
            p->drawRect(r);
            if (opt->state & State_On) {  // an X, not a tick
                p->setPen(Theme::text);
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
        // Text box with a black outline, plus a bevelled arrow button on the right.
        const bool enabled = opt->state & State_Enabled;
        const QRect r = opt->rect.adjusted(0, 0, -1, -1);
        p->fillRect(r, enabled ? Theme::field : Theme::face);
        p->setPen(Theme::outline);
        p->drawRect(r);
        QRect arrow = subControlRect(cc, opt, SC_ComboBoxArrow, w);
        arrow.setTop(r.top());
        arrow.setBottom(r.bottom() + 1);
        arrow.setRight(r.right() + 1);
        drawButton(*p, arrow, opt->state & State_On, false);
        const QColor ink = enabled ? Theme::text : Theme::dim;
        const QPoint c = arrow.center() + QPoint(1, -1);
        p->setPen(Qt::NoPen);
        p->setBrush(ink);
        p->drawPolygon(QPolygon({c + QPoint(-4, -2), c + QPoint(4, -2), c + QPoint(0, 2)}));
        p->fillRect(c.x() - 4, c.y() + 4, 9, 1, ink);  // the Windows 3.1 underline
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
        p.setPen(Theme::outline);
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
            p.setPen(Theme::light);
            p.drawLine(e.topLeft(), e.topRight() - QPoint(1, 0));
            p.drawLine(e.topLeft(), e.bottomLeft() - QPoint(0, 1));
            p.setPen(Theme::shadow);
            p.drawLine(e.bottomLeft(), e.bottomRight());
            p.drawLine(e.topRight(), e.bottomRight());
        }
    }
};

// @names are swapped for the Theme colours in apply().
const char *styleSheet = R"(
QWidget#central { background: @face; }
QLabel { color: @text; }
QLabel#strip {
    background: @strip; color: #ffffff; font-weight: bold;
    padding: 3px 6px; qproperty-alignment: AlignCenter;
}
QLabel#meta { color: @dim; }
QLabel#status {
    color: @dim; padding: 2px 6px;
    border: 1px solid; border-color: @shadow @light @light @shadow;
}
QFrame#card { background: @face; border: 1px solid @outline; }
QLineEdit {
    color: @text; background: @field; border: 1px solid @outline;
    padding: 3px 5px; selection-background-color: @select; selection-color: #ffffff;
}
QLineEdit:read-only { background: @fieldAlt; }
QLineEdit:disabled { color: @dim; background: @face; }
QPushButton { font-weight: bold; }
QCheckBox { color: @text; }
QComboBox QAbstractItemView {
    color: @text; background: @field; border: 1px solid @outline;
    selection-background-color: @select; selection-color: #ffffff;
}
QTreeWidget {
    color: @text; background: @field; alternate-background-color: @fieldAlt;
    border: 1px solid @outline; outline: 0;
}
QTreeWidget::item { padding: 3px 2px; }
QTreeWidget::item:selected { background: @select; color: #ffffff; }
QHeaderView::section {
    color: @text; font-weight: bold; padding: 3px 6px; background: @face;
    border: none; border-right: 1px solid @shadow; border-bottom: 1px solid @outline;
}
QToolTip { color: @text; background: @fieldAlt; border: 1px solid @outline; }
)";

QString themedStyleSheet()
{
    // Longer names first so @field doesn't eat @fieldAlt.
    const std::pair<const char *, QColor> names[] = {
        {"@fieldAlt", Theme::fieldAlt}, {"@outline", Theme::outline}, {"@shadow", Theme::shadow},
        {"@select", Theme::select}, {"@strip", Theme::strip}, {"@field", Theme::field},
        {"@light", Theme::light}, {"@face", Theme::face}, {"@text", Theme::text}, {"@dim", Theme::dim},
    };
    QString css = styleSheet;
    for (const auto &[name, colour] : names)
        css.replace(name, colour.name());
    return css;
}

}

namespace Theme {

void apply(QApplication &app)
{
    app.setStyle(new Win31Style);
    QPalette pal;
    pal.setColor(QPalette::Window, face);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Button, face);
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::Base, field);
    pal.setColor(QPalette::AlternateBase, fieldAlt);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Highlight, select);
    pal.setColor(QPalette::HighlightedText, Qt::white);
    pal.setColor(QPalette::Light, light);
    pal.setColor(QPalette::Mid, shadow);
    pal.setColor(QPalette::Dark, shadow);
    pal.setColor(QPalette::Shadow, outline);
    pal.setColor(QPalette::PlaceholderText, dim.darker(115));
    for (auto role : {QPalette::ButtonText, QPalette::WindowText, QPalette::Text})
        pal.setColor(QPalette::Disabled, role, dim.darker(130));
    app.setPalette(pal);
    app.setStyleSheet(themedStyleSheet());
}

void drawSunken(QPainter &p, const QRect &r, const QColor &fill)
{
    p.fillRect(r, fill);
    p.setPen(shadow);
    p.drawLine(r.topLeft(), r.topRight());
    p.drawLine(r.topLeft(), r.bottomLeft());
    p.setPen(light);
    p.drawLine(r.bottomLeft(), r.bottomRight());
    p.drawLine(r.topRight(), r.bottomRight());
}

void drawBlocks(QPainter &p, const QRect &r, double fraction)
{
    p.fillRect(r, field);
    p.setPen(light);
    p.drawRect(r.adjusted(0, 0, -1, -1));
    const QRect inner = r.adjusted(2, 2, -2, -2);
    constexpr int block = 6, gap = 2;
    const int count = (inner.width() + gap) / (block + gap);
    const int lit = fraction < 0 ? 0 : int(qMin(fraction, 1.0) * count + 0.5);
    for (int i = 0; i < lit; ++i)
        p.fillRect(inner.left() + i * (block + gap), inner.top(), block, inner.height(), bar);
}

}
