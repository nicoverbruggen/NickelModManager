#include "icons.h"
#include "icon_notice.h"
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <QTransform>

namespace NickelModManager {
namespace ui {
namespace {
// Paths copied from the pinned Lucide assets. Curves retain the converter's
// cubic coordinates, so removing the converter does not change the glyphs.
QPainterPath backPath() {
    QPainterPath p;
    p.moveTo(12, 19);
    p.lineTo(5, 12);
    p.lineTo(12, 5);
    p.moveTo(19, 12);
    p.lineTo(5, 12);
    return p;
}

QPainterPath chevronPath() {
    QPainterPath p;
    p.moveTo(9, 18);
    p.lineTo(15, 12);
    p.lineTo(9, 6);
    return p;
}

QPainterPath restartPath() {
    QPainterPath p;
    p.moveTo(21, 12);
    p.cubicTo(21, 16.9705627485, 16.9705627485, 21, 12, 21);
    p.cubicTo(7.02943725152, 21, 3, 16.9705627485, 3, 12);
    p.cubicTo(3, 7.02943725152, 7.02943725152, 3, 12, 3);
    p.cubicTo(14.52, 3, 16.93, 4, 18.74, 5.74);
    p.lineTo(21, 8);
    p.moveTo(21, 3);
    p.lineTo(21, 8);
    p.lineTo(16, 8);
    return p;
}

QPainterPath blocksPath() {
    QPainterPath p;
    p.moveTo(10, 22);
    p.lineTo(10, 7);
    p.cubicTo(10, 6.44771525017, 9.55228474983, 6, 9, 6);
    p.lineTo(4, 6);
    p.cubicTo(2.89543050034, 6, 2, 6.89543050034, 2, 8);
    p.lineTo(2, 20);
    p.cubicTo(2, 21.1045694997, 2.89543050034, 22, 4, 22);
    p.lineTo(16, 22);
    p.cubicTo(17.1045694997, 22, 18, 21.1045694997, 18, 20);
    p.lineTo(18, 15);
    p.cubicTo(18, 14.4477152502, 17.5522847498, 14, 17, 14);
    p.lineTo(2, 14);
    p.moveTo(15, 2);
    p.lineTo(21, 2);
    p.cubicTo(21.5522847498, 2, 22, 2.44771525017, 22, 3);
    p.lineTo(22, 9);
    p.cubicTo(22, 9.55228474983, 21.5522847498, 10, 21, 10);
    p.lineTo(15, 10);
    p.cubicTo(14.4477152502, 10, 14, 9.55228474983, 14, 9);
    p.lineTo(14, 3);
    p.cubicTo(14, 2.44771525017, 14.4477152502, 2, 15, 2);
    p.closeSubpath();
    return p;
}

QPainterPath lockPath() {
    QPainterPath p;
    p.moveTo(5, 11);
    p.lineTo(19, 11);
    p.cubicTo(20.1045694997, 11, 21, 11.8954305003, 21, 13);
    p.lineTo(21, 20);
    p.cubicTo(21, 21.1045694997, 20.1045694997, 22, 19, 22);
    p.lineTo(5, 22);
    p.cubicTo(3.89543050034, 22, 3, 21.1045694997, 3, 20);
    p.lineTo(3, 13);
    p.cubicTo(3, 11.8954305003, 3.89543050034, 11, 5, 11);
    p.closeSubpath();
    p.moveTo(7, 11);
    p.lineTo(7, 7);
    p.cubicTo(7, 4.23857625085, 9.23857625085, 2, 12, 2);
    p.cubicTo(14.7614237492, 2, 17, 4.23857625085, 17, 7);
    p.lineTo(17, 11);
    return p;
}

QPainterPath infoPath() {
    QPainterPath p;
    p.addEllipse(QRectF(2, 2, 20, 20));
    p.moveTo(12, 16);
    p.lineTo(12, 12);
    p.moveTo(12, 8);
    p.lineTo(12.01, 8);
    return p;
}

class VectorIcon final : public QIconEngine {
  public:
    VectorIcon(Icon kind, QColor color, qreal stroke)
        : kind_(kind), color_(color), stroke_(stroke) {}
    QIconEngine *clone() const override {
        return new VectorIcon(kind_, color_, stroke_);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
    // The requested size is the ink size. Lucide glyphs fill different parts of
    // their 24-unit box, so the stroked bounds, not the box, are fitted and
    // centered. The stroke keeps one weight for every glyph at a given size.
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        const QPainterPath path = iconPath(kind_);
        const QRectF bounds = path.boundingRect();
        const qreal side = qMin(rect.width(), rect.height()),
                    stroke = stroke_ > 0 ? stroke_ : qMax<qreal>(1.5, side / 9.0);
        const qreal scale =
            qMax<qreal>(1, side - stroke) / qMax<qreal>(1, qMax(bounds.width(), bounds.height()));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(QRectF(rect).center());
        painter->scale(scale, scale);
        painter->translate(-bounds.center());
        painter->setPen(QPen(color_, stroke / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        if (mode == QIcon::Disabled) {
            painter->setOpacity(.4);
        }
        painter->drawPath(path);
        painter->restore();
    }

  private:
    Icon kind_;
    QColor color_;
    qreal stroke_;
};
} // namespace

QPainterPath iconPath(Icon kind) {
    switch (kind) {
    case Icon::Back:
        return backPath();
    case Icon::ChevronRight:
        return chevronPath();
    case Icon::ChevronLeft: {
        QTransform mirror;
        mirror.translate(24, 0);
        mirror.scale(-1, 1);
        return mirror.map(chevronPath());
    }
    case Icon::Restart:
        return restartPath();
    case Icon::Blocks:
        return blocksPath();
    case Icon::Lock:
        return lockPath();
    case Icon::Info:
        return infoPath();
    case Icon::Alert: {
        QTransform flip;
        flip.translate(0, 24);
        flip.scale(1, -1);
        return flip.map(infoPath());
    }
    }
    return {};
}

QIcon icon(Icon kind, const QColor &color, qreal stroke) {
    return QIcon(new VectorIcon(kind, color, stroke));
}

// Keep the required notice in the library even though no UI displays it.
const char *iconNotice() {
    return lucideNotice;
}
} // namespace ui
} // namespace NickelModManager
