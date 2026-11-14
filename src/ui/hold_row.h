#pragma once
#include <QFrame>
#include <QPointF>
#include <QTimer>
#include <functional>

namespace NickelModManager {
namespace ui {
// A row body accepts touch directly on both firmware generations. Its child
// labels pass input through; controls such as the switch keep their own input.
class HoldRow final : public QFrame {
  public:
    explicit HoldRow(int movementTolerance, QWidget *parent = nullptr);
    std::function<void()> held;
    std::function<void()> tapped;

  protected:
    bool event(QEvent *event) override;

  private:
    void begin(const QPointF &point);
    void move(const QPointF &point);
    void cancel();
    QTimer timer_;
    QPointF origin_;
    int tolerance_;
    bool tracking_ = false;
};
} // namespace ui
} // namespace NickelModManager
