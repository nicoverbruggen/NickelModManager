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
} // namespace ui
} // namespace NickelModManager
