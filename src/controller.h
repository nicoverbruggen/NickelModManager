#pragma once
#include "mods/store.h"
#include <functional>

namespace NickelModManager {
// startController connects the process-lifetime store to the manager and Nickel.
// Call once on the GUI thread after store.start(). The application owns the controller.
// homeReady runs once when Nickel integration reports that Home is visible.
void startController(ModStore *store, StartReport report, const QString &self,
                     const QString &storage, std::function<void()> homeReady);
} // namespace NickelModManager
