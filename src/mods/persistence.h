#pragma once
#include "mod.h"
#include <QVector>

namespace NickelModManager {
// Only durable fields are written. loaded and parked in Mod belong to the
// running process and are left at their defaults when reading this snapshot.
struct SavedModState {
    QString firmware;
    QVector<Mod> mods;
};

struct LoadedModState {
    SavedModState state;
    bool fresh = false; // Missing or damaged state requires initial discovery.
    QString error;
};

// loadModState reads and validates saved state. Exclude stock libraries and the manager.
// Missing state is normal; damaged state is kept as .bad and reported in error.
// Neither case decides which installed mods should be enabled.
LoadedModState loadModState(const QString &path, const QString &managerFilename);

// saveModState replaces saved state through QSaveFile. Return false with error set if writing
// or committing fails. Session-only fields are never serialized.
bool saveModState(const QString &path, const SavedModState &state, QString &error);
} // namespace NickelModManager
