// Exercise the shutdown script without a device. FIRMWARE_ROOT selects real
// stock scripts; otherwise fixture-only hashes let these checks run in the SDK.
#include "mods/files.h"
#include "updates/hook.h"
#include "updates/package.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>

namespace {
int failures = 0;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition);            \
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
QString hash(const QString &path) {
    QString error;
    return NickelModManager::hashFile(path, error);
}

struct Fixture {
    QTemporaryDir directory;
    QByteArray package;
    QString path(const QString &relative) const {
        return directory.path() + relative;
    }
    QString state() const {
        return path("/mnt/onboard/.adds/nickel-mod-manager");
    }
    QString restore() const {
        return state() + "/restore";
    }
    QString kobo() const {
        return path("/mnt/onboard/.kobo");
    }
    QString manager() const {
        return path("/usr/local/Kobo/imageformats/libnickelmm.so");
    }

    Fixture() {
        QByteArray script = NickelModManager::UpdateHook::script();
        const QString firmware = QString::fromLocal8Bit(qgetenv("FIRMWARE_ROOT"));
        for (const auto &entry : NickelModManager::UpdateHook::contract()) {
            if (firmware.isEmpty()) {
                write(path(entry.first), "stock fixture " + entry.first.toUtf8());
                // Only this temporary script accepts synthetic stock scripts.
                for (const auto &accepted : entry.second) {
                    script.replace(accepted.toLatin1(), hash(path(entry.first)).toLatin1());
                }
            } else if (!QFile::copy(firmware + entry.first, preparePath(entry.first))) {
                std::fprintf(stderr, "Cannot copy stock script %s\n", qPrintable(entry.first));
                std::abort();
            }
        }
        write(path("/hook.sh"), script);
        write(manager(), "manager");
        write(kobo() + "/update.tar", "update");
        QDir().mkpath(restore());
        QString error;
        CHECK(NickelModManager::writePackage(
            restore() + "/Kobo.tgz", {{"imageformats/libnickelmm.so", manager()}}, error));
        package = read(restore() + "/Kobo.tgz");
        write(restore() + "/Kobo.tgz.sha256", hash(restore() + "/Kobo.tgz").toLatin1() + '\n');
        write(restore() + "/manager.sha256", hash(manager()).toLatin1() + '\n');
        write(path("/mounted"), "");
        write(path("/boot-target"), "7\n");
    }

    QString preparePath(const QString &relative) {
        QDir().mkpath(QFileInfo(path(relative)).absolutePath());
        return path(relative);
    }

    QByteArray run(const QString &action = "stop") {
        QProcess process;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) && defined(Q_OS_UNIX)
        process.setChildProcessModifier([] {});
#endif
        process.start("sh", {path("/hook.sh"), action, "--fixture=" + directory.path()});
        const bool finished = process.waitForStarted(10000) && process.waitForFinished(30000);
        if (!finished) {
            process.kill();
            process.waitForFinished();
        }
        CHECK(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0);
        if (!process.readAllStandardError().isEmpty()) {
            ++failures;
        }
        return read(state() + "/restore.log");
    }
    void queued(const QByteArray &log) {
        CHECK(log.contains("QUEUED"));
        CHECK(read(kobo() + "/Kobo.tgz") == package);
        CHECK(QDir(kobo()).entryList(QDir::Files) == (QStringList{"Kobo.tgz", "update.tar"}));
    }
    void skipped(const QByteArray &log, const QByteArray &reason) {
        CHECK(log.contains("SKIP " + reason));
        CHECK(!QFile::exists(kobo() + "/Kobo.tgz"));
    }
};

void queuesOnceAndAcceptsBootFormats() {
    Fixture fixture;
    fixture.queued(fixture.run());
    CHECK(fixture.run().contains("SKIP an existing Kobo.tgz is kept"));
    CHECK(read(fixture.kobo() + "/Kobo.tgz") == fixture.package);
    for (const auto &value : {"0x7", "07", "5", "recovery", ""}) {
        QFile::remove(fixture.kobo() + "/Kobo.tgz");
        QFile::remove(fixture.state() + "/restore.log");
        write(fixture.path("/boot-target"), value);
        const QByteArray log = fixture.run();
        if (QByteArray(value) == "0x7" || QByteArray(value) == "07") {
            fixture.queued(log);
        } else {
            CHECK(!QFile::exists(fixture.kobo() + "/Kobo.tgz"));
            CHECK(log.contains("SKIP"));
        }
    }
}

void normalRebootAndExistingPackage() {
    Fixture normal;
    QFile::remove(normal.kobo() + "/update.tar");
    CHECK(normal.run().isEmpty());
    CHECK(normal.run("start").isEmpty());
    CHECK(!QFile::exists(normal.kobo() + "/Kobo.tgz"));
    Fixture existing;
    write(existing.kobo() + "/Kobo.tgz", "someone else");
    CHECK(existing.run().contains("SKIP an existing Kobo.tgz is kept"));
    CHECK(read(existing.kobo() + "/Kobo.tgz") == "someone else");
}

void refusesUnsafeRestoration() {
    {
        Fixture fixture;
        QDir(fixture.restore()).removeRecursively();
        fixture.skipped(fixture.run(), "restore after updates is off");
    }
    {
        Fixture fixture;
        write(fixture.state() + "/restore-off", "off");
        fixture.skipped(fixture.run(), "restore after updates is off");
    }
    {
        Fixture fixture;
        write(fixture.path("/etc/init.d/ota"), "changed");
        fixture.skipped(fixture.run(), "unfamiliar stock scripts");
    }
    {
        Fixture fixture;
        QFile::remove(fixture.path("/mounted"));
        fixture.skipped(fixture.run(), "user storage is not mounted");
    }
    {
        Fixture fixture;
        write(fixture.manager(), "other");
        fixture.skipped(fixture.run(), "NickelModManager is missing, changed or parked");
    }
    {
        Fixture fixture;
        write(fixture.restore() + "/Kobo.tgz", "other");
        fixture.skipped(fixture.run(), "restore package changed");
    }
    {
        Fixture fixture;
        write(fixture.restore() + "/Kobo.tgz", "not gzip");
        write(fixture.restore() + "/Kobo.tgz.sha256",
              hash(fixture.restore() + "/Kobo.tgz").toLatin1() + '\n');
        fixture.skipped(fixture.run(), "restore package is damaged");
    }
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (QStandardPaths::findExecutable("sha256sum").isEmpty() ||
        QStandardPaths::findExecutable("gunzip").isEmpty()) {
        std::puts("Skipped: needs sha256sum and gunzip.");
        return 77;
    }
    queuesOnceAndAcceptsBootFormats();
    normalRebootAndExistingPackage();
    refusesUnsafeRestoration();
    return failures ? 1 : 0;
}
