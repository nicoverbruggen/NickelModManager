// Checks the store's file and state rules in temporary folders.
#include "mods/store.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <utime.h>

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

struct Device {
    QTemporaryDir root;
    QString plugins() const { return root.path() + "/imageformats"; }
    QString storage() const { return root.path() + "/onboard/.adds/nickel-mod-manager"; }
    Device() { QDir().mkpath(plugins()); write(plugins() + "/libnickelmm.so", "loader"); }
    static void write(const QString &path, const QByteArray &bytes) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) std::abort();
    }
    static QByteArray read(const QString &path) {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
    bool live(const QString &file) const { return QFile::exists(plugins() + "/" + file); }
    bool kept(const QString &file) const { return QFile::exists(storage() + "/mods/" + file); }
    NickelModManager::ModStore store(const QString &firmware = "rev-1", bool putBack = true) const {
        return NickelModManager::ModStore(plugins(), storage(), "libnickelmm.so", firmware, putBack);
    }
};
const NickelModManager::Mod *find(const NickelModManager::ModStore &store, const QString &file) {
    for (const auto &mod : store.mods())
        if (mod.file == file) return &mod;
    return nullptr;
}

void firstStartAndToggles() {
    Device device;
    device.write(device.plugins() + "/libnm.so", "menu");
    device.write(device.plugins() + "/libprobe.so", "probe");
    device.write(device.plugins() + "/libkimg.so", "Kobo's own image plugin");
    auto first = device.store();
    const auto report = first.start();
    CHECK(report.errors.isEmpty());
    CHECK(first.mods().size() == 2 && !find(first, "libkimg.so"));
    CHECK(!device.kept("libkimg.so"));
    CHECK(!find(first, "libnickelmm.so"));
    CHECK(find(first, "libnm.so")->name == "NickelMenu" && find(first, "libprobe.so")->name == "probe");
    CHECK(device.kept("libnm.so") && device.kept("libprobe.so"));
    CHECK(first.pendingChanges() == 0);

    QString error;
    CHECK(first.setEnabled("libprobe.so", false, error));
    CHECK(!device.live("libprobe.so") && device.kept("libprobe.so"));
    // The size comes from the installed library, or from the saved copy when off.
    CHECK(first.librarySize("libnm.so") == 4 && first.librarySize("libprobe.so") == 5);
    CHECK(first.librarySize("libmissing.so") == -1);
    CHECK(first.pendingChanges() == 1);
    // Undoing the change in the same session leaves nothing waiting.
    CHECK(first.setEnabled("libprobe.so", true, error));
    CHECK(device.live("libprobe.so") && first.pendingChanges() == 0);
    CHECK(first.setEnabled("libprobe.so", false, error));

    auto second = device.store();
    second.start();
    CHECK(!find(second, "libprobe.so")->enabled && !find(second, "libprobe.so")->loaded);
    CHECK(find(second, "libprobe.so")->note.isEmpty());
    CHECK(second.pendingChanges() == 0);
    CHECK(second.setEnabled("libprobe.so", true, error));
    CHECK(device.read(device.plugins() + "/libprobe.so") == "probe");

    // A mod turned on loads at the next start.
    auto third = device.store();
    third.start();
    CHECK(find(third, "libprobe.so")->loaded && third.pendingChanges() == 0);
}

void removedOutsideAndUpdatedByInstaller() {
    Device device;
    device.write(device.plugins() + "/libnm.so", "menu 1");
    device.store().start();
    // Its own installer replaces it: the copy follows.
    device.write(device.plugins() + "/libnm.so", "menu 2");
    device.store().start();
    CHECK(device.read(device.storage() + "/mods/libnm.so") == "menu 2");
    // Something else removes it while the firmware stays the same, such as
    // a failsafe. The loader does not put it back.
    QFile::remove(device.plugins() + "/libnm.so");
    auto store = device.store();
    const auto report = store.start();
    CHECK(report.removedOutside == QStringList{"libnm.so"});
    CHECK(!device.live("libnm.so") && device.kept("libnm.so"));
    CHECK(!find(store, "libnm.so")->enabled && !find(store, "libnm.so")->note.isEmpty());
    auto later = device.store();
    CHECK(later.start().removedOutside.isEmpty());
    // An installer that puts it back turns it on again.
    device.write(device.plugins() + "/libnm.so", "menu 3");
    auto reinstalled = device.store();
    reinstalled.start();
    CHECK(find(reinstalled, "libnm.so")->enabled && find(reinstalled, "libnm.so")->note.isEmpty());
}

void firmwareUpdate() {
    Device device;
    device.write(device.plugins() + "/libnm.so", "menu");
    device.write(device.plugins() + "/libprobe.so", "probe");
    {
        auto store = device.store("rev-1");
        store.start();
        QString error;
        CHECK(store.setEnabled("libprobe.so", false, error));
    }
    // A full update writes a new system partition: the plugin folder is
    // empty and the loader comes back through its own package.
    QDir(device.plugins()).removeRecursively();
    QDir().mkpath(device.plugins());
    device.write(device.plugins() + "/libnickelmm.so", "loader");
    auto restored = device.store("rev-2");
    const auto report = restored.start();
    CHECK(report.restored == QStringList{"libnm.so"});
    CHECK(device.read(device.plugins() + "/libnm.so") == "menu");
    CHECK(!device.live("libprobe.so"));
    CHECK(restored.pendingChanges() == 1);
    // The next start loads it, and nothing waits any more.
    auto next = device.store("rev-2");
    next.start();
    CHECK(find(next, "libnm.so")->loaded && find(next, "libnm.so")->enabled);
    CHECK(next.pendingChanges() == 0);
}

// Firmware 4.x updates keep the plugin folder, so a mod missing after one
// was removed on purpose and stays off.
void firmwareUpdateWithoutPutBack() {
    Device device;
    device.write(device.plugins() + "/libnm.so", "menu");
    device.store("rev-1", false).start();
    QFile::remove(device.plugins() + "/libnm.so");
    auto store = device.store("rev-2", false);
    const auto report = store.start();
    CHECK(report.restored.isEmpty() && report.removedOutside == QStringList{"libnm.so"});
    CHECK(!device.live("libnm.so") && device.kept("libnm.so"));
    CHECK(!find(store, "libnm.so")->enabled && store.pendingChanges() == 0);
}

// NickelHook's failsafe holds a library that Nickel stopped on: upstream in
// place as <file>.failsafe, the Qt 6 port one folder up.
void failsafe() {
    for (const bool up : {false, true}) {
        Device device;
        const QString held = (up ? device.root.path() : device.plugins()) + "/libnm.so.failsafe";
        device.write(device.plugins() + "/libnm.so", "menu");
        device.store().start();
        QFile::rename(device.plugins() + "/libnm.so", held);
        auto store = device.store();
        const auto report = store.start();
        CHECK(report.parked == QStringList{"libnm.so"} && report.removedOutside.isEmpty());
        CHECK(!find(store, "libnm.so")->enabled && find(store, "libnm.so")->parked);
        CHECK(!find(store, "libnm.so")->note.isEmpty() && store.pendingChanges() == 0);
        QString error;
        CHECK(store.setEnabled("libnm.so", true, error));
        CHECK(device.read(device.plugins() + "/libnm.so") == "menu" && !QFile::exists(held));
        CHECK(store.pendingChanges() == 1);

        // A library the failsafe holds that the loader never saw is listed too.
        Device fresh;
        const QString parked = (up ? fresh.root.path() : fresh.plugins()) + "/libother.so.failsafe";
        fresh.write(parked, "other");
        auto first = fresh.store();
        CHECK(first.start().parked == QStringList{"libother.so"} && fresh.kept("libother.so"));

        // Turning off removes the library from both places, so a failsafe
        // that restores it later finds nothing.
        Device both;
        both.write(both.plugins() + "/libnm.so", "menu");
        auto on = both.store();
        on.start();
        both.write((up ? both.root.path() : both.plugins()) + "/libnm.so.failsafe", "menu");
        CHECK(on.setEnabled("libnm.so", false, error));
        CHECK(!both.live("libnm.so") && on.failsafe("libnm.so").isEmpty() && both.kept("libnm.so"));
    }
}

// Opening the manager reads the folders again.
void rescan() {
    Device device;
    device.write(device.plugins() + "/libnm.so", "menu");
    device.write(device.plugins() + "/libprobe.so", "probe");
    auto store = device.store();
    store.start();
    device.write(device.plugins() + "/libnew.so", "new");
    QFile::remove(device.plugins() + "/libprobe.so");
    QFile::rename(device.plugins() + "/libnm.so", device.plugins() + "/libnm.so.failsafe");
    store.rescan();
    CHECK(find(store, "libnew.so") && find(store, "libnew.so")->enabled && !find(store, "libnew.so")->loaded);
    CHECK(device.kept("libnew.so"));
    CHECK(!find(store, "libprobe.so")->enabled && !find(store, "libprobe.so")->note.isEmpty());
    CHECK(find(store, "libnm.so")->parked && !find(store, "libnm.so")->enabled);
    // The failsafe puts it back.
    QFile::rename(device.plugins() + "/libnm.so.failsafe", device.plugins() + "/libnm.so");
    store.rescan();
    CHECK(find(store, "libnm.so")->enabled && !find(store, "libnm.so")->parked && find(store, "libnm.so")->note.isEmpty());
    CHECK(store.pendingChanges() == 2);
}

void setTime(const QString &path, long seconds) {
    struct utimbuf times = {seconds, seconds};
    if (utime(QFile::encodeName(path).constData(), &times)) std::abort();
}
qint64 timeOf(const QString &path) { return QFileInfo(path).lastModified().toMSecsSinceEpoch() / 1000; }

// The build date is the library's modification time, kept through every
// copy. The install date is when the loader first saw the bytes.
void dates() {
    Device device;
    device.write(device.plugins() + "/libnm.so", "menu");
    setTime(device.plugins() + "/libnm.so", 1700000000);
    {
        auto store = device.store();
        store.start();
        CHECK(find(store, "libnm.so")->built == 1700000000 && find(store, "libnm.so")->added == 0);
        CHECK(timeOf(device.storage() + "/mods/libnm.so") == 1700000000);
        QString error;
        CHECK(store.setEnabled("libnm.so", false, error) && store.setEnabled("libnm.so", true, error));
        CHECK(timeOf(device.plugins() + "/libnm.so") == 1700000000);
    }
    device.write(device.plugins() + "/libnew.so", "new");
    setTime(device.plugins() + "/libnew.so", 1710000000);
    auto store = device.store();
    store.start();
    CHECK(find(store, "libnm.so")->added == 0);
    CHECK(find(store, "libnew.so")->built == 1710000000 && find(store, "libnew.so")->added > 1710000000);
    device.write(device.plugins() + "/libnm.so", "menu 2");
    auto updated = device.store();
    updated.start();
    CHECK(find(updated, "libnm.so")->added > 1710000000);
    // A mod that is off keeps its dates from state.json.
    QString error;
    CHECK(updated.setEnabled("libnew.so", false, error));
    auto later = device.store();
    later.start();
    CHECK(find(later, "libnew.so")->built == 1710000000 && find(later, "libnew.so")->added > 1710000000);
}

void refusesChangedCopiesAndDamagedState() {
    Device device;
    device.write(device.plugins() + "/libnm.so", "menu");
    {
        auto store = device.store();
        store.start();
        QString error;
        CHECK(store.setEnabled("libnm.so", false, error));
        device.write(device.storage() + "/mods/libnm.so", "changed");
        CHECK(!store.setEnabled("libnm.so", true, error) && !error.isEmpty());
        CHECK(!device.live("libnm.so"));
        CHECK(QDir(device.plugins()).entryList(QDir::Files | QDir::Hidden).size() == 1);
    }
    device.write(device.storage() + "/state.json", "{");
    device.write(device.plugins() + "/libprobe.so", "probe");
    auto store = device.store();
    const auto report = store.start();
    CHECK(report.errors.size() == 1);
    CHECK(QFile::exists(device.storage() + "/state.json.bad"));
    CHECK(find(store, "libprobe.so") && find(store, "libprobe.so")->enabled);
    CHECK(!NickelModManager::validModFilename("../libx.so") && !NickelModManager::validModFilename(".libx.so.nmm-part"));
    CHECK(NickelModManager::modDisplayName("libnickelclock.so") == "NickelClock");
    CHECK(NickelModManager::modDisplayName("libsomething.so") == "something" && NickelModManager::modDisplayName("other.so") == "other");
}
}

int main() {
    firstStartAndToggles();
    removedOutsideAndUpdatedByInstaller();
    firmwareUpdate();
    firmwareUpdateWithoutPutBack();
    failsafe();
    rescan();
    dates();
    refusesChangedCopiesAndDamagedState();
    if (failures) { std::fprintf(stderr, "%d checks failed\n", failures); return 1; }
    std::puts("All store checks passed.");
    return 0;
}
