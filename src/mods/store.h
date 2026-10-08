#pragma once
#include "mod.h"
#include <QString>
#include <QStringList>
#include <QVector>

namespace NickelModManager {
// What start() did, for the cards NickelModManager shows once Home is up.
struct StartReport {
    QStringList restored, removedOutside, parked, errors;
};

// ModStore manages the libraries in plugins and their copies in storage/mods.
// storage/state.json records the desired state and the firmware identity.
class ModStore {
  public:
    // ModStore takes paths to the plugin and storage directories, its own
    // library basename and the current firmware identity. putBack permits
    // restoration after a firmware change. Use it only on firmware 5.x/6.x;
    // firmware 4.x preserves mods during updates.
    ModStore(QString plugins, QString storage, QString self, QString firmware, bool putBack);

    // start reads saved state and reconciles it with the installed files.
    // Call once at Nickel startup, before other operations. Returns the changes
    // and errors for this start; an error does not undo earlier file changes.
    StartReport start();

    // rescan reconciles changes made while Nickel runs. It preserves the
    // loaded flags of known mods and does not repeat the firmware check.
    // File and state errors are not returned.
    void rescan();

    // setEnabled changes the selection for the next start. Off removes the
    // live and failsafe files after saving their bytes; on restores a library.
    // Returns false with error set on failure. Completed file changes are not
    // rolled back if a later operation, including saving state, fails.
    bool setEnabled(const QString &file, bool enabled, QString &error);

    // mods returns a borrowed view. Do not retain element references across
    // start or rescan, which can append or reorder entries.
    const QVector<Mod> &mods() const { return mods_; }

    // keptOn returns paths to existing saved copies selected for the next start.
    QStringList keptOn() const;

    // pendingChanges counts differences between enabled and the loaded baseline.
    int pendingChanges() const;

    // librarySize returns the size in bytes of the installed library, the held
    // failsafe copy or the saved copy, in that order, or -1 when none exists.
    qint64 librarySize(const QString &file) const;

    // failsafe returns the held library's path, or an empty string if absent.
    QString failsafe(const QString &file) const;

  private:
    void loadState(StartReport &report);
    // Records the bytes and dates of the library at path for mod.
    void observe(Mod &mod, const QString &path, const QString &hash);
    QString saveAndRemoveLibrary(const QString &filename, QString &error);
    bool restoreLibrary(Mod &mod, QString &error);
    void recordEnabledState(Mod &mod, bool enabled);

    QStringList candidateLibraries() const;
    QString savedLibraryPath(const QString &filename) const;
    void discoverStartupLibraries(StartReport &report);
    bool recordStartupLibrary(const QString &file, StartReport &report);
    void restoreMissingLibraries(StartReport &report);
    void forgetMissingCopies();
    void sortMods();
    void rescanLibrary(const QString &file);
    void recordRemovedLibraries(const QStringList &candidates);
    bool saveState(QString &error);
    Mod *find(const QString &file);
    Mod &ensureMod(const QString &filename);
    bool refreshSavedLibrary(const Mod &mod, const QString &path, const QString &hash,
                             QString &error);
    bool recordStartupStatus(Mod &mod, bool parked, StartReport &report);
    void recordRescanStatus(Mod &mod, bool held);
    QString plugins_, storage_, self_, firmware_, lastFirmware_;
    bool putBack_;
    // No state.json existed: the mods found now were installed earlier.
    bool fresh_ = false;
    QVector<Mod> mods_;
    // The firmware identity differs from the one saved by the last start.
    bool changed_ = false;
};

} // namespace NickelModManager
