#include "lifecycle.h"
#include "mods/files.h"
#include "updates/hook.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace NickelModManager {
namespace {
// lstat distinguishes a missing path from an unreadable path or dangling link.
bool present(const QString &path, bool &found, QString &error) {
    struct stat metadata;
    if (!lstat(nativePath(path).constData(), &metadata)) {
        found = true;
        return true;
    }
    found = false;
    if (errno == ENOENT) {
        return true;
    }
    error = path + ": " + systemError();
    return false;
}

bool discard(const QString &path, QString &error) {
    if (unlink(nativePath(path).constData()) && errno != ENOENT) {
        error = path + ": " + systemError();
        return false;
    }
    syncDirectory(QFileInfo(path).absolutePath());
    return true;
}

bool saveMarker(const QString &path, const QByteArray &bytes, QString &error) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = path + ": " + file.errorString();
        return false;
    }
    syncDirectory(QFileInfo(path).absolutePath());
    return true;
}
} // namespace

Lifecycle::Lifecycle(QString self, QString storage, QString firmware, QString root)
    : self_(std::move(self)), storage_(std::move(storage)), firmware_(std::move(firmware)),
      root_(std::move(root)) {
    const QFileInfo library(self_);
    parked_ = QFileInfo(library.absolutePath()).absolutePath() + "/" + library.fileName() +
              ".failsafe";
    receipt_ = QFileInfo(storage_).absolutePath() + "/.nickelmodmanager.initialized";
    confirmed_ = storage_ + "/confirmed";
}

Lifecycle::Result Lifecycle::begin(QString &error) {
    bool requested, initialized, keep;
    if (!present(storage_ + "/uninstall-now", requested, error) ||
        !present(receipt_, initialized, error) ||
        !present(storage_ + "/uninstall", keep, error)) {
        return Result::Error;
    }
    if ((initialized && !isRegularFile(receipt_)) ||
        (keep && !isRegularFile(storage_ + "/uninstall"))) {
        error = "Installation receipt and uninstall marker must be regular files.";
        return Result::Error;
    }
    if (requested || (initialized && !keep)) {
        return remove(error) ? Result::Removed : Result::Error;
    }
    if (!confirmedBefore() && !arm(error)) {
        return Result::Error;
    }
    if (!initializeMarkers(error)) {
        return Result::Error;
    }
    return Result::Start;
}

bool Lifecycle::initializeMarkers(QString &error) {
    bool initialized;
    if (!present(receipt_, initialized, error)) {
        return false;
    }
    if (initialized) {
        return true;
    }
    if (!QDir().mkpath(storage_)) {
        error = "Could not create " + storage_;
        return false;
    }
    bool keep;
    if (!present(storage_ + "/uninstall", keep, error)) {
        return false;
    }
    if (!keep && !saveMarker(storage_ + "/uninstall",
                             "Delete this file and restart to remove NickelModManager.\n", error)) {
        return false;
    }
    // The receipt lives outside the folder. Deleting the folder must request
    // removal too, rather than silently creating its keep-installed file again.
    return saveMarker(receipt_, "1\n", error);
}

bool Lifecycle::remove(QString &error) {
    // Stop restoration first. If later removal fails, the manager cannot queue
    // its own reinstall during a firmware update. Keep other mods and backups.
    if (!QDir().mkpath(storage_)) {
        error = "Could not create " + storage_;
        return false;
    }
    UpdateHook hook(root_, storage_, self_);
    if (!hook.setEnabled(false, {}, error)) {
        return false;
    }
    if (!discard(parked_, error) || !discard(self_, error)) {
        return false;
    }
    // Only actual removal clears the receipt. Reinstall can seed the marker
    // again without discarding saved state or recreating it on ordinary boots.
    // A reinstall arms the failsafe again, even for the same library.
    return discard(receipt_, error) && discard(confirmed_, error) &&
           discard(storage_ + "/uninstall-now", error) && discard(storage_ + "/uninstall", error);
}

bool Lifecycle::arm(QString &error) {
    struct stat metadata;
    if (lstat(nativePath(self_).constData(), &metadata) || !S_ISREG(metadata.st_mode)) {
        error = self_ + ": manager library is not a regular file";
        return false;
    }
    bool occupied;
    if (!present(parked_, occupied, error)) {
        return false;
    }
    if (occupied && !isRegularFile(parked_)) {
        error = parked_ + ": failsafe path is not a regular file";
        return false;
    }
    if (rename(nativePath(self_).constData(), nativePath(parked_).constData())) {
        error = self_ + ": " + systemError();
        return false;
    }
    device_ = metadata.st_dev;
    inode_ = metadata.st_ino;
    armed_ = true;
    syncDirectory(QFileInfo(self_).absolutePath());
    syncDirectory(QFileInfo(parked_).absolutePath());
    return true;
}

bool Lifecycle::confirm(QString &error) {
    if (!armed_) {
        error = "Startup failsafe is not armed.";
        return false;
    }
    struct stat metadata;
    if (lstat(nativePath(parked_).constData(), &metadata) || !S_ISREG(metadata.st_mode) ||
        quint64(metadata.st_dev) != device_ || quint64(metadata.st_ino) != inode_) {
        error = parked_ + ": parked manager library changed";
        return false;
    }
    // link publishes without overwriting a package installed while startup was
    // in progress. Both paths are on the system partition, which supports links.
    if (link(nativePath(parked_).constData(), nativePath(self_).constData())) {
        error = self_ + ": " + systemError();
        return false;
    }
    syncDirectory(QFileInfo(self_).absolutePath());
    if (!discard(parked_, error)) {
        return false;
    }
    armed_ = false;
    if (hash_.isEmpty()) {
        return true;
    }
    return saveMarker(confirmed_, record(), error);
}

// The record holds the library hash and firmware identity of the last
// confirmed start. A missing, unreadable or different record, an unreadable
// library or an unknown firmware all arm the failsafe, because a mistake here
// must fail towards protection.
bool Lifecycle::confirmedBefore() {
    QString ignored;
    hash_ = hashFile(self_, ignored);
    if (hash_.isEmpty() || firmware_.isEmpty()) {
        return false;
    }
    QFile file(confirmed_);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    return file.read(4096) == record();
}

QByteArray Lifecycle::record() const {
    return "library " + hash_.toLatin1() + "\nfirmware " + firmware_.toUtf8() + "\n";
}
} // namespace NickelModManager
