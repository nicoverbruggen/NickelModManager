#pragma once
#include <QByteArray>
#include <QString>

namespace NickelModManager {
QByteArray nativePath(const QString &path);
QString systemError();
bool isRegularFile(const QString &path);

// Sync the containing directory after rename or removal. This is best effort;
// an unavailable directory or failed sync does not change the caller's result.
void syncDirectory(const QString &path);

// Query the linker rather than the current filename. NickelHook may have
// renamed a loaded library into its failsafe location in this process.
bool libraryIsLoaded(const QString &path);

// hashFile returns the SHA-256 of a regular file of at most 32 MiB, or an
// empty string with error set. It refuses symlinks.
QString hashFile(const QString &path, QString &error);

// copyFile copies through a temporary file in scratch, or the target's folder
// if scratch is empty, then renames it into place after checking expected.
// Returns false with error set on failure. Qt6 loads every file in a plugin
// folder, so scratch must be outside that folder and on the same filesystem.
bool copyFile(const QString &source,
              const QString &target,
              const QString &expected,
              QString &error,
              const QString &scratch = QString());
} // namespace NickelModManager
