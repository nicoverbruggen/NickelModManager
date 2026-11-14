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
class VectorIcon final : public QIconEngine {
  public:
    VectorIcon(Icon kind, QColor color) : kind_(kind), color_(color) {}
    QIconEngine *clone() const override { return new VectorIcon(kind_, color_); }
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
        const qreal scale = side / qMax<qreal>(1, qMax(bounds.width(), bounds.height()));
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(QRectF(rect).center());
        painter->scale(scale, scale);
        painter->translate(-bounds.center());
        painter->setPen(Qt::NoPen);
        painter->setBrush(color_);
        if (mode == QIcon::Disabled) { painter->setOpacity(.4); }
        painter->drawPath(path);
        painter->restore();
    }
  private:
    Icon kind_;
    QColor color_;
};
} // namespace

QPainterPath iconPath(Icon kind) {
    if (kind == Icon::Restart) { return restartPath(); }
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
    case Icon::Blocks: name = "squares-four"; break;
    case Icon::Lock: name = "lock"; break;
    case Icon::Info: name = "info"; break;
    case Icon::Alert: name = "warning"; break;
    case Icon::Settings: name = "gear-fine"; break;
    case Icon::Download: name = "download-simple"; break;
    case Icon::Ask: name = "question"; break;
    case Icon::Shield: name = "x"; break;
    case Icon::Restore: name = "arrow-u-up-left"; break;
    default: break;
    }
    QPainterPath path;
    phosphorGeometry(name, path);
    QTransform normalized;
    normalized.scale(24.0 / 256, 24.0 / 256);
    return normalized.map(path);
}

QIcon icon(Icon kind, const QColor &color, qreal stroke) {
    Q_UNUSED(stroke);
    return QIcon(new VectorIcon(kind, color));
}
const char *iconNotice() { return lucideNotice; }
} // namespace ui
} // namespace NickelModManager
