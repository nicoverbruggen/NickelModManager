#include "store.h"
#include "files.h"
#include "persistence.h"
#include "compat.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <sys/stat.h>
#include <unistd.h>

namespace NickelModManager {
ModStore::ModStore(QString plugins, QString storage, QString self, QString firmware, bool putBack)
    : plugins_(std::move(plugins)), storage_(std::move(storage)), self_(std::move(self)),
      firmware_(std::move(firmware)), putBack_(putBack) {
}

// While a NickelHook mod starts, its failsafe moves the library out of the
// way and moves it back after the mod's delay. When Nickel stops before then,
// or when the Qt 6 port's compatibility check fails, the library stays there.
// Upstream NickelHook renames it in place to <file>.failsafe. Its Qt 6 port
// moves it one folder up, because Qt 6 loads any file in a plugin folder.
QString ModStore::failsafe(const QString &file) const {
    for (const QString &path : {plugins_ + "/" + file + ".failsafe",
                                QFileInfo(plugins_).absolutePath() + "/" + file + ".failsafe"}) {
        if (isRegularFile(path)) {
            return path;
        }
    }
    return QString();
}

namespace {
// Save each file before removing it. A missing file needs no work; a failed
// backup must leave the installed file available. Earlier removals remain if
// a later removal fails. removedHash records the first file processed.
bool saveAndRemoveFile(const QString &path,
                       const QString &savedPath,
                       QString &removedHash,
                       QString &error) {
    if (path.isEmpty() || !isRegularFile(path)) {
        return true;
    }

    const QString sourceHash = hashFile(path, error);
    if (sourceHash.isEmpty()) {
        return false;
    }

    const bool savedCopyMatches = hashFile(savedPath, error) == sourceHash;
    if (!savedCopyMatches) {
        if (!copyFile(path, savedPath, sourceHash, error)) {
            return false;
        }
    }

    if (unlink(nativePath(path).constData()) != 0) {
        error = path + ": " + systemError();
        return false;
    }
    syncDirectory(QFileInfo(path).absolutePath());

    if (removedHash.isEmpty()) {
        removedHash = sourceHash;
    }
    return true;
}

} // namespace

// Keep a copy before removing a mod. Remove the failsafe first so NickelHook
// cannot restore it later. If both files are already absent, retain the saved
// hash so turning off an already disabled mod still succeeds.
QString ModStore::saveAndRemoveLibrary(const QString &filename, QString &error) {
    const QString installedPath = plugins_ + "/" + filename;
    const QString savedPath = storage_ + "/mods/" + filename;
    QString removedHash;

    for (const QString &path : {failsafe(filename), installedPath}) {
        if (!saveAndRemoveFile(path, savedPath, removedHash, error)) {
            return {};
        }
    }

    if (!removedHash.isEmpty()) {
        return removedHash;
    }
    const Mod *mod = find(filename);
    return mod ? mod->hash : QString();
}

Mod *ModStore::find(const QString &file) {
    for (auto &mod : mods_) {
        if (mod.file == file) {
            return &mod;
        }
    }
    return nullptr;
}

// Discovery can append and reallocate the mod list. Use this reference only
// until the next call which adds a mod; do not retain it in the UI.
Mod &ModStore::ensureMod(const QString &filename) {
    if (Mod *mod = find(filename)) {
        return *mod;
    }
    mods_.append(Mod(filename, modDisplayName(filename)));
    return mods_.last();
}

// Keep saved bytes current for both startup discovery and session rescans.
// The caller decides how to report a failed backup and still observes the file.
bool ModStore::refreshSavedLibrary(const Mod &mod,
                                   const QString &path,
                                   const QString &hash,
                                   QString &error) {
    const QString savedPath = savedLibraryPath(mod.file);
    if (mod.hash == hash && isRegularFile(savedPath)) {
        return true;
    }
    return copyFile(path, savedPath, hash, error);
}

// --- state snapshots

// Loading happens once at startup. Rescans preserve the session's baseline
// rather than rereading the previous boot's state.
void ModStore::loadState(StartReport &report) {
    const LoadedModState loaded = loadModState(storage_ + "/state.json", self_);
    mods_ = loaded.state.mods;
    lastFirmware_ = loaded.state.firmware;
    fresh_ = loaded.fresh;
    if (!loaded.error.isEmpty()) {
        report.errors << loaded.error;
    }
}

bool ModStore::saveState(QString &error) {
    SavedModState state;
    state.mods = mods_;
    state.firmware = lastFirmware_;
    return saveModState(storage_ + "/state.json", state, error);
}

// --- startup recovery

StartReport ModStore::start() {
    StartReport report;
    QString error;

    if (!QDir().mkpath(storage_ + "/mods")) {
        report.errors << "Could not create " + storage_ + "/mods";
    }
    loadState(report);

    discoverStartupLibraries(report);
    changed_ = !lastFirmware_.isEmpty() && !firmware_.isEmpty() && lastFirmware_ != firmware_;
    restoreMissingLibraries(report);
    forgetMissingCopies();
    sortMods();

    if (!firmware_.isEmpty()) {
        lastFirmware_ = firmware_;
    }
    fresh_ = false;
    if (!saveState(error)) {
        report.errors << error;
    }
    return report;
}

// Both startup and rescan must find the two NickelHook failsafe locations.
// Keep basenames here; each caller checks whether a candidate is manageable.
QStringList ModStore::candidateLibraries() const {
    QStringList candidates = QDir(plugins_).entryList(
        {"*.so"}, QDir::Files | QDir::NoSymLinks | QDir::Hidden, QDir::Name);
    for (const QString &folder : {plugins_, QFileInfo(plugins_).absolutePath()}) {
        for (const QString &name : QDir(folder).entryList(
                 {"*.so.failsafe"}, QDir::Files | QDir::NoSymLinks | QDir::Hidden)) {
            candidates << name.left(name.size() - 9);
        }
    }
    candidates.removeDuplicates();
    return candidates;
}

QString ModStore::savedLibraryPath(const QString &filename) const {
    return storage_ + "/mods/" + filename;
}

void ModStore::discoverStartupLibraries(StartReport &report) {
    for (const auto &filename : candidateLibraries()) {
        recordStartupLibrary(filename, report);
    }
}

// Record installed bytes even if the backup fails, and report that error.
// A live file counts as loaded because Qt may visit it later in this startup.
// A held file counts only when this process already has the library loaded.
bool ModStore::recordStartupLibrary(const QString &file, StartReport &report) {
    QString error;
    if (!validModFilename(file) || isStockLibrary(file) || file == self_) {
        return false;
    }
    QString path = plugins_ + "/" + file;
    // A failsafe that holds a library this process loaded is NickelHook
    // at work in this start. Otherwise the library was parked earlier.
    bool parked = false;
    if (!isRegularFile(path)) {
        path = failsafe(file);
        if (path.isEmpty()) {
            return false;
        }
        parked = !libraryIsLoaded(plugins_ + "/" + file);
    }
    const QString hash = hashFile(path, error);
    if (hash.isEmpty()) {
        report.errors << error;
        return false;
    }
    Mod &mod = ensureMod(file);
    if (!refreshSavedLibrary(mod, path, hash, error)) {
        report.errors << error;
    }
    observe(mod, path, hash);
    return recordStartupStatus(mod, parked, report);
}

// The plugin folder establishes the startup baseline even if Qt has not yet
// loaded a library. A parked failsafe has no active mod and stays disabled.
bool ModStore::recordStartupStatus(Mod &mod, bool parked, StartReport &report) {
    if (parked) {
        mod.enabled = false;
        mod.loaded = false;
        mod.parked = true;
        mod.note = "It may be broken or not work with this firmware.";
        report.parked << mod.file;
        return false;
    }
    mod.note.clear();
    mod.enabled = true;
    mod.loaded = true;
    return true;
}

// Firmware 5.x/6.x can replace the whole system partition. Restore missing
// enabled mods only after such a firmware change; other removals stay off.
void ModStore::restoreMissingLibraries(StartReport &report) {
    QString error;
    const bool updated = putBack_ && changed_;
    for (auto &mod : mods_) {
        if (mod.loaded || !mod.enabled) {
            continue;
        }
        if (!updated) {
            mod.enabled = false;
            mod.note = "Removed outside Manage Mods. Turn it on to put the kept copy back.";
            report.removedOutside << mod.file;
        } else if (copyFile(savedLibraryPath(mod.file),
                            plugins_ + "/" + mod.file,
                            mod.hash,
                            error,
                            QFileInfo(plugins_).absolutePath())) {
            mod.note = "Put back after the firmware update.";
            report.restored << mod.file;
        } else {
            mod.enabled = false;
            mod.note = "Could not be put back after the firmware update.";
            report.errors << error;
        }
    }
}

void ModStore::forgetMissingCopies() {
    mods_.erase(std::remove_if(mods_.begin(),
                               mods_.end(),
                               [&](const Mod &mod) {
                                   return !mod.loaded && !mod.enabled &&
                                          !isRegularFile(savedLibraryPath(mod.file));
                               }),
                mods_.end());
}

void ModStore::sortMods() {
    std::sort(mods_.begin(), mods_.end(), [](const Mod &a, const Mod &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

// --- session changes

void ModStore::observe(Mod &mod, const QString &path, const QString &hash) {
    // An unchanged hash keeps the install date. On first setup, the time
    // when an existing mod was installed is unknown, so leave it unset.
    struct stat st;
    if (!lstat(nativePath(path).constData(), &st)) {
        mod.built = qint64(st.st_mtime);
    }
    if (mod.hash != hash && !(fresh_ && mod.hash.isEmpty())) {
        mod.added = QDateTime::currentMSecsSinceEpoch() / 1000;
    }
    mod.hash = hash;
}

void ModStore::rescan() {
    const QStringList candidates = candidateLibraries();
    for (const auto &filename : candidates) {
        rescanLibrary(filename);
    }
    recordRemovedLibraries(candidates);
    sortMods();

    QString error;
    saveState(error);
}

// A rescan observes installers and completed failsafes without changing the
// startup baseline of known mods. Newly discovered mods use the linker state.
void ModStore::rescanLibrary(const QString &file) {
    QString error;
    if (!validModFilename(file) || isStockLibrary(file) || file == self_) {
        return;
    }
    QString path = plugins_ + "/" + file;
    const bool held = !isRegularFile(path);
    if (held) {
        path = failsafe(file);
    }
    if (path.isEmpty()) {
        return;
    }
    const QString hash = hashFile(path, error);
    if (hash.isEmpty()) {
        return;
    }
    Mod *mod = find(file);
    if (!mod) {
        // A library that appeared during this session loads at the next start.
        mods_.append(Mod(file, modDisplayName(file)));
        mod = &mods_.last();
        mod->loaded = libraryIsLoaded(plugins_ + "/" + file);
    }
    refreshSavedLibrary(*mod, path, hash, error);
    observe(*mod, path, hash);
    recordRescanStatus(*mod, held);
}

// Preserve loaded during rescans so completed failsafes and external installers
// update the desired state without losing pending restart changes.
void ModStore::recordRescanStatus(Mod &mod, bool held) {
    if (held && !libraryIsLoaded(plugins_ + "/" + mod.file)) {
        mod.enabled = false;
        mod.parked = true;
        mod.note = "It may be broken or not work with this firmware.";
        return;
    }
    if (!mod.enabled || mod.parked) {
        mod.note.clear();
    }
    mod.enabled = true;
    mod.parked = false;
}

// Missing enabled libraries were removed during this session. Keep their
// saved copies so the user can turn them on again.
void ModStore::recordRemovedLibraries(const QStringList &candidates) {
    for (auto &mod : mods_) {
        if (!mod.enabled || candidates.contains(mod.file)) {
            continue;
        }
        mod.enabled = false;
        mod.note = "Removed outside Manage Mods. Turn it on to put the kept copy back.";
    }
}

bool ModStore::setEnabled(const QString &filename, bool enabled, QString &error) {
    Mod *mod = find(filename);
    if (!mod) {
        error = filename + " is not a known mod.";
        return false;
    }

    if (enabled) {
        if (!restoreLibrary(*mod, error)) {
            return false;
        }
    } else {
        const QString hash = saveAndRemoveLibrary(filename, error);
        if (hash.isEmpty()) {
            return false;
        }
        mod->hash = hash;
    }

    recordEnabledState(*mod, enabled);
    return saveState(error);
}

// Prefer the failsafe's actual bytes over an older saved copy. Copy them to
// storage before moving the held file back. A regular installed file needs
// no restoration. Completed copies remain if the later rename fails.
bool ModStore::restoreLibrary(Mod &mod, QString &error) {
    const QString installedPath = plugins_ + "/" + mod.file;
    const QString savedPath = savedLibraryPath(mod.file);
    if (isRegularFile(installedPath)) {
        return true;
    }

    const QString heldPath = failsafe(mod.file);
    if (heldPath.isEmpty()) {
        return copyFile(
            savedPath, installedPath, mod.hash, error, QFileInfo(plugins_).absolutePath());
    }

    const QString hash = hashFile(heldPath, error);
    if (hash.isEmpty()) {
        return false;
    }
    if (hashFile(savedPath, error) != hash && !copyFile(heldPath, savedPath, hash, error)) {
        return false;
    }
    if (rename(nativePath(heldPath).constData(), nativePath(installedPath).constData()) != 0) {
        error = installedPath + ": " + systemError();
        return false;
    }

    syncDirectory(QFileInfo(heldPath).absolutePath());
    syncDirectory(plugins_);
    mod.hash = hash;
    return true;
}

// Record the selection only after the file operation succeeds.
// Toggling cannot unload a library, so loaded remains the startup baseline.
void ModStore::recordEnabledState(Mod &mod, bool enabled) {
    mod.enabled = enabled;
    mod.parked = false;
    mod.note.clear();
}

QStringList ModStore::keptOn() const {
    QStringList result;
    for (const auto &mod : mods_) {
        if (mod.enabled && isRegularFile(storage_ + "/mods/" + mod.file)) {
            result << storage_ + "/mods/" + mod.file;
        }
    }
    return result;
}

qint64 ModStore::librarySize(const QString &file) const {
    for (const QString &path : {plugins_ + "/" + file, failsafe(file), savedLibraryPath(file)}) {
        if (!path.isEmpty() && isRegularFile(path)) {
            return QFileInfo(path).size();
        }
    }
    return -1;
}

int ModStore::pendingChanges() const {
    return int(std::count_if(mods_.begin(), mods_.end(), [](const Mod &mod) {
        return mod.enabled != mod.loaded;
    }));
}
} // namespace NickelModManager
