#pragma once
#include "context.h"
#include <QPointer>
#include <QVector>
class QLabel;
namespace NickelModManager {
namespace ui {
class ActionButton;
// Kobalt's pager: a Previous cell, the page count and a Next cell under a
// hairline. It hides itself when everything fits on one page.
class PagerBar final : public QWidget {
  public:
    PagerBar(const Context &context, const QString &prefix, QWidget *parent = nullptr);
    void setPage(int page, int count);
    ActionButton *previous, *next;
    QLabel *counter;

  protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

  private:
    Context context_;
};
// Rows on pages instead of a scroll area, which flickers on e-ink. Rows span
// the full width, supply their own margins and are never cut at a page edge.
class PagedList final : public QWidget {
  public:
    PagedList(const Context &context, const QString &prefix, QWidget *parent = nullptr);
    void clear();
    void addWidget(QWidget *row);
    void setPage(int page);
    // Shows the page holding this row, also after the list is paged again
    // because its height changed. Turning a page by hand releases it.
    void keepVisible(QWidget *row);
    int page() const {
        return page_;
    }
    QWidget *content() const {
        return content_;
    }

  protected:
    void resizeEvent(QResizeEvent *) override;
    void showEvent(QShowEvent *) override;

  private:
    // No member defaults: GCC 4.9 cannot brace-initialise such a struct.
    struct Item {
        QPointer<QWidget> widget;
    };
    void paginate();
    void place();
    int anchorPage() const;
    int measure(const Item &item, int width) const;
    Context context_;
    QWidget *content_;
    PagerBar *pager_;
    QVector<Item> items_;
    QVector<QPair<int, int>> pages_;
    QPointer<QWidget> anchor_;
    int page_ = 0;
};
} // namespace ui
} // namespace NickelModManager
