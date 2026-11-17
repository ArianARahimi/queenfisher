#include "Theme.h"

#include <QApplication>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPalette>
#include <QPolygonF>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>

namespace {

// Check boxes and drop-downs drawn the Vista way; the style sheet covers the rest.
class VistaStyle : public QProxyStyle
{
public:
    VistaStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}

    void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p, const QWidget *w) const override
    {
        if (pe != PE_IndicatorCheckBox) {
            QProxyStyle::drawPrimitive(pe, opt, p, w);
            return;
        }
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(opt->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, QColor(0x2a, 0x2d, 0x34));
        g.setColorAt(1, Theme::field);
        const bool enabled = opt->state & State_Enabled;
        p->setBrush(enabled ? QBrush(g) : QBrush(Theme::panel));
        p->setPen(enabled && (opt->state & State_MouseOver) ? Theme::accent : Theme::outline);
        p->drawRoundedRect(r, 2, 2);
        if (opt->state & State_On) {   // a tick, like Vista
            QPen pen(enabled ? Theme::glow : QColor(0x4c, 0x50, 0x58), 2);
            pen.setCapStyle(Qt::RoundCap);
            p->setPen(pen);
            const QPointF a = r.topLeft();
            const qreal s = r.width();
            p->drawPolyline(QPolygonF({a + QPointF(s * 0.22, s * 0.52), a + QPointF(s * 0.42, s * 0.72),
                                       a + QPointF(s * 0.78, s * 0.28)}));
        }
        p->restore();
    }

    void drawComplexControl(ComplexControl cc, const QStyleOptionComplex *opt, QPainter *p, const QWidget *w) const override
    {
        if (cc != CC_ComboBox) {
            QProxyStyle::drawComplexControl(cc, opt, p, w);
            return;
        }
        // Rounded field with a glossy arrow segment on the right.
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const bool enabled = opt->state & State_Enabled;
        const bool hover = enabled && (opt->state & State_MouseOver);
        const QRectF r = QRectF(opt->rect).adjusted(0.5, 0.5, -0.5, -0.5);
        p->setPen(hover || (opt->state & State_HasFocus) ? Theme::accent : Theme::outline);
        p->setBrush(enabled ? Theme::field : Theme::panel);
        p->drawRoundedRect(r, 2, 2);
        const QRect arrow = subControlRect(cc, opt, SC_ComboBoxArrow, w).adjusted(0, 1, -1, -1);
        Theme::drawGloss(*p, arrow, hover ? Theme::hover : QColor(0x3a, 0x3d, 0x45), 2);
        const QPointF c = QRectF(arrow).center();
        p->setPen(Qt::NoPen);
        p->setBrush(enabled ? Theme::text : Theme::dim);
        p->drawPolygon(QPolygonF({c + QPointF(-4, -2), c + QPointF(4, -2), c + QPointF(0, 2.5)}));
        p->restore();
    }

    int pixelMetric(PixelMetric m, const QStyleOption *opt, const QWidget *w) const override
    {
        if (m == PM_IndicatorWidth || m == PM_IndicatorHeight)
            return 14;
        return QProxyStyle::pixelMetric(m, opt, w);
    }
};

// @names are swapped for colours in apply(). "Gloss" = Vista's two-tone gradient:
// a lighter top half, then a sharp step to a darker bottom half.
const char *styleSheet = R"(
QWidget#page, QMainWindow { background: @window; }
QLabel { color: @text; }
QToolTip { color: @text; background: @panel; border: 1px solid @outline; }

QMenuBar {
    color: @text; border-bottom: 1px solid @outline;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2d3037, stop:1 #23252b);
}
QMenuBar::item { padding: 3px 8px; background: transparent; }
QMenuBar::item:selected { background: @hover; border-radius: 3px; }
QMenu { color: @text; background: @panel; border: 1px solid @outline; padding: 2px; }
QMenu::item { padding: 4px 24px 4px 24px; }
QMenu::item:selected { background: @accent; color: @onAccent; border-radius: 2px; }
QMenu::item:disabled { color: #6c707a; }
QMenu::separator { height: 1px; background: @edge; margin: 3px 6px; }

QTabWidget::pane { border: 1px solid @outline; background: @panel; top: -1px; }
QTabBar::tab {
    color: #cfd3da; padding: 6px 14px 6px 10px; margin-right: -1px;
    border: 1px solid @outline; border-bottom: none;
    border-top-left-radius: 4px; border-top-right-radius: 4px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3b3e46, stop:0.5 #30333a, stop:0.51 #292b31, stop:1 #24262c);
}
QTabBar::tab:hover:!selected { background: @wash; }
QTabBar::tab:selected {
    color: #ffffff; margin-bottom: -1px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4c515c, stop:0.5 #3d414a, stop:0.51 #33363e, stop:1 @panel);
}
QLabel#wordmark { color: @glow; font-size: 22px; font-weight: bold; padding: 0 10px 2px 0; }

QPushButton {
    color: @text; padding: 4px 14px; border: 1px solid @outline; border-radius: 3px;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4a4e58, stop:0.49 #3a3d45, stop:0.5 #303339, stop:1 #2a2c32);
}
QPushButton:hover { border-color: @accent; background: @wash; }
QPushButton:pressed { border-color: @accent; background: @sunk; }
QPushButton:disabled { color: #6c707a; }

/* Download: the one button that matters, in the green so it's found at a glance */
QPushButton#primary { color: @onAccent; font-weight: bold; padding: 4px 16px; border-color: @outline; background: @gloss; }
QPushButton#primary:hover { border-color: @glow; background: @glossLit; }
QPushButton#primary:pressed { background: @glossSunk; }
QPushButton#primary:disabled {
    color: #6c707a; border-color: @outline;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a3d45, stop:0.49 #33363d, stop:0.5 #2c2e34, stop:1 #282a2f);
}

QToolButton#action {
    color: @text; border: 1px solid transparent; border-radius: 3px; padding: 3px 8px;
    background: transparent;
}
QToolButton#action:hover { border-color: @accent; background: @wash; }
QToolButton#action:pressed, QToolButton#action:checked { border-color: @accent; background: @sunk; }
QToolButton#action:disabled { color: #6c707a; }

QLineEdit, QPlainTextEdit {
    color: @text; background: @field; border: 1px solid @outline; border-radius: 2px;
    padding: 3px 5px; selection-background-color: @accent; selection-color: @onAccent;
}
QLineEdit:focus { border-color: @accent; }
QLineEdit:read-only { color: @dim; }
QComboBox { color: @text; padding: 3px 5px; }
QComboBox QAbstractItemView {
    color: @text; background: @field; border: 1px solid @outline;
    selection-background-color: @accent; selection-color: @onAccent;
}
QCheckBox { color: @text; spacing: 6px; }
QCheckBox:disabled { color: #4c5058; }

QTreeWidget, QListWidget {
    color: @text; background: @field; alternate-background-color: @fieldAlt;
    border: 1px solid @outline; outline: 0;
}
QTreeWidget::item, QListWidget::item { padding: 2px 3px; }
QTreeWidget::item:selected, QListWidget::item:selected { color: @onAccent; background: @gloss; }
QHeaderView::section {
    color: #cfd3da; padding: 3px 6px;
    border: none; border-right: 1px solid #1a1b1f; border-bottom: 1px solid @outline;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #373a42, stop:0.5 #30333a, stop:0.51 #2b2d33, stop:1 #27292e);
}

QFrame#box { background: @field; border: 1px solid @outline; }
QLabel#boxHeader {
    color: @onAccent; font-weight: bold; padding: 2px 6px; qproperty-alignment: AlignCenter;
    border-bottom: 1px solid @outline; background: @gloss;
}
/* a box that doesn't apply (Quality for audio) greys out completely */
QLabel#boxHeader:disabled {
    color: #6c707a; border-bottom-color: @outline;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #3a3d45, stop:0.5 #33363d, stop:0.51 #2c2e34, stop:1 #282a2f);
}
QListWidget:disabled { color: #4c5058; background: @panel; }
QListWidget::item:selected:disabled { color: #6c707a; background: #2c2e34; }
QLabel#sideTitle, QLabel#paneTitle { color: @dim; }
QFrame#banner { border: 1px solid @outline; border-radius: 2px; background: @gloss; }
QFrame#banner QLabel { color: @onAccent; font-weight: bold; }
QLabel#chip {
    color: @onAccent; font-weight: bold; padding: 3px 10px; border: 1px solid @outline; border-bottom: none;
    border-top-left-radius: 3px; border-top-right-radius: 3px; background: @gloss;
}
QFrame#previewPane { background: @field; border: 1px solid @outline; }
QLabel#meta { color: @dim; }

QSplitter::handle { background: @window; }
QSplitter::handle:vertical { height: 7px; }

QStatusBar {
    color: @text; border-top: 1px solid @outline;
    background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2d3037, stop:1 #202227);
}
QStatusBar::item { border: none; }
QLabel#cell {
    color: @text; padding: 1px 8px; border: 1px solid; border-radius: 2px;
    border-color: @outline @edge @edge @outline; background: #1c1d22;
}

QScrollBar:vertical { background: @fieldAlt; width: 15px; margin: 0; }
QScrollBar:horizontal { background: @fieldAlt; height: 15px; margin: 0; }
QScrollBar::handle {
    border: 1px solid @outline; border-radius: 3px; min-height: 24px; min-width: 24px;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #4a4e58, stop:0.49 #3a3d45, stop:0.5 #303339, stop:1 #2a2c32);
}
QScrollBar::handle:hover { border-color: @accent; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
)";

// Vista's two-tone gradient from one colour, the same steps as drawGloss().
QString gloss(const QColor &base)
{
    return QString("qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:0.49 %2, stop:0.5 %3, stop:1 %4)")
        .arg(base.lighter(150).name(), base.lighter(115).name(), base.name(), base.darker(115).name());
}

QString themedStyleSheet()
{
    // Longer names first so @field doesn't eat @fieldAlt, or @gloss @glossLit.
    const std::pair<const char *, QString> names[] = {
        {"@glossLit", gloss(Theme::accent.lighter(118))}, {"@glossSunk", gloss(Theme::accent.darker(125))},
        {"@gloss", gloss(Theme::accent)}, {"@wash", gloss(Theme::hover)}, {"@sunk", gloss(Theme::hover.darker(140))},
        {"@fieldAlt", Theme::fieldAlt.name()}, {"@onAccent", Theme::onAccent.name()},
        {"@outline", Theme::outline.name()}, {"@window", Theme::window.name()}, {"@accent", Theme::accent.name()},
        {"@field", Theme::field.name()}, {"@panel", Theme::panel.name()}, {"@hover", Theme::hover.name()},
        {"@glow", Theme::glow.name()}, {"@edge", Theme::edge.name()}, {"@text", Theme::text.name()},
        {"@dim", Theme::dim.name()},
    };
    QString css = styleSheet;
    for (const auto &[name, value] : names)
        css.replace(name, value);
    return css;
}

}

namespace Theme {

void apply(QApplication &app)
{
    app.setStyle(new VistaStyle);
    QPalette pal;
    pal.setColor(QPalette::Window, window);
    pal.setColor(QPalette::WindowText, text);
    pal.setColor(QPalette::Button, panel);
    pal.setColor(QPalette::ButtonText, text);
    pal.setColor(QPalette::Base, field);
    pal.setColor(QPalette::AlternateBase, fieldAlt);
    pal.setColor(QPalette::Text, text);
    pal.setColor(QPalette::Highlight, accent);
    pal.setColor(QPalette::HighlightedText, onAccent);
    pal.setColor(QPalette::Light, edge);
    pal.setColor(QPalette::Mid, outline);
    pal.setColor(QPalette::Dark, outline);
    pal.setColor(QPalette::Shadow, outline);
    pal.setColor(QPalette::PlaceholderText, dim.darker(120));
    pal.setColor(QPalette::Link, glow);
    for (auto role : {QPalette::ButtonText, QPalette::WindowText, QPalette::Text})
        pal.setColor(QPalette::Disabled, role, QColor(0x6c, 0x70, 0x7a));
    app.setPalette(pal);
    app.setStyleSheet(themedStyleSheet());
}

QPixmap recolour(const QString &file, const QColor &colour, bool shaded)
{
    QImage img = QImage(file).convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < img.height(); ++y) {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const int a = qAlpha(line[x]);
            // shaded: the icon's brightness scales the colour, so its 3D look survives
            const double k = shaded ? 0.4 + 1.2 * qGray(line[x]) / 255.0 : 1.0;
            line[x] = qRgba(qMin(255, int(colour.red() * k)), qMin(255, int(colour.green() * k)),
                            qMin(255, int(colour.blue() * k)), a);
        }
    }
    return QPixmap::fromImage(img);
}

void drawGloss(QPainter &p, const QRect &r, const QColor &base, int radius)
{
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, base.lighter(150));
    g.setColorAt(0.49, base.lighter(115));
    g.setColorAt(0.5, base);
    g.setColorAt(1, base.darker(115));
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(r), radius, radius);
    p.restore();
}

// Vista progress bar: dark rounded track, glossy fill, percentage in the middle.
void drawBar(QPainter &p, const QRect &r, double fraction, BarState state)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(outline);
    p.setBrush(QColor(0x10, 0x11, 0x14));
    p.drawRoundedRect(QRectF(r).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);

    const QRect inner = r.adjusted(2, 2, -2, -2);
    double shown = 0;
    QColor colour = running;
    switch (state) {
    case BarState::Waiting: break;
    case BarState::Running: shown = qBound(0.0, fraction, 1.0); break;
    case BarState::Done: shown = 1; colour = done; break;
    case BarState::Failed: shown = 1; colour = failed; break;
    }
    const int width = int(inner.width() * shown + 0.5);
    if (width > 0)
        drawGloss(p, QRect(inner.topLeft(), QSize(width, inner.height())), colour.darker(120), 1);

    QString label;
    if (state == BarState::Running && fraction >= 0)
        label = QString::number(int(fraction * 100)) + " %";
    else if (state == BarState::Done)
        label = "100 %";
    if (!label.isEmpty()) {
        // Dark text where the bar is filled, light text where it isn't, so it reads at any value.
        QFont f = p.font();
        f.setPointSizeF(f.pointSizeF() * 0.85);
        p.setFont(f);
        const QRect filled(inner.topLeft(), QSize(width, inner.height()));
        p.setClipRect(filled);
        p.setPen(onAccent);
        p.drawText(r, Qt::AlignCenter, label);
        p.setClipRegion(QRegion(r).subtracted(filled));
        p.setPen(text);
        p.drawText(r, Qt::AlignCenter, label);
    }
    p.restore();
}

}
