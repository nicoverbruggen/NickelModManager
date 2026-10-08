#pragma once

namespace NickelModManager {
// startModManager runs once at Qt startup in Nickel's GUI process. It handles
// uninstall requests and arms the startup failsafe before creating the store.
void startModManager();
} // namespace NickelModManager
