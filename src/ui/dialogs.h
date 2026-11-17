#pragma once
#include "context.h"
#include <QVector>
class QLabel;
namespace NickelModManager {
namespace ui {
struct DialogAction {
    QString text, name;
    bool enabled = true;
    bool bold = false;
};
// A bordered card with full-width action rows over a capture of the window
// below it. Returns the chosen index, or -1 when nothing was chosen.
int choose(QWidget *parent, const Context &context, const QString &title, const QString &message,
           const QString &detail, const QVector<DialogAction> &actions);

// confirmModRemoval opens the confirmation shared by the row and About.
bool confirmModRemoval(QWidget *parent, const Context &context, const QString &name, bool available);

// showStatus shows a card with only a title over the window and returns once it
// is painted. It has no buttons and stays until the caller closes it or the
// process ends. The dialog deletes itself when closed.
QWidget *showStatus(QWidget *parent, const Context &context, const QString &title);
} // namespace ui
} // namespace NickelModManager
