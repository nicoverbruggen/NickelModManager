#include "pagination.h"
#include "controls.h"
#include <QLabel>
#include <QPainter>
#include <QResizeEvent>
#include <QtMath>

namespace NickelModManager {
namespace ui {
namespace {
const QColor mid("#b7b7b7");
void polishTree(QWidget *widget) {
    widget->ensurePolished();
    for (auto *child : widget->findChildren<QWidget *>()) {
        child->ensurePolished();
    }
}
} // namespace
PagerBar::PagerBar(const Context &context, const QString &prefix, QWidget *parent)
    : QWidget(parent), context_(context) {
    setObjectName(prefix + "Pager");
    previous = new ActionButton({}, context, this);
    previous->setObjectName(prefix + "Previous");
    previous->setAccessibleName("Previous page");
    next = new ActionButton({}, context, this);
    next->setObjectName(prefix + "Next");
    next->setAccessibleName("Next page");
    for (auto *cell : {previous, next}) {
        cell->setKind(ButtonKind::Quiet);
    }
    previous->setGlyph(Icon::ChevronLeft, context.px(36));
    next->setGlyph(Icon::ChevronRight, context.px(36));
    previous->setRules(Qt::RightEdge);
    next->setRules(Qt::LeftEdge);
    counter = new QLabel(this);
    counter->setObjectName(prefix + "PageCount");
    counter->setAlignment(Qt::AlignCenter);
    context.apply(counter, 28);
    counter->setStyleSheet(counter->styleSheet() + "background:transparent;");
    setFixedHeight(context.px(112));
}
void PagerBar::resizeEvent(QResizeEvent *) {
    const int top = qMax(1, context_.px(1)), cell = context_.px(118);
    previous->setGeometry(0, top, cell, height() - top);
    next->setGeometry(width() - cell, top, cell, height() - top);
    counter->setGeometry(cell, top, qMax(1, width() - 2 * cell), height() - top);
}
void PagerBar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), palette().color(QPalette::Window));
    painter.fillRect(0, 0, width(), qMax(1, context_.px(1)), mid);
}
void PagerBar::setPage(int page, int count) {
    count = qMax(1, count);
    page = qBound(0, page, count - 1);
    counter->setText(QString("Page %1 of %2").arg(page + 1).arg(count));
    previous->setEnabled(page > 0);
    next->setEnabled(page + 1 < count);
    setVisible(count > 1);
}

PagedList::PagedList(const Context &context, const QString &prefix, QWidget *parent)
    : QWidget(parent), context_(context), content_(new QWidget(this)),
      pager_(new PagerBar(context, prefix, this)) {
    setObjectName(prefix + "List");
    content_->setObjectName(prefix + "Content");
    pager_->hide();
    QObject::connect(pager_->previous, &QPushButton::clicked, this, [this] {
        anchor_ = nullptr;
        setPage(page_ - 1);
    });
    QObject::connect(pager_->next, &QPushButton::clicked, this, [this] {
        anchor_ = nullptr;
        setPage(page_ + 1);
    });
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
void PagedList::clear() {
    for (auto &item : items_) {
        if (item.widget) {
            delete item.widget.data();
        }
    }
    items_.clear();
    pages_.clear();
    anchor_ = nullptr;
}
void PagedList::keepVisible(QWidget *row) {
    anchor_ = row;
}
int PagedList::anchorPage() const {
    if (anchor_) {
        for (int index = 0; index < pages_.size(); ++index) {
            for (int item = pages_[index].first; item < pages_[index].second; ++item) {
                if (items_[item].widget == anchor_) {
                    return index;
                }
            }
        }
    }
    return -1;
}
void PagedList::addWidget(QWidget *row) {
    row->setParent(content_);
    row->hide();
    items_.append({row});
}
void PagedList::setPage(int page) {
    if (pages_.isEmpty()) {
        paginate();
    }
    const int anchored = anchorPage();
    const int bounded = anchored >= 0 ? anchored : qBound(0, page, qMax(0, int(pages_.size()) - 1));
    page_ = bounded;
    place();
}
void PagedList::resizeEvent(QResizeEvent *) {
    paginate();
}
void PagedList::showEvent(QShowEvent *) {
    paginate();
}
int PagedList::measure(const Item &item, int width) const {
    QWidget *widget = item.widget;
    polishTree(widget);
    if (widget->minimumHeight() == widget->maximumHeight()) {
        return widget->minimumHeight();
    }
    const int height =
        widget->hasHeightForWidth() ? widget->heightForWidth(width) : widget->sizeHint().height();
    return qMax(widget->minimumHeight(), height);
}
void PagedList::paginate() {
    const int width = qMax(1, this->width());
    QVector<int> heights;
    int total = 0;
    for (const auto &item : items_) {
        heights.append(measure(item, width));
        total += heights.last();
    }
    pages_.clear();
    if (total <= height()) {
        pages_.append({0, int(items_.size())});
    } else {
        const int available = qMax(1, height() - pager_->height());
        int index = 0;
        while (index < items_.size()) {
            const int start = index;
            int used = 0;
            while (index < items_.size() &&
                   (index == start || used + heights[index] <= available)) {
                used += heights[index];
                ++index;
            }
            pages_.append({start, index});
        }
    }
    if (pages_.isEmpty()) {
        pages_.append({0, 0});
    }
    page_ = qBound(0, page_, int(pages_.size()) - 1);
    if (anchorPage() >= 0) {
        page_ = anchorPage();
    }
    place();
}
void PagedList::place() {
    if (pages_.isEmpty()) {
        return;
    }
    const bool paged = pages_.size() > 1;
    const int width = qMax(1, this->width()),
              pagerHeight = paged ? pager_->height() : 0;
    content_->setGeometry(0, 0, this->width(), qMax(0, height() - pagerHeight));
    pager_->setGeometry(0, height() - pager_->height(), this->width(), pager_->height());
    pager_->setPage(page_, pages_.size());
    // Rows that stay on the page keep their widgets as they are. Hiding and
    // showing them again would redraw them on e-ink for nothing.
    for (int index = 0; index < items_.size(); ++index) {
        if (items_[index].widget &&
            (index < pages_[page_].first || index >= pages_[page_].second)) {
            items_[index].widget->hide();
        }
    }
    int top = 0;
    for (int index = pages_[page_].first; index < pages_[page_].second; ++index) {
        const auto &item = items_[index];
        const int height = measure(item, width);
        if (item.widget) {
            item.widget->setGeometry(0, top, width, height);
            item.widget->show();
        }
        top += height;
    }
}

} // namespace ui
} // namespace NickelModManager
