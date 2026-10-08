#include "hook.h"
#include "package.h"
#include "compat.h"
#include "mods/files.h"
#include "restore_hook.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace NickelModManager {
// --- file and archive helpers
namespace {
const char *const initName = "/etc/init.d/nickelmodmanager";
// Early-boot reboots skip K entries; normal reboots can run both. The script
// accepts either route and refuses to replace a package already queued.
const char *const linkNames[] = {"/etc/rc6.d/K00nickelmodmanager",
                                 "/etc/rc6.d/S00nickelmodmanager"};
// The first line after the shebang names the script as ours, also when an
// older NickelModManager wrote a different version of it.
const char *const marker = "# NickelModManager:";

QString linkTarget(const QString &path) {
    char buffer[256];
    const ssize_t size = readlink(nativePath(path).constData(), buffer, sizeof buffer - 1);
    return size > 0 ? QString::fromLocal8Bit(buffer, int(size)) : QString();
}
bool exists(const QString &path) {
    // A dangling symlink still occupies a path and must not be overwritten.
    struct stat st;
    return !lstat(nativePath(path).constData(), &st);
}
bool ours(const QString &path) {
    QFile file(path);
    return isRegularFile(path) && file.open(QIODevice::ReadOnly) && file.read(512).contains(marker);
}
bool save(const QString &path, const QByteArray &bytes, QString &error) {
    // Refuse a direct write if a temporary file cannot be created. A partial
    // init script or checksum must not replace the last complete file.
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = path + ": " + file.errorString();
        return false;
    }
    return true;
}
} // namespace

// --- update contract

UpdateHook::UpdateHook(QString root,
                       QString storage,
                       QString self,
                       QVector<QPair<QString, QStringList>> contract)
    : root_(std::move(root)), storage_(std::move(storage)), self_(std::move(self)),
      contract_(std::move(contract)) {
}

QByteArray UpdateHook::script() {
    return QByteArray(restoreHook);
}

QVector<QPair<QString, QStringList>> UpdateHook::contract() {
    // These are the stock scripts accepted for restoration.
    // Exact hashes are a conservative guard; even a comment change rejects
    // the scripts. The shutdown script repeats the check before queuing.
    return {
        {"/etc/init.d/ota", {"60ce6320db4a065be8f37e5c3fa3bccb5a54948c261dd980fde7aba0611a8448"}},
        {"/etc/init.d/rc", {"dc914f7bac4ec44ed39fc95b34d6250d6a8573633b5a7e16f74f0ff533d34312"}},
        {"/etc/init.d/mount-userdata",
         {"b2ad669790d17cbd8aa244b321e43b96dac9f33a91d294f8c65ed050cc626f5f"}},
        {"/usr/libexec/platform/utils.sh",
         {"2f3df68977747b2e2f6a4f00097db1a84a172d47926fb59bf9ebfcd1f13fe8ad",
          "91d260ec9fa0b6453f0ed8511e90e85a8cfae94040e00f416e206b6842262bf1"}},
    };
}

bool UpdateHook::supported() const {
    QString error;
    for (const auto &entry : contract_) {
        if (!entry.second.contains(hashFile(root_ + entry.first, error))) {
            return false;
        }
    }
    return true;
}

bool UpdateHook::enabled() const {
    return !exists(storage_ + "/restore-off");
}

bool UpdateHook::installed() const {
    if (!ours(root_ + initName)) {
        return false;
    }
    for (const char *link : linkNames) {
        if (linkTarget(root_ + link) != initName) {
            return false;
        }
    }
    QFile file(root_ + initName);
    return file.open(QIODevice::ReadOnly) && file.readAll() == script();
}

// --- shutdown entries

// Validate every occupied path before writing any shutdown entry. This avoids
// replacing a script or link owned by another installer. I/O failures later
// in installation can still leave some of our entries in place.
bool UpdateHook::install(QString &error) {
    if (!checkInstallPaths(error)) {
        return false;
    }
    if (!writeInitScript(error)) {
        return false;
    }
    return writeShutdownLinks(error);
}

bool UpdateHook::checkInstallPaths(QString &error) const {
    const QString init = root_ + initName;
    if (exists(init) && !ours(init)) {
        error = init + " exists and is not NickelModManager's.";
        return false;
    }
    for (const char *link : linkNames) {
        if (exists(root_ + link) && linkTarget(root_ + link) != initName) {
            error = root_ + link + " exists and is not NickelModManager's.";
            return false;
        }
    }
    return true;
}

// Reuse identical script bytes, but restore executable permissions even if
// the file did not need rewriting. QSaveFile keeps incomplete writes hidden.
bool UpdateHook::writeInitScript(QString &error) {
    const QString init = root_ + initName;
    QFile current(init);
    if (!(current.open(QIODevice::ReadOnly) && current.readAll() == script())) {
        current.close();
        if (!save(init, script(), error)) {
            return false;
        }
    }
    if (!QFile::setPermissions(init,
                               QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                   QFileDevice::ExeOwner | QFileDevice::ReadGroup |
                                   QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                   QFileDevice::ExeOther)) {
        error = init + ": could not make it executable";
        return false;
    }
    return true;
}

bool UpdateHook::writeShutdownLinks(QString &error) {
    for (const char *link : linkNames) {
        if (!exists(root_ + link) && symlink(initName, nativePath(root_ + link).constData())) {
            error = root_ + link + ": " + QString::fromLocal8Bit(std::strerror(errno));
            return false;
        }
    }
    return true;
}

bool UpdateHook::uninstall(QString &error) {
    // An older manager may have installed a different script version. Its
    // marker still permits removal, but foreign files never qualify.
    for (const char *link : linkNames) {
        if (linkTarget(root_ + link) == initName) {
            if (unlink(nativePath(root_ + link).constData()) && errno != ENOENT) {
                error = root_ + link + ": " + systemError();
                return false;
            }
        }
    }
    if (ours(root_ + initName)) {
        if (unlink(nativePath(root_ + initName).constData()) && errno != ENOENT) {
            error = root_ + initName + ": " + systemError();
            return false;
        }
    }
    syncDirectory(root_ + "/etc/rc6.d");
    syncDirectory(root_ + "/etc/init.d");
    return true;
}

// --- restore package and preference

bool UpdateHook::setEnabled(bool on, const QStringList &libraries, QString &error) {
    const QString off = storage_ + "/restore-off", restore = storage_ + "/restore";
    if (!on) {
        // Save the opt-out first. If cleanup is incomplete, the shutdown
        // script will still see it and refuse to queue the restore package.
        if (!exists(off) && !save(off, "Restore after firmware updates is off.\n", error)) {
            return false;
        }
        if (!uninstall(error)) {
            return false;
        }
        for (const char *name : {"Kobo.tgz", "Kobo.tgz.sha256", "manager.sha256", "manifest"}) {
            const QString path = restore + "/" + name;
            if (unlink(nativePath(path).constData()) && errno != ENOENT) {
                error = path + ": " + systemError();
                return false;
            }
        }
        if (rmdir(nativePath(restore).constData()) && errno != ENOENT) {
            error = restore + ": " + systemError();
            return false;
        }
        syncDirectory(storage_);
        return true;
    }
    if (exists(off) && unlink(nativePath(off).constData())) {
        error = off + ": " + QString::fromLocal8Bit(std::strerror(errno));
        return false;
    }
    return refresh(libraries, error);
}

// Prepare a complete package before installing the shutdown hook. Unknown
// stock scripts remove our hook and return an error; the saved preference stays.
bool UpdateHook::refresh(const QStringList &libraries, QString &error) {
    if (!enabled()) {
        return true;
    }
    if (!supported()) {
        if (!uninstall(error)) {
            return false;
        }
        error = "This firmware's update scripts are not the ones NickelModManager knows.";
        return false;
    }

    const QString restore = storage_ + "/restore";
    if (!QDir().mkpath(restore)) {
        error = "Could not create " + restore;
        return false;
    }
    const PackageEntries entries = packageEntries(libraries);
    const QByteArray manifest = packageManifest(entries);
    if (!packageIsCurrent(manifest, error)) {
        if (!writeRestorePackage(entries, manifest, error)) {
            return false;
        }
    }
    return install(error);
}

UpdateHook::PackageEntries UpdateHook::packageEntries(const QStringList &libraries) const {
    QVector<QPair<QString, QString>> entries{
        {"imageformats/" + QFileInfo(self_).fileName(), self_}};
    for (const auto &library : libraries) {
        entries.append({"imageformats/" + QFileInfo(library).fileName(), library});
    }
    return entries;
}

// Compare input names, sizes and modification times to avoid rewriting a
// package whose inputs are unchanged. packageIsCurrent also checks its hash.
QByteArray UpdateHook::packageManifest(const PackageEntries &entries) const {
    QByteArray manifest;
    for (const auto &entry : entries) {
        const QFileInfo file(entry.second);
        manifest += entry.first.toUtf8() + ' ' + QByteArray::number(file.size()) + ' ' +
                    QByteArray::number(file.lastModified().toMSecsSinceEpoch()) + '\n';
    }
    return manifest;
}

bool UpdateHook::packageIsCurrent(const QByteArray &manifest, QString &error) const {
    const QString restore = storage_ + "/restore";
    QFile last(restore + "/manifest"), recorded(restore + "/Kobo.tgz.sha256");
    return last.open(QIODevice::ReadOnly) && last.readAll() == manifest &&
           recorded.open(QIODevice::ReadOnly) &&
           hashFile(restore + "/Kobo.tgz", error) ==
               QString::fromLatin1(recorded.readAll().trimmed());
}

// Write the package checksum last. The shutdown script must not accept a new
// package until both its manager checksum and manifest have been saved.
bool UpdateHook::writeRestorePackage(const PackageEntries &entries,
                                     const QByteArray &manifest,
                                     QString &error) {
    const QString restore = storage_ + "/restore";
    const QString manager = hashFile(self_, error);
    if (manager.isEmpty() || !writePackage(restore + "/Kobo.tgz", entries, error)) {
        return false;
    }
    const QString package = hashFile(restore + "/Kobo.tgz", error);
    if (package.isEmpty() || !save(restore + "/manager.sha256", manager.toLatin1() + "\n", error) ||
        !save(restore + "/manifest", manifest, error) ||
        !save(restore + "/Kobo.tgz.sha256", package.toLatin1() + "\n", error)) {
        return false;
    }
    return true;
}

} // namespace NickelModManager
