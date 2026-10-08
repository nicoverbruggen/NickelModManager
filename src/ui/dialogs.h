#pragma once
#include "context.h"
#include <QVector>
class QLabel;
namespace NickelModManager {
namespace ui {
struct DialogAction {
    QString text, name;
};
// A bordered card over a capture of the window below it. The first action is
// primary. Returns the chosen action's index, or -1 when nothing was chosen.
int choose(QWidget *parent, const Context &context, const QString &title, const QString &message,
           const QString &detail, const QVector<DialogAction> &actions);

// showStatus shows a card with only a title over the window and returns once it
// is painted. It has no buttons and stays until the caller closes it or the
// process ends. The dialog deletes itself when closed.
QWidget *showStatus(QWidget *parent, const Context &context, const QString &title);
} // namespace ui
} // namespace NickelModManager
