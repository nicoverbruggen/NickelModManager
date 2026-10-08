// Exercise the real lifecycle on temporary system and user-storage trees.
#include "entrypoint/lifecycle.h"
#include "updates/hook.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {
using Lifecycle = NickelModManager::Lifecycle;
int failures = 0;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        std::abort();
    }
}

QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

struct System {
    QTemporaryDir directory;
    QString root() const { return directory.path() + "/root"; }
    QString storage() const { return directory.path() + "/onboard/.adds/nickelmodmanager"; }
    QString self() const { return root() + "/usr/local/Kobo/imageformats/libnickelmm.so"; }
    QString parked() const { return root() + "/usr/local/Kobo/libnickelmm.so.failsafe"; }
    QString receipt() const { return directory.path() + "/onboard/.adds/.nickelmodmanager.initialized"; }
    QString confirmed() const { return storage() + "/confirmed"; }
    Lifecycle lifecycle(const QString &firmware = "rev-1") const {
        return Lifecycle(self(), storage(), firmware, root());
    }
    System() {
        write(self(), "manager library");
        write(root() + "/usr/local/Kobo/imageformats/libnm.so", "installed mod");
        write(storage() + "/mods/libdisabled.so", "disabled mod backup");
        write(storage() + "/state.json", "saved state");
    }
};

void startAndConfirm() {
    System system;
    auto lifecycle = system.lifecycle();
    QString error;
    CHECK(lifecycle.begin(error) == Lifecycle::Result::Start);
    CHECK(!QFile::exists(system.self()) && read(system.parked()) == "manager library");
    CHECK(QFile::exists(system.storage() + "/uninstall") && QFile::exists(system.receipt()));
    CHECK(lifecycle.armed() && lifecycle.confirm(error) && !lifecycle.armed());
    CHECK(read(system.self()) == "manager library" && !QFile::exists(system.parked()));
    CHECK(QFile::exists(system.confirmed()));
    // A normal start preserves the user's marker content.
    write(system.storage() + "/uninstall", "custom marker");
    auto next = system.lifecycle();
    CHECK(next.begin(error) == Lifecycle::Result::Start && !next.armed());
    CHECK(read(system.storage() + "/uninstall") == "custom marker");
}

void uninstallAndReinstall() {
    for (int trigger = 0; trigger < 3; ++trigger) {
        System system;
        QString error;
        auto first = system.lifecycle();
        CHECK(first.begin(error) == Lifecycle::Result::Start && first.confirm(error));
        // Cleanup works even when stock update scripts are no longer known.
        write(system.root() + "/etc/init.d/nickelmodmanager", NickelModManager::UpdateHook::script());
        QDir().mkpath(system.root() + "/etc/rc6.d");
        CHECK(!symlink("/etc/init.d/nickelmodmanager", QFile::encodeName(system.root() + "/etc/rc6.d/S00nickelmodmanager").constData()));
        write(system.storage() + "/restore/Kobo.tgz", "restore package");
        if (trigger == 0) {
            CHECK(QFile::remove(system.storage() + "/uninstall"));
        } else if (trigger == 1) {
            write(system.storage() + "/uninstall-now", "");
        } else {
            CHECK(QDir(system.storage()).removeRecursively());
        }
        auto removal = system.lifecycle();
        CHECK(removal.begin(error) == Lifecycle::Result::Removed);
        CHECK(!QFile::exists(system.self()) && !QFile::exists(system.parked()));
        CHECK(!QFile::exists(system.receipt()) && !QFile::exists(system.storage() + "/uninstall-now"));
        CHECK(!QFile::exists(system.confirmed()));
        CHECK(!QFile::exists(system.root() + "/etc/init.d/nickelmodmanager"));
        CHECK(!QFileInfo(system.root() + "/etc/rc6.d/S00nickelmodmanager").isSymLink());
        CHECK(!QFile::exists(system.storage() + "/restore"));
        CHECK(read(system.root() + "/usr/local/Kobo/imageformats/libnm.so") == "installed mod");
        if (trigger != 2) {
            CHECK(read(system.storage() + "/state.json") == "saved state");
            CHECK(read(system.storage() + "/mods/libdisabled.so") == "disabled mod backup");
        }
        write(system.self(), "new manager library");
        auto reinstall = system.lifecycle();
        CHECK(reinstall.begin(error) == Lifecycle::Result::Start && reinstall.confirm(error));
        CHECK(QFile::exists(system.storage() + "/uninstall"));
        CHECK(read(system.self()) == "new manager library");
    }
}

void interruptedStartupAndReplacement() {
    System system;
    QString error;
    {
        auto unfinished = system.lifecycle();
        CHECK(unfinished.begin(error) == Lifecycle::Result::Start);
        // Destruction must not restore a library after an unfinished startup.
    }
    CHECK(!QFile::exists(system.self()) && QFile::exists(system.parked()));
    write(system.self(), "fixed library");
    auto retry = system.lifecycle();
    CHECK(retry.begin(error) == Lifecycle::Result::Start && retry.confirm(error));
    CHECK(read(system.self()) == "fixed library");

    // A firmware change arms the failsafe again for the same library.
    auto pending = system.lifecycle("rev-2");
    CHECK(pending.begin(error) == Lifecycle::Result::Start);
    write(system.self(), "new package during startup");
    CHECK(!pending.confirm(error) && !error.isEmpty());
    CHECK(read(system.self()) == "new package during startup");
    CHECK(read(system.parked()) == "fixed library");
}

// Only a new library or firmware arms the failsafe. Otherwise a crash in another
// mod would park the manager, and it could not start at the next boot.
void failsafeOnlyForNewStarts() {
    System system;
    QString error;
    auto first = system.lifecycle();
    CHECK(first.begin(error) == Lifecycle::Result::Start && first.armed() && first.confirm(error));

    // An unfinished start that was not armed leaves the library in place.
    {
        auto same = system.lifecycle();
        CHECK(same.begin(error) == Lifecycle::Result::Start && !same.armed());
        CHECK(!same.confirm(error) && !error.isEmpty());
    }
    CHECK(read(system.self()) == "manager library" && !QFile::exists(system.parked()));

    auto firmware = system.lifecycle("rev-2");
    CHECK(firmware.begin(error) == Lifecycle::Result::Start && firmware.armed());
    CHECK(firmware.confirm(error));
    auto confirmedFirmware = system.lifecycle("rev-2");
    CHECK(confirmedFirmware.begin(error) == Lifecycle::Result::Start && !confirmedFirmware.armed());

    write(system.self(), "updated manager library");
    auto library = system.lifecycle("rev-2");
    CHECK(library.begin(error) == Lifecycle::Result::Start && library.armed());
    CHECK(library.confirm(error));

    // A damaged record or an unknown firmware fails towards protection.
    write(system.confirmed(), "damaged");
    auto damaged = system.lifecycle("rev-2");
    CHECK(damaged.begin(error) == Lifecycle::Result::Start && damaged.armed());
    CHECK(damaged.confirm(error));
    auto unknown = system.lifecycle(QString());
    CHECK(unknown.begin(error) == Lifecycle::Result::Start && unknown.armed());
    CHECK(unknown.confirm(error));
}

void failedSetupAndRemoval() {
    System system;
    QString error;
    // A cleanup failure leaves the uninstall request available for the next
    // boot, and the opt-out prevents the leftover package from being queued.
    write(system.storage() + "/uninstall-now", "");
    QDir().mkpath(system.storage() + "/restore/Kobo.tgz");
    auto removal = system.lifecycle();
    CHECK(removal.begin(error) == Lifecycle::Result::Error && !error.isEmpty());
    CHECK(QFile::exists(system.self()) && QFile::exists(system.storage() + "/uninstall-now"));
    CHECK(QFile::exists(system.storage() + "/restore-off"));

    System blocked;
    QDir().mkpath(blocked.parked());
    auto startup = blocked.lifecycle();
    CHECK(startup.begin(error) == Lifecycle::Result::Error);
    CHECK(read(blocked.self()) == "manager library");

    System receipt;
    QDir().mkpath(receipt.receipt());
    auto malformed = receipt.lifecycle();
    CHECK(malformed.begin(error) == Lifecycle::Result::Error);
    CHECK(read(receipt.self()) == "manager library");
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    startAndConfirm();
    uninstallAndReinstall();
    interruptedStartupAndReplacement();
    failsafeOnlyForNewStarts();
    failedSetupAndRemoval();
    if (failures) {
        return 1;
    }
    std::puts("All lifecycle checks passed.");
    return 0;
}
