#pragma once
#include "context.h"
#include <QPointer>
#include <QPushButton>
class QLabel;
namespace NickelModManager {
namespace ui {
// A row for a host menu that copies a native row's font, text inset, icon
// column, height and bottom rule at paint time instead of using its class.
class MenuEntry final : public QPushButton {
  public:
    MenuEntry(const QString &caption, QWidget *reference, QWidget *parent);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override {
        return sizeHint();
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *) override;

  private:
    QPointer<QWidget> reference_;
};
} // namespace ui
} // namespace NickelModManager
