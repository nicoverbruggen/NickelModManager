#include "hold_row.h"
#include <QMouseEvent>
#include <QStyle>
#include <QTouchEvent>

namespace NickelModManager {
namespace ui {
namespace {
QPointF mousePosition(QEvent *event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return static_cast<QMouseEvent *>(event)->position();
#else
    return static_cast<QMouseEvent *>(event)->localPos();
#endif
}
} // namespace
HoldRow::HoldRow(int movementTolerance, QWidget *parent)
    : QFrame(parent), tolerance_(movementTolerance) {
    setAttribute(Qt::WA_AcceptTouchEvents);
    timer_.setSingleShot(true);
    timer_.setInterval(550); // The same hold duration as Kobalt's list controls.
    connect(&timer_, &QTimer::timeout, this, [this] {
        cancel();
        if (held) {
            held();
        }
    });
}

void HoldRow::begin(const QPointF &point) {
    origin_ = point;
    tracking_ = true;
    setProperty("nmmPressed", true);
    style()->unpolish(this);
    style()->polish(this);
    update();
    timer_.start();
}

void HoldRow::move(const QPointF &point) {
    if (tracking_ && (!rect().contains(point.toPoint()) ||
                      (point - origin_).manhattanLength() > tolerance_)) {
        cancel();
    }
}

void HoldRow::cancel() {
    timer_.stop();
    tracking_ = false;
    setProperty("nmmPressed", false);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

bool HoldRow::event(QEvent *event) {
    switch (event->type()) {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate: {
        const auto *touch = static_cast<QTouchEvent *>(event);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const auto points = touch->points();
        const QPointF point = points.isEmpty() ? QPointF() : points.first().position();
#else
        const auto points = touch->touchPoints();
        const QPointF point = points.isEmpty() ? QPointF() : points.first().pos();
#endif
        if (points.size() != 1) {
            cancel();
        } else if (event->type() == QEvent::TouchBegin) {
            begin(point);
        } else {
            move(point);
        }
        event->accept();
        return true;
    }
    case QEvent::MouseButtonPress:
        if (static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            begin(mousePosition(event));
            event->accept();
            return true;
        }
        break;
    case QEvent::MouseMove:
        move(mousePosition(event));
        return true;
    case QEvent::MouseButtonRelease:
    case QEvent::TouchEnd: {
        const bool tap = tracking_;
        cancel();
        if (tap && tapped) { tapped(); }
        event->accept();
        return true;
    }
    case QEvent::TouchCancel:
    case QEvent::Hide:
    case QEvent::WindowDeactivate:
        cancel();
        break;
    default:
        break;
    }
    return QFrame::event(event);
}
} // namespace ui
} // namespace NickelModManager
