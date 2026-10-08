#pragma once
#include <functional>
class QObject;
class QWidget;

namespace NickelModManager {
// attachNickel adds Manage Mods to More and reports Home readiness once.
// owner owns the event observer and must outlive its callbacks. Call on the GUI thread.
void attachNickel(QObject *owner, std::function<void()> openManager,
                  std::function<void(QWidget *)> reachedHome);
} // namespace NickelModManager
