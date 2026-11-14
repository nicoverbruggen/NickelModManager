// Adapted from the UI library of an earlier loader prototype, ported to also build with Qt 5.2.1
// and GCC 4.9.
#include "controls.h"
#include <QAbstractButton>
#include <QEvent>
#include <QFontMetrics>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QResizeEvent>
#include <QTouchEvent>

namespace NickelModManager {
namespace ui {
namespace {
// Separate row rules from control outlines so the list has less visual weight.
const QColor line("#888888"), outline("#000000"), pressedFill("#555555"), disabledFill("#eeeeee"),
    disabledText("#666666");
// Take touch directly on both firmware generations. Qt5 does not synthesize
// clicks for these controls. On Qt6 a touch-aware row would otherwise accept
// the switch's unhandled touch before Qt could synthesize its mouse click.
class TouchClick final : public QObject {
  public:
    explicit TouchClick(QAbstractButton *button) : QObject(button), button_(button) {
        button->setAttribute(Qt::WA_AcceptTouchEvents);
        button->installEventFilter(this);
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (watched != button_) {
            return false;
        }
        const auto inside = [this, event] {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const auto points = static_cast<QTouchEvent *>(event)->points();
            return !points.isEmpty() && button_->rect().contains(points.first().position().toPoint());
#else
            const auto points = static_cast<QTouchEvent *>(event)->touchPoints();
            return !points.isEmpty() && button_->rect().contains(points.first().pos().toPoint());
#endif
        };
        switch (event->type()) {
        case QEvent::TouchBegin:
            if (!button_->isEnabled()) {
                return false;
            }
            pressed_ = true;
            button_->setDown(true);
            event->accept();
            return true;
        case QEvent::TouchUpdate:
            button_->setDown(inside());
            return true;
        case QEvent::TouchEnd: {
            const bool click = pressed_ && inside() && button_->isEnabled();
            pressed_ = false;
            button_->setDown(false);
            if (click) {
                button_->click();
            }
            return true;
        }
        case QEvent::TouchCancel:
            pressed_ = false;
            button_->setDown(false);
            return true;
        default:
            return false;
        }
    }

  private:
    QAbstractButton *button_;
    bool pressed_ = false;
};
void acceptTouch(QAbstractButton *button) {
    new TouchClick(button);
}
// horizontalAdvance arrived in Qt 5.11 and capHeight in Qt 5.8. Firmware
// 4.x has Qt 5.2.1.
int advance(const QFontMetrics &metrics, const QString &text) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
    return metrics.horizontalAdvance(text);
#else
    return metrics.width(text);
#endif
}
qreal capHeight(const QFontMetricsF &metrics) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 8, 0)
    return metrics.capHeight();
#else
    return metrics.tightBoundingRect(QStringLiteral("H")).height();
#endif
}
// Baseline of a single line of this font, centered in a box of this height.
// Text next to a glyph is drawn at this baseline instead of with
// Qt::AlignVCenter. Qt rounds its centering differently from this
// calculation, which left the glyph about 1.5 pixels above the text on
// firmware 4.45.
int textBaseline(const QFont &font, int height) {
    const QFontMetricsF metrics(font);
    return qRound((height - metrics.height()) / 2.0 + metrics.ascent());
}
// Vertical center of the capital letters on that baseline.
qreal capCenter(const QFont &font, int height) {
    return textBaseline(font, height) - capHeight(QFontMetricsF(font)) / 2.0;
}
QFont detailFont(QFont font) {
    font.setPixelSize(qMax(1, font.pixelSize() * 13 / 15));
    font.setWeight(QFont::Normal);
    return font;
}

} // namespace

void enableTouch(QAbstractButton *button) {
    acceptTouch(button);
}
int Context::px(int value) const {
    return qMax(1, qRound(value * density / 300.0));
}
void Context::apply(QWidget *widget, int pixels) const {
    auto value = font;
    value.setPixelSize(px(pixels));
    value.setItalic(false);
    widget->setFont(value);
    widget->setPalette(palette);
    const auto family = value.family().replace('\\', "\\\\").replace('"', "\\\"");
    widget->setStyleSheet(
        QString("font-family:\"%1\";font-size:%2px;font-style:normal;font-weight:400;")
            .arg(family)
            .arg(value.pixelSize()));
}

Toggle::Toggle(const QString &name, const Context &context, QWidget *parent)
    : QCheckBox(parent), context_(context) {
    context.apply(this);
    setAccessibleName(name);
    setFocusPolicy(Qt::NoFocus);
    acceptTouch(this);
    setFixedSize(context_.px(104), context_.px(88));
}
bool Toggle::hitButton(const QPoint &point) const {
    return rect().contains(point);
}
void Toggle::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QColor ink = palette().color(QPalette::WindowText),
                 paper = palette().color(QPalette::Window);
    if (!isEnabled()) {
        painter.setOpacity(.35);
    }
    // The track ends at the content edge. The space on its left is touch area.
    const qreal stroke = qMax(1, context_.px(2));
    const QRectF track(width() - context_.px(76) - stroke / 2, (height() - context_.px(42)) / 2.0,
                       context_.px(76), context_.px(42));
    painter.setPen(QPen(ink, stroke));
    painter.setBrush(isChecked() ? ink : paper);
    painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
    const int diameter = context_.px(30), inset = context_.px(6);
    const qreal x = isChecked() ? track.right() - inset - diameter : track.left() + inset;
    painter.setPen(Qt::NoPen);
    painter.setBrush(isChecked() ? paper : ink);
    painter.drawEllipse(QRectF(x, track.top() + inset, diameter, diameter));
    if (isDown() && isEnabled()) {
        // While held, a contrasting outline inside the track. The knob moves on release.
        const qreal gap = context_.px(3);
        const QRectF ring = track.adjusted(gap, gap, -gap, -gap);
        painter.setPen(QPen(isChecked() ? paper : ink, qMax<qreal>(1.5, context_.px(3))));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(ring, ring.height() / 2, ring.height() / 2);
    }
}

CircleButton::CircleButton(const QString &name, Icon glyph, const Context &context, QWidget *parent)
    : QPushButton(parent), glyph_(glyph), context_(context) {
    setAccessibleName(name);
    setFocusPolicy(Qt::NoFocus);
    enableTouch(this);
    setFixedSize(context.px(88), context.px(88));
}
void CircleButton::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const auto colors = parentWidget()->palette();
    const QColor ink = colors.color(QPalette::WindowText), paper = colors.color(QPalette::Window);
    const bool down = isDown() && isEnabled();
    if (!isEnabled()) {
        painter.setOpacity(.35);
    }
    const QRectF circle = QRectF(rect()).adjusted(context_.px(7), context_.px(7), -context_.px(7), -context_.px(7));
    painter.setPen(QPen(ink, context_.px(2)));
    painter.setBrush(down ? ink : paper);
    painter.drawEllipse(circle);
    const int size = context_.px(36);
    NickelModManager::ui::icon(glyph_, down ? paper : ink).paint(&painter, QRect((width()-size)/2, (height()-size)/2, size, size));
}

ActionButton::ActionButton(const QString &caption, QWidget *parent) : QPushButton(caption, parent) {
    setFocusPolicy(Qt::NoFocus);
    acceptTouch(this);
}
ActionButton::ActionButton(const QString &caption, const Context &context, QWidget *parent)
    : ActionButton(caption, parent) {
    context.apply(this, 30);
    setMinimumHeight(context.px(72));
}
void ActionButton::setKind(ButtonKind kind) {
    kind_ = kind;
    update();
}
void ActionButton::setGlyph(Icon kind, int ink) {
    hasGlyph_ = true;
    glyph_ = kind;
    ink_ = ink;
    updateGeometry();
    update();
}
QSize ActionButton::sizeHint() const {
    if (layout()) {
        const QSize size = layout()->totalSizeHint();
        return QSize(size.width(), qMax(size.height(), minimumHeight()));
    }
    const QFontMetrics metrics(font());
    const int inset = qMax(8, font().pixelSize() / 2), gap = qMax(6, font().pixelSize() / 3);
    int width = 2 * inset + (text().isEmpty() ? 0 : advance(metrics, text()));
    if (hasGlyph_) {
        width += ink_ + (text().isEmpty() ? 0 : gap);
    }
    int height = metrics.height() + inset;
    if (!detail_.isEmpty()) {
        const QFontMetrics small(detailFont(font()));
        width = qMax(width, 2 * inset + advance(small, detail_));
        height += small.height();
    }
    return QSize(width, qMax(minimumHeight(), height));
}
QSize ActionButton::minimumSizeHint() const {
    return layout() ? layout()->totalMinimumSize() : sizeHint();
}
bool ActionButton::hasHeightForWidth() const {
    return layout() ? layout()->hasHeightForWidth() : false;
}
int ActionButton::heightForWidth(int width) const {
    if (!layout()) {
        return QPushButton::heightForWidth(width);
    }
    return qMax(minimumHeight(), layout()->totalHeightForWidth(width));
}
void ActionButton::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    // Style sheet rules rewrite this button's own palette roles. The parent
    // keeps the panel's real ink and paper.
    const QPalette colors = parentWidget() ? parentWidget()->palette() : palette();
    const QColor ink = colors.color(QPalette::WindowText), paper = colors.color(QPalette::Window);
    const bool down = isDown() && isEnabled();
    QColor fill = paper, textColor = isEnabled() ? ink : disabledText;
    if (kind_ == ButtonKind::Primary) {
        fill = !isEnabled() ? disabledFill : down ? pressedFill : ink;
        textColor = isEnabled() ? paper : disabledText;
    } else if (down) {
        fill = layout() ? QColor("#dddddd") : ink;
        textColor = layout() ? ink : paper;
    }
    painter.fillRect(rect(), fill);
    if (kind_ == ButtonKind::Outlined) {
        painter.setPen(outline);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5));
    }
    painter.setPen(line);
    if (rules_ & Qt::LeftEdge) {
        painter.drawLine(QPointF(.5, 0), QPointF(.5, height()));
    }
    if (rules_ & Qt::RightEdge) {
        painter.drawLine(QPointF(width() - .5, 0), QPointF(width() - .5, height()));
    }
    if (rules_ & Qt::TopEdge) {
        painter.drawLine(QPointF(0, .5), QPointF(width(), .5));
    }
    if (rules_ & Qt::BottomEdge) {
        painter.drawLine(QPointF(0, height() - .5), QPointF(width(), height() - .5));
    }
    if (layout()) {
        return;
    }
    painter.setFont(font());
    painter.setPen(textColor);
    const int inset = qMax(8, font().pixelSize() / 2), gap = qMax(6, font().pixelSize() / 3);
    const auto paintGlyph = [&](int left) {
        const int top = text().isEmpty() ? (height() - ink_) / 2
                                         : qRound(capCenter(font(), height()) - ink_ / 2.0);
        NickelModManager::ui::icon(glyph_, textColor)
            .paint(&painter, QRect(left, top, ink_, ink_), Qt::AlignCenter,
                   isEnabled() ? QIcon::Normal : QIcon::Disabled);
    };
    if (!detail_.isEmpty()) {
        // The caption and its detail as one centered block of two lines. A
        // glyph sits before the caption on the first line.
        const QFont small = detailFont(font());
        const QFontMetrics big(font()), little(small);
        const int top = (height() - big.height() - little.height()) / 2;
        if (hasGlyph_) {
            const int lineWidth = ink_ + gap + advance(big, text()),
                      left = (width() - lineWidth) / 2;
            NickelModManager::ui::icon(glyph_, textColor)
                .paint(&painter,
                       QRect(left, top + qRound(capCenter(font(), big.height()) - ink_ / 2.0), ink_,
                             ink_),
                       Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
            painter.drawText(QPoint(left + ink_ + gap, top + textBaseline(font(), big.height())),
                             text());
        } else {
            painter.drawText(QRect(inset, top, width() - 2 * inset, big.height()), Qt::AlignCenter,
                             text());
        }
        painter.setFont(small);
        painter.drawText(QRect(inset, top + big.height(), width() - 2 * inset, little.height()),
                         Qt::AlignCenter,
                         little.elidedText(detail_, Qt::ElideRight, width() - 2 * inset));
        return;
    }
    if (!hasGlyph_) {
        painter.drawText(rect().adjusted(inset, 0, -inset, 0), Qt::AlignCenter | Qt::TextWordWrap,
                         text());
        return;
    }
    if (text().isEmpty()) {
        paintGlyph((width() - ink_) / 2);
        return;
    }
    const int textWidth = advance(fontMetrics(), text());
    const int left = kind_ == ButtonKind::Quiet ? inset : (width() - textWidth - gap - ink_) / 2;
    paintGlyph(left);
    painter.drawText(QPoint(left + ink_ + gap, textBaseline(font(), height())), text());
}

Divider::Divider(const Context &context, QWidget *parent) : QWidget(parent) {
    context.apply(this);
    setFixedHeight(qMax(1, context.px(1)));
    setAttribute(Qt::WA_TransparentForMouseEvents);
}
void Divider::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), line);
}

Header::Header(const Context &context, QWidget *parent) : QWidget(parent), context_(context) {
    // Keep the same interface font as the list and action menus.
    back = new ActionButton(QString(), context, this);
    back->setObjectName("nmmBack");
    back->setAccessibleName("Back");
    back->setKind(ButtonKind::Quiet);
    back->setGlyph(Icon::Back, context.px(36));
    title = new QLabel(this);
    title->setObjectName("nmmTitle");
    title->setAlignment(Qt::AlignCenter);
    title->setTextFormat(Qt::PlainText);
    context.apply(title, 40);
    QFont serif = title->font();
    serif.setFamily(context.font.family());
    title->setFont(serif);
    title->setStyleSheet(QString("font-family:\"%1\";font-size:%2px;font-style:normal;"
                                 "font-weight:400;background:transparent;")
                             .arg(context.font.family()).arg(serif.pixelSize()));
    setFixedHeight(context.px(112));
}
void Header::setTitle(const QString &value) {
    text_ = value;
    arrange();
}
void Header::resizeEvent(QResizeEvent *) {
    arrange();
}
void Header::arrange() {
    // Back keeps a touch area around its arrow, and the arrow lines up with
    // the page content inset.
    const int padding = context_.px(20), edge = qMax(0, context_.px(36) - padding),
              top = context_.px(12), buttonHeight = height() - 2 * top;
    const int backWidth = context_.px(36) + 2 * padding;
    back->setGeometry(edge, top, backWidth, buttonHeight);
    // Keep the title centered on the screen, clear of Back on both sides.
    const int side = edge + backWidth + context_.px(16);
    const int titleWidth = qMax(1, width() - 2 * side);
    title->setGeometry(side, 0, titleWidth, height());
    title->setText(title->fontMetrics().elidedText(text_, Qt::ElideRight, titleWidth));
    title->setAccessibleName(text_);
}
void Header::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    painter.fillRect(0, height() - qMax(1, context_.px(1)), width(), qMax(1, context_.px(1)), line);
}

} // namespace ui
} // namespace NickelModManager
