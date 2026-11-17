#include "icons.h"
#include "icon_notice.h"
#include <QIconEngine>
#include <QPainter>
#include <QPainterPathStroker>
#include <QPixmap>
#include <QTransform>

namespace NickelModManager {
namespace ui {
namespace {
#include "icons/phosphor.h"

// Match Kobalt's custom sync glyph for restart. The other controls use the
// same pinned Phosphor regular outlines as Kobalt.
QPainterPath restartPath() {
    QPainterPath path;
    path.moveTo(4, 9);
    path.cubicTo(7, 2, 17, 2, 20, 9);
    path.moveTo(20, 4);
    path.lineTo(20, 9);
    path.lineTo(15, 9);
    path.moveTo(20, 15);
    path.cubicTo(17, 22, 7, 22, 4, 15);
    path.moveTo(4, 20);
    path.lineTo(4, 15);
    path.lineTo(9, 15);
    QPainterPathStroker stroke;
    stroke.setWidth(1.5);
    stroke.setCapStyle(Qt::RoundCap);
    stroke.setJoinStyle(Qt::RoundJoin);
    return stroke.createStroke(path);
}
// Keep the original Manage Mods glyph in Nickel's More menu.
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

class VectorIcon final : public QIconEngine {
  public:
    VectorIcon(Icon kind, QColor color, qreal stroke) : kind_(kind), color_(color), stroke_(stroke) {}
    QIconEngine *clone() const override { return new VectorIcon(kind_, color_, stroke_); }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        const QPainterPath path = iconPath(kind_);
        const QRectF bounds = path.boundingRect();
        const qreal side = qMin(rect.width(), rect.height());
        const bool blocks = kind_ == Icon::Blocks;
        const qreal stroke = blocks ? (stroke_ > 0 ? stroke_ : qMax<qreal>(1.5, side / 9.0)) : 0;
        const qreal scale = qMax<qreal>(1, side - stroke) /
                            qMax<qreal>(1, qMax(bounds.width(), bounds.height()));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(QRectF(rect).center());
        painter->scale(scale, scale);
        painter->translate(-bounds.center());
        if (blocks) {
            painter->setPen(QPen(color_, stroke / scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
        } else {
            painter->setPen(Qt::NoPen);
            painter->setBrush(color_);
        }
        if (mode == QIcon::Disabled) { painter->setOpacity(.4); }
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
    if (kind == Icon::Restart) { return restartPath(); }
    if (kind == Icon::Blocks) { return blocksPath(); }
    if (kind == Icon::ChevronLeft) {
        QTransform mirror;
        mirror.translate(24, 0);
        mirror.scale(-1, 1);
        return mirror.map(iconPath(Icon::ChevronRight));
    }
    QString name;
    switch (kind) {
    case Icon::Back: name = "arrow-left"; break;
    case Icon::ChevronRight: name = "caret-right"; break;
    case Icon::Lock: name = "lock"; break;
    case Icon::Info: name = "info"; break;
    case Icon::Alert: name = "warning"; break;
    case Icon::Settings: name = "gear-fine"; break;
    case Icon::Download: name = "download-simple"; break;
    case Icon::Ask: name = "question"; break;
    case Icon::Shield: name = "x"; break;
    case Icon::Restore: name = "arrow-u-up-left"; break;
    case Icon::Trash: name = "trash"; break;
    default: break;
    }
    QPainterPath path;
    phosphorGeometry(name, path);
    QTransform normalized;
    normalized.scale(24.0 / 256, 24.0 / 256);
    return normalized.map(path);
}

QIcon icon(Icon kind, const QColor &color, qreal stroke) {
    return QIcon(new VectorIcon(kind, color, stroke));
}
const char *iconNotice() { return lucideNotice; }
} // namespace ui
} // namespace NickelModManager
