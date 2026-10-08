#include "menu.h"
#include "controls.h"
#include <QEvent>
#include <QLabel>
#include <QPainter>

namespace NickelModManager {
namespace ui {
MenuEntry::MenuEntry(const QString &caption, QWidget *reference, QWidget *parent)
    : QPushButton(caption, parent), reference_(reference) {
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    enableTouch(this);
    if (reference) {
        reference->installEventFilter(this);
    }
}
QSize MenuEntry::sizeHint() const {
    const int height = reference_ ? (reference_->height() > 1 ? reference_->height()
                                                              : reference_->sizeHint().height())
                                  : fontMetrics().height() * 3;
    return QSize(QPushButton::sizeHint().width(), qMax(1, height));
}
bool MenuEntry::eventFilter(QObject *watched, QEvent *event) {
    if (watched == reference_ &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Polish ||
         event->type() == QEvent::FontChange)) {
        updateGeometry();
        update();
    }
    return QPushButton::eventFilter(watched, event);
}
void MenuEntry::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    auto *label = reference_ ? reference_->findChild<QLabel *>("label") : nullptr;
    auto *glyph = reference_ ? reference_->findChild<QLabel *>("pixmap") : nullptr;
    const QFont textFont = label ? label->font() : font();
    QColor ink = label ? label->palette().color(label->foregroundRole())
                       : palette().color(QPalette::WindowText);
    int left = qMax(16, height() / 4);
    if (label && label->isVisible()) {
        // Nickel indents its labels with style sheet padding, which contentsRect reports.
        left = label->mapTo(reference_, label->contentsRect().topLeft()).x() +
               qMax(0, label->indent());
    }
    // Nickel's own rows invert while held: ink fill, paper text and glyph.
    const QColor paper = reference_ ? reference_->palette().color(QPalette::Window)
                                    : palette().color(QPalette::Window);
    const QColor rule = ink;
    if (isDown()) {
        painter.fillRect(rect(), ink);
        ink = paper;
    }
    if (glyph && glyph->isVisible()) {
        // Nickel's own glyphs fill about three fifths of the icon column.
        const QRect column(glyph->mapTo(reference_, QPoint()), glyph->size());
        const int side = qMax(1, qRound(qMin(column.width(), column.height()) * .6));
        // Nickel's own More icons draw strokes of about 2.5 pixels at this size.
        NickelModManager::ui::icon(Icon::Blocks, ink, qMax<qreal>(1.5, side / 17.0))
            .paint(&painter, QRect(column.center().x() - side / 2, column.center().y() - side / 2,
                                   side, side));
    }
    painter.setFont(textFont);
    painter.setPen(ink);
    painter.drawText(QRect(left, 0, qMax(0, width() - left), height()),
                     Qt::AlignLeft | Qt::AlignVCenter, text());
    painter.fillRect(QRect(0, height() - 1, width(), 1), rule);
}
} // namespace ui
} // namespace NickelModManager
