#include "nickel.h"
#include "compat.h"
#include "ui/menu.h"
#include <QApplication>
#include <QBoxLayout>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <utility>

namespace NickelModManager {
namespace {
// Nickel builds More on demand. Observe widget events and inspect after layout.
class Integration final : public QObject {
  public:
    Integration(QObject *owner, std::function<void(const QFont &)> openManager,
                std::function<void(QWidget *)> reachedHome)
        : QObject(owner), openManager_(std::move(openManager)),
          reachedHome_(std::move(reachedHome)) {
        qApp->installEventFilter(this);
        queueRoute();
    }

  private:
    std::function<void(const QFont &)> openManager_;
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
    // Nickel builds More when it first opens. Help is the anchor, matched by
    // widget name and layout rather than translated labels: Manage Mods goes
    // directly above it, which is below Settings.
    void insertMenuEntry(QWidget *widget) {
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
        // A layout shows a new child of a visible parent only in a later event.
        // Show it now so it is part of the next paint.
        button->show();
        const QPointer<QWidget> reference(widget);
        connect(button, &QPushButton::clicked, this, [this, reference, button] {
            // The stock label distinguishes Tolino's Bariol UI from Kobo's
            // fonts without a brand flag or a private Nickel symbol.
            auto *label = reference ? reference->findChild<QLabel *>("label") : nullptr;
            openManager_(label ? label->font() : button->font());
        });
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
        const auto type = event->type();
        // Insert Manage Mods while Help is polished or shown, before More is
        // first drawn. The queued pass below runs after that paint, and on
        // e-ink the row would then appear in a second redraw.
        if ((type == QEvent::Polish || type == QEvent::Show) && watched->isWidgetType() &&
            watched->objectName() == "helpButton") {
            insertMenuEntry(static_cast<QWidget *>(watched));
        }
        if (type == QEvent::Show || type == QEvent::ChildAdded || type == QEvent::Polish) {
            queueRoute();
        }
        return QObject::eventFilter(watched, event);
    }
};

} // namespace

void attachNickel(QObject *owner, std::function<void(const QFont &)> openManager,
                  std::function<void(QWidget *)> reachedHome) {
    new Integration(owner, std::move(openManager), std::move(reachedHome));
}
} // namespace NickelModManager
