#pragma once
#include <QString>

namespace NickelModManager {
// One library in Nickel's plugin folder, or one the user turned off.
struct Mod {
    Mod() {}
    Mod(const QString &file, const QString &name) : file(file), name(name) {}
    QString file, name, hash;
    // Whether the mod should load at the next start. An active NickelHook
    // failsafe can temporarily hold its library outside the plugin folder.
    bool enabled = false;
    // The state used to compare this start with the next. Libraries present
    // at startup count as loaded, even if Qt has not visited them yet. A mod
    // discovered later counts as loaded only if the linker has it in memory.
    bool loaded = false;
    QString note; // Reason for an automatic state change, shown in the manager.
    // NickelHook's failsafe holds the library without an active load. A
    // failsafe in progress during a successful start does not count as parked.
    bool parked = false;
    qint64 built =
        0; // Library mtime in epoch seconds, preserved by Kobo's installer. 0 if unknown.
    qint64 added = 0; // First observation of these bytes. 0 for mods found at initial setup.
};

// Accept only .so basenames which can safely become storage paths.
bool validModFilename(const QString &filename);

// Use a known project name, or the basename without lib/.so.
QString modDisplayName(const QString &filename);

bool isStockLibrary(const QString &filename);
} // namespace NickelModManager
