#pragma once
#include "mod.h"
#include <QHash>
#include <QVector>

namespace NickelModManager {
// modVersions reads optional mod.manifest files in immediate .adds folders.
// Only schema 1 declarations matching a known library's SHA-256 supply a
// version. Missing, invalid, stale and ambiguous declarations are ignored.
// This reads metadata only; it never changes a mod's config or saved state.
QHash<QString, QString> modVersions(const QString &adds, const QVector<Mod> &mods);
} // namespace NickelModManager
