// Check persistence independently of library discovery or filesystem changes.
#include "mods/persistence.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
const QString managerFilename = "libnickelmm.so";

void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        std::abort();
    }
}

QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void roundTripKeepsDurableState() {
    QTemporaryDir directory;
    const QString path = directory.path() + "/state.json";
    NickelModManager::SavedModState state;
    state.firmware = "rev-2";
    NickelModManager::Mod mod("libnm.so", "NickelMenu");
    mod.hash = "recorded hash";
    mod.enabled = true;
    mod.loaded = true;
    mod.parked = true;
    mod.note = QString::fromUtf8("A note with \xc3\xa9, quotes \" and a newline\n");
    mod.built = 1700000000;
    mod.added = 1710000000;
    state.mods.append(mod);

    QString error;
    CHECK(NickelModManager::saveModState(path, state, error));
    const auto loaded = NickelModManager::loadModState(path, managerFilename);
    CHECK(loaded.error.isEmpty() && !loaded.fresh);
    CHECK(loaded.state.firmware == state.firmware);
    CHECK(loaded.state.mods.size() == 1);
    if (loaded.state.mods.size() != 1) {
        return;
    }
    const auto &saved = loaded.state.mods.first();
    CHECK(saved.file == mod.file && saved.name == mod.name && saved.hash == mod.hash);
    CHECK(saved.enabled && saved.note == mod.note);
    CHECK(saved.built == mod.built && saved.added == mod.added);
    CHECK(!saved.loaded && !saved.parked);
}

void readingDoesNotDiscoverOrChangeLibraries() {
    QTemporaryDir directory;
    const QString path = directory.path() + "/state.json";
    const QString library = directory.path() + "/libnm.so";
    write(library, "installed library");
    const auto missing = NickelModManager::loadModState(path, managerFilename);
    CHECK(missing.error.isEmpty() && missing.fresh && missing.state.mods.isEmpty());
    CHECK(!QFile::exists(path) && read(library) == "installed library");

    write(path, "{");
    const auto damaged = NickelModManager::loadModState(path, managerFilename);
    CHECK(!damaged.error.isEmpty() && damaged.fresh && damaged.state.mods.isEmpty());
    CHECK(!QFile::exists(path) && read(path + ".bad") == "{");
    CHECK(read(library) == "installed library");
}

void untrustedEntriesCannotBecomePaths() {
    QTemporaryDir directory;
    const QString path = directory.path() + "/state.json";
    write(path, R"({"schema":1,"mods":[
        {"file":"../libbad.so"},{"file":"libkimg.so"},
        {"file":"libnickelmm.so"},
        {"file":"libnm.so","enabled":true},
        {"file":"libnm.so","enabled":false}]})");
    const auto loaded = NickelModManager::loadModState(path, managerFilename);
    CHECK(loaded.error.isEmpty() && !loaded.fresh);
    CHECK(loaded.state.mods.size() == 1);
    if (loaded.state.mods.size() == 1) {
        CHECK(loaded.state.mods.first().file == "libnm.so" && loaded.state.mods.first().enabled);
    }
}

void failedWriteLeavesExistingDestination() {
    QTemporaryDir directory;
    const QString path = directory.path() + "/state.json";
    QDir().mkdir(path);
    write(path + "/keep", "existing contents");
    QString error;
    CHECK(!NickelModManager::saveModState(path, {}, error) && !error.isEmpty());
    CHECK(read(path + "/keep") == "existing contents");
}
} // namespace

int main() {
    roundTripKeepsDurableState();
    readingDoesNotDiscoverOrChangeLibraries();
    untrustedEntriesCannotBecomePaths();
    failedWriteLeavesExistingDestination();
    if (failures) {
        return 1;
    }
    std::puts("All persistence checks passed.");
    return 0;
}
