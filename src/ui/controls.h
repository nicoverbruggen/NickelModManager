#pragma once
#include "context.h"
#include "icons.h"
#include <QCheckBox>
#include <QPushButton>
class QLabel;
namespace NickelModManager {
namespace ui {
// On Qt 5, makes a button take touch events itself and click when the
// finger lifts inside it; Nickel 4.x delivers taps only as touch. Does
// nothing on Qt 6.
void enableTouch(QAbstractButton *button);

class Toggle final : public QCheckBox {
  public:
    Toggle(const QString &name, const Context &context, QWidget *parent = nullptr);

  protected:
    bool hitButton(const QPoint &point) const override;
    void paintEvent(QPaintEvent *) override;

  private:
    Context context_;
};
// Every button inverts while pressed: ink fill and paper text. A filled
// button lightens to dark grey instead, so it still reads as the same button.
enum class ButtonKind { Outlined, Quiet, Primary };
class ActionButton : public QPushButton {
  public:
    ActionButton(const QString &caption, QWidget *parent = nullptr);
    ActionButton(const QString &caption, const Context &context, QWidget *parent = nullptr);
    void setKind(ButtonKind kind);
    ButtonKind kind() const {
        return kind_;
    }
    // The glyph is drawn in the current text color, so it inverts with the button.
    void setGlyph(Icon kind, int ink);
    // Draws a hairline along these edges, for cells that sit against a rule.
    // Qt::Edge values combined with |. Qt 5.2 has no Qt::Edges.
    void setRules(int edges) {
        rules_ = edges;
        update();
    }
    // A smaller second line under the caption, such as what the action applies.
    void setDetail(const QString &detail) {
        detail_ = detail;
        setAccessibleDescription(detail);
        updateGeometry();
        update();
    }
    QString detail() const {
        return detail_;
    }
    // A button that holds a layout of labels sizes itself from that layout,
    // so wrapped names and descriptions keep their full height.
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    ButtonKind kind_ = ButtonKind::Outlined;
    bool hasGlyph_ = false;
    Icon glyph_ = Icon::ChevronRight;
    int ink_ = 0;
    int rules_ = 0;
    QString detail_;
};
class Divider final : public QWidget {
  public:
    Divider(const Context &context, QWidget *parent = nullptr);

  protected:
    void paintEvent(QPaintEvent *) override;
};
// A header with a Back arrow at the start and a serif title centered across
// the full width, shortened with an ellipsis when it does not fit.
class Header final : public QWidget {
  public:
    Header(const Context &context, QWidget *parent = nullptr);
    void setTitle(const QString &title);
    ActionButton *back;
    QLabel *title;

  protected:
    void resizeEvent(QResizeEvent *) override;
    void paintEvent(QPaintEvent *) override;

  private:
    void arrange();
    QString text_;
    Context context_;
};
} // namespace ui
} // namespace NickelModManager
