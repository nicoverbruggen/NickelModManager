#pragma once
#include <functional>
class QObject;
class QWidget;
class QFont;

namespace NickelModManager {
// attachNickel adds Manage Mods to More and reports Home readiness once.
// openManager receives the stock Help label's font when the entry is tapped.
// owner owns the event observer and must outlive its callbacks. Call on the GUI thread.
void attachNickel(QObject *owner, std::function<void(const QFont &)> openManager,
                  std::function<void(QWidget *)> reachedHome);
} // namespace NickelModManager
