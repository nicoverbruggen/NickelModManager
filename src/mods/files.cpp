#include "files.h"
#include "compat.h"
#include <QFile>
#include <QFileInfo>
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#if defined(__linux__)
#include <link.h>
#endif
#include <sys/stat.h>
#include <unistd.h>

namespace NickelModManager {
constexpr qint64 limit = 32 * 1024 * 1024;
QString systemError() {
    return QString::fromLocal8Bit(std::strerror(errno));
}
QByteArray nativePath(const QString &path) {
    return QFile::encodeName(path);
}
bool isRegularFile(const QString &path) {
    // lstat checks the entry itself, so a symlink cannot stand in for a mod.
    struct stat st;
    return !lstat(nativePath(path).constData(), &st) && S_ISREG(st.st_mode);
}
// A rename or unlink is only durable once its folder is synced.
void syncDirectory(const QString &path) {
    const int fd = open(nativePath(path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return;
    }
    fsync(fd);
    close(fd);
}
// Whether this process has loaded the library from this path. Qt may load
// plugins before or after NickelModManager. The dynamic linker keeps the path it
// opened, so this holds after NickelHook's failsafe renamed the file.
bool libraryIsLoaded(const QString &path) {
#if defined(__linux__)
    struct Search {
        QByteArray path;
        bool found;
    } search{nativePath(path), false};
    dl_iterate_phdr(
        [](struct dl_phdr_info *info, size_t, void *data) {
            auto *search = static_cast<Search *>(data);
            if (info->dlpi_name && search->path == info->dlpi_name) {
                search->found = true;
                return 1;
            }
            return 0;
        },
        &search);
    return search.found;
#else
    void *handle = dlopen(nativePath(path).constData(), RTLD_NOW | RTLD_NOLOAD);
    if (handle) {
        dlclose(handle);
    }
    return handle != nullptr;
#endif
}

namespace {
// Validate the opened descriptor, not a prior filename lookup. Both hashing
// and copying refuse symlinks and files larger than the library size limit.
int openRegularFile(const QString &path, struct stat &metadata, QString &error) {
    const int descriptor = open(nativePath(path).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        error = path + ": " + systemError();
        return -1;
    }
    if (fstat(descriptor, &metadata) || !S_ISREG(metadata.st_mode) || metadata.st_size > limit) {
        close(descriptor);
        error = path + ": not a regular file of at most 32 MiB";
        return -1;
    }
    return descriptor;
}

// POSIX writes may be partial or interrupted. Finish this buffer before
// reading the next one so the digest and output describe the same bytes.
bool writeAll(
    int descriptor, const char *bytes, ssize_t size, const QString &path, QString &error) {
    for (ssize_t done = 0; done < size;) {
        const ssize_t step = write(descriptor, bytes + done, size - done);
        if (step < 0 && errno == EINTR) {
            continue;
        }
        if (step <= 0) {
            error = path + ": " + systemError();
            return false;
        }
        done += step;
    }
    return true;
}

// Check the running size too, since the source can grow after fstat. Stop
// before publishing if reading or writing any part of the library fails.
bool copyContents(int input,
                  int output,
                  const QString &source,
                  const QString &part,
                  QCryptographicHash &digest,
                  QString &error) {
    char buffer[65536];
    qint64 total = 0;
    ssize_t count;
    while ((count = read(input, buffer, sizeof buffer)) > 0 && (total += count) <= limit) {
        addBytes(digest, buffer, int(count));
        if (!writeAll(output, buffer, count, part, error)) {
            return false;
        }
    }
    if (count != 0) {
        error = source + ": could not be read completely";
        return false;
    }
    return true;
}

// Kobo preserves archive times, and the UI uses mtime as the build date.
// Preserve those times and flush the bytes before publishing the copy.
bool flushCopy(int output, const struct stat &metadata, const QString &part, QString &error) {
#if defined(__APPLE__)
    const struct timespec times[2] = {metadata.st_atimespec, metadata.st_mtimespec};
#else
    const struct timespec times[2] = {metadata.st_atim, metadata.st_mtim};
#endif
    if (futimens(output, times) || fsync(output)) {
        error = part + ": " + systemError();
        return false;
    }
    return true;
}

// Check the bytes before the rename. If an installer replaced the source,
// leave the old destination intact rather than publish an unexpected library.
bool publishCopy(const QString &source,
                 const QString &part,
                 const QString &target,
                 const QString &actualHash,
                 const QString &expectedHash,
                 QString &error) {
    if (actualHash != expectedHash) {
        error = source + " changed: it no longer matches the recorded copy";
        return false;
    }
    if (rename(nativePath(part).constData(), nativePath(target).constData()) != 0) {
        error = target + ": " + systemError();
        return false;
    }
    return true;
}
} // namespace

QString hashFile(const QString &path, QString &error) {
    // Check the opened file rather than trusting a prior path lookup.
    // The second size limit also catches a file growing during the read.
    struct stat metadata;
    const int fd = openRegularFile(path, metadata, error);
    if (fd < 0) {
        return {};
    }
    QCryptographicHash digest(QCryptographicHash::Sha256);
    char buffer[65536];
    qint64 total = 0;
    ssize_t count;
    while ((count = read(fd, buffer, sizeof buffer)) > 0 && (total += count) <= limit) {
        addBytes(digest, buffer, int(count));
    }
    close(fd);
    if (count != 0) {
        error = path + (count < 0 ? ": " + systemError() : QString(": grew beyond 32 MiB"));
        return {};
    }
    return QString::fromLatin1(digest.result().toHex());
}

bool copyFile(const QString &source,
              const QString &target,
              const QString &expected,
              QString &error,
              const QString &scratch) {
    struct stat metadata;
    const int input = openRegularFile(source, metadata, error);
    if (input < 0) {
        return false;
    }

    // Qt6 loads every file in imageformats. A temporary copy must stay outside
    // that directory until complete; scratch must be on the same filesystem.
    const QString folder = QFileInfo(target).absolutePath();
    const QString part =
        (scratch.isEmpty() ? folder : scratch) + "/." + QFileInfo(target).fileName() + ".nmm-part";
    unlink(nativePath(part).constData());
    const int output = open(
        nativePath(part).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0644);
    if (output < 0) {
        close(input);
        error = part + ": " + systemError();
        return false;
    }

    QCryptographicHash digest(QCryptographicHash::Sha256);
    bool complete = copyContents(input, output, source, part, digest, error);
    if (complete) {
        complete = flushCopy(output, metadata, part, error);
    }
    close(input);
    if (close(output) && complete) {
        error = part + ": " + systemError();
        complete = false;
    }
    if (complete) {
        complete = publishCopy(
            source, part, target, QString::fromLatin1(digest.result().toHex()), expected, error);
    }
    if (!complete) {
        unlink(nativePath(part).constData());
        return false;
    }
    syncDirectory(folder);
    return true;
}
} // namespace NickelModManager
