#include "nickel.h"
#include "compat.h"
#include "ui/menu.h"
#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QPushButton>
#include <utility>

namespace NickelModManager {
namespace {
// Nickel builds More on demand. Observe widget events and inspect after layout.
class Integration final : public QObject {
  public:
    Integration(QObject *owner, std::function<void()> openManager,
                std::function<void(QWidget *)> reachedHome)
        : QObject(owner), openManager_(std::move(openManager)),
          reachedHome_(std::move(reachedHome)) {
        qApp->installEventFilter(this);
        queueRoute();
    }

  private:
    std::function<void()> openManager_;
    std::function<void(QWidget *)> reachedHome_;
    bool routeQueued_ = false;
    bool home_ = false;
    void inspectRoute() {
        // Widget names identify Nickel's controls without depending on their
        // translated labels. Skip layouts that do not match the expected shape.
        QWidget *main = nullptr;
        for (QWidget *widget : QApplication::allWidgets()) {
            if (widget->objectName() == "MainWindow" &&
                QByteArray(widget->metaObject()->className()) == "MainWindowView" &&
                widget->isVisible()) {
                main = widget;
            }
            insertMenuEntry(widget);
        }
        if (main && !home_) {
            auto *more = main->findChild<QWidget *>("moreButton");
            if (more && more->isVisible()) {
                home_ = true;
                reachedHome_(main);
            }
        }
    }
    // More is built on demand. Match widget names and layout shape, rather
    // than translated labels, and use the Help row as the insertion anchor.
    void insertMenuEntry(QWidget *widget) {
        // More is built when it first opens. Its Help row is the anchor:
        // Manage Mods goes directly above it, which is below Settings.
        if (widget->objectName() != "helpButton") {
            return;
        }
        auto *parent = widget->parentWidget();
        auto *layout = parent ? qobject_cast<QBoxLayout *>(parent->layout()) : nullptr;
        if (!layout || layout->indexOf(widget) < 0 ||
            parent->findChild<QPushButton *>("nickelModManagerButton")) {
            return;
        }
        auto *button = new NickelModManager::ui::MenuEntry("Manage Mods", widget, parent);
        button->setObjectName("nickelModManagerButton");
        layout->insertWidget(layout->indexOf(widget), button);
        connect(button, &QPushButton::clicked, this, [this] { openManager_(); });
    }

    void queueRoute() {
        // ChildAdded can arrive before a widget has its final layout. Inspect
        // after the current event, and merge repeated events into one pass.
        if (routeQueued_) {
            return;
        }
        routeQueued_ = true;
        NickelModManager::later(this, 0, [this] {
            routeQueued_ = false;
            inspectRoute();
        });
    }
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::Show || event->type() == QEvent::ChildAdded ||
            event->type() == QEvent::Polish) {
            queueRoute();
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

void attachNickel(QObject *owner, std::function<void()> openManager,
                  std::function<void(QWidget *)> reachedHome) {
    new Integration(owner, std::move(openManager), std::move(reachedHome));
}
} // namespace NickelModManager
