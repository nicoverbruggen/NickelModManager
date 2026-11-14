#include "mods/metadata.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "%d: %s\n", __LINE__, #condition); ++failures; } } while (0)
void write(const QString &path, const QByteArray &bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        std::abort();
    }
}
} // namespace

int main() {
    QTemporaryDir root;
    QDir().mkpath(root.path() + "/nickel-home");
    const QString path = root.path() + "/nickel-home/mod.manifest";
    NickelModManager::Mod mod("libnickelhome.so", "NickelHome");
    mod.hash = QString(64, 'a');
    const QVector<NickelModManager::Mod> mods{mod};
    const QByteArray manifest = "[mod]\nschema=1\nlibrary=libnickelhome.so\nversion=0.7-test\nsha256=" + mod.hash.toLatin1() + "\n";
    const auto versions = [&] { return NickelModManager::modVersions(root.path(), mods); };
    CHECK(versions().isEmpty());
    write(path, manifest);
    CHECK(versions().value(mod.file) == "0.7-test");
    write(path, manifest + "[future-settings]\ntype=bool\n");
    CHECK(versions().value(mod.file) == "0.7-test");
    for (const QByteArray &invalid : {
             QByteArray(manifest).replace("schema=1", "schema=2"),
             QByteArray(manifest).replace("version=0.7-test", "version="),
             QByteArray(manifest).replace("libnickelhome.so", "../libnickelhome.so"),
             QByteArray(manifest).replace(mod.hash.toLatin1(), QByteArray(64, 'b')),
             manifest + "version=duplicate\n",
             QByteArray(16385, 'x')}) {
        write(path, invalid);
        CHECK(versions().isEmpty());
    }
    write(path, manifest);
    CHECK(NickelModManager::modVersions(root.path(), {}).isEmpty());
    QDir().mkpath(root.path() + "/duplicate");
    write(root.path() + "/duplicate/mod.manifest", manifest);
    CHECK(versions().isEmpty());
    CHECK(QFile::remove(root.path() + "/duplicate/mod.manifest"));
    // Removing the conflicting declaration makes the version available again.
    CHECK(versions().value(mod.file) == "0.7-test");
    CHECK(QFile::remove(path));
    CHECK(QFile::link(root.path() + "/duplicate", root.path() + "/linked-folder"));
    write(root.path() + "/duplicate/mod.manifest", manifest);
    CHECK(versions().value(mod.file) == "0.7-test"); // The symlink is not a second declaration.
    return failures ? 1 : 0;
}
