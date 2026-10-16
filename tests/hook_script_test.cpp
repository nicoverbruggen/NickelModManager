// Exercise the shutdown script without a device. FIRMWARE_ROOT selects real
// stock scripts; otherwise fixture-only hashes let these checks run in the SDK.
#include "mods/files.h"
#include "updates/hook.h"
#include "updates/package.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <QTemporaryDir>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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
    qint64 elapsed = 0;
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
        // Exercise the production supervisor with a shorter fixture deadline.
        CHECK(script.contains("deadline_seconds=30"));
        script.replace("deadline_seconds=30", "deadline_seconds=2");
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

    QByteArray run(const QString &action = "stop", bool blockedDiagnostic = false,
                   int interruption = 0) {
        QProcess process;
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert("PATH", path("/bin") + ":" + environment.value("PATH"));
        environment.insert("NMM_HOOK_TEST_ROOT", directory.path());
        environment.insert("NMM_HOOK_REAL_SLEEP", QStandardPaths::findExecutable("sleep"));
        environment.insert("NMM_HOOK_REAL_HASH", QStandardPaths::findExecutable("sha256sum"));
        environment.insert("NMM_HOOK_REAL_BASH", QStandardPaths::findExecutable("bash"));
        environment.insert("NMM_HOOK_REAL_COPY", QStandardPaths::findExecutable("cp"));
        process.setProcessEnvironment(environment);
        int diagnosticPipe = -1;
        if (blockedDiagnostic) {
            const QString pipe = path("/diagnostic-pipe");
            CHECK(::mkfifo(QFile::encodeName(pipe).constData(), 0600) == 0);
            diagnosticPipe = ::open(QFile::encodeName(pipe).constData(), O_RDWR | O_NONBLOCK);
            CHECK(diagnosticPipe >= 0);
            const char bytes[4096] = {};
            while (::write(diagnosticPipe, bytes, sizeof bytes) > 0) {}
            CHECK(errno == EAGAIN);
            process.setStandardErrorFile(pipe);
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) && defined(Q_OS_UNIX)
        process.setChildProcessModifier([] {});
#endif
        QElapsedTimer clock;
        clock.start();
        const QByteArray update = read(kobo() + "/update.tar");
        process.start("bash", {path("/hook.sh"), action, "--fixture=" + directory.path()});
        const bool started = process.waitForStarted(10000);
        if (started && interruption) {
            while (!QFile::exists(path("/stalled-child")) && clock.elapsed() < 1000) {
                process.waitForReadyRead(10);
            }
            CHECK(QFile::exists(path("/stalled-child")));
#if QT_VERSION >= QT_VERSION_CHECK(5, 3, 0)
            const qint64 pid = process.processId();
#else
            const qint64 pid = process.pid();
#endif
            CHECK(pid > 1 && ::kill(pid_t(pid), interruption) == 0);
        }
        const bool finished = started &&
                              process.waitForFinished(blockedDiagnostic ? 5000 : 30000);
        elapsed = clock.elapsed();
        if (diagnosticPipe >= 0) {
            // Release a stuck diagnostic so a failed test can still let the
            // production cleanup run before the fixture disappears.
            char bytes[4096];
            while (::read(diagnosticPipe, bytes, sizeof bytes) > 0) {}
            if (!finished) {
                process.waitForFinished(2000);
            }
            ::close(diagnosticPipe);
        }
        if (!finished) {
            process.kill();
            process.waitForFinished();
        }
        CHECK(finished && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0);
        const QByteArray diagnostic = process.readAllStandardError();
        CHECK(diagnostic.isEmpty());
        CHECK(read(kobo() + "/update.tar") == update);
        return read(state() + "/restore.log");
    }
    void command(const QString &name, const QByteArray &body) {
        const QString executable = path("/bin/") + name;
        write(executable, "#!/bin/sh\n" + body);
        CHECK(QFile::setPermissions(executable,
                                   QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                       QFileDevice::ExeOwner));
    }
    void stall(const QString &name, const QByteArray &before = {}) {
        command(name, before +
                  // Both processes ignore TERM. The supervisor must stop the
                  // whole group, including a child holding a pipeline open.
                  "trap '' TERM\n"
                  "echo $$ > \"$NMM_HOOK_TEST_ROOT/stalled-parent\"\n"
                  "sh -c 'trap \"\" TERM; echo $$ > \"$NMM_HOOK_TEST_ROOT/stalled-child\"; exec sleep 60' &\n"
                  "wait\n"
                  ": > \"$NMM_HOOK_TEST_ROOT/continued\"\n");
    }
    void checkDeadline(bool blockedDiagnostic = false) {
        run("stop", blockedDiagnostic);
        CHECK(elapsed >= 1500 && elapsed < 10000);
        checkStalledCommandsStopped();
    }
    void checkStalledCommandsStopped() {
        CHECK(!QFile::exists(path("/continued")));
        for (const auto &name : {"stalled-parent", "stalled-child"}) {
            checkProcessStopped(read(path("/") + name));
        }
    }
    void checkProcessStopped(const QByteArray &recordedPid) {
        bool valid = false;
        const int pid = recordedPid.trimmed().toInt(&valid);
        CHECK(valid && pid > 1);
        if (!valid || pid <= 1) {
            return;
        }
#ifdef Q_OS_LINUX
        // An exited child can remain a zombie until the container's init
        // reaps it. It cannot continue the copy or keep a pipe open.
        QElapsedTimer clock;
        clock.start();
        QByteArray status;
        do {
            status = read("/proc/" + QString::number(pid) + "/stat");
        } while (!status.isEmpty() && status.mid(status.lastIndexOf(')') + 2, 1) != "Z" &&
                 clock.elapsed() < 1000);
        const bool running = !status.isEmpty() && status.mid(status.lastIndexOf(')') + 2, 1) != "Z";
#else
        const bool running = ::kill(pid, 0) == 0;
#endif
        CHECK(!running);
        // Limit the damage from a failing cleanup regression to this fixture.
        if (running) {
            ::kill(pid, SIGKILL);
        }
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

void timeoutDoesNotWaitForDiagnosticOutput() {
    Fixture fixture;
    fixture.stall("sha256sum");
    fixture.checkDeadline(true);
    CHECK(!QFile::exists(fixture.kobo() + "/Kobo.tgz"));
}

void normalCompletionLeavesNoTimerCommand() {
    Fixture fixture;
    // Keep a cancelled timer alive long enough to observe it without racing
    // its normal expiry. No production command needs to ignore TERM.
    QByteArray script = read(fixture.path("/hook.sh"));
    script.replace("deadline_seconds=2", "deadline_seconds=30");
    write(fixture.path("/hook.sh"), script);
    fixture.command("sleep",
                    "trap '' TERM\n"
                    "echo $$ >> \"$NMM_HOOK_TEST_ROOT/timer-pids\"\n"
                    "exec \"$NMM_HOOK_REAL_SLEEP\" \"$@\"\n");
    fixture.command("sha256sum",
                    "if [ ! -f \"$NMM_HOOK_TEST_ROOT/hash-started\" ]; then\n"
                    "    : > \"$NMM_HOOK_TEST_ROOT/hash-started\"\n"
                    "    for attempt in 1 2 3 4 5 6 7 8 9 10; do\n"
                    "        [ ! -f \"$NMM_HOOK_TEST_ROOT/timer-pids\" ] || break\n"
                    "        \"$NMM_HOOK_REAL_SLEEP\" 0.02\n"
                    "    done\n"
                    "fi\n"
                    "exec \"$NMM_HOOK_REAL_HASH\" \"$@\"\n");
    fixture.queued(fixture.run());
    for (const auto &line : read(fixture.path("/timer-pids")).split('\n')) {
        if (line.isEmpty()) {
            continue;
        }
        fixture.checkProcessStopped(line);
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

void interruptedSupervisorStopsItsCommands() {
    for (const int signal : {SIGHUP, SIGINT, SIGTERM}) {
        Fixture fixture;
        fixture.stall("sha256sum");
        fixture.run("stop", false, signal);
        CHECK(fixture.elapsed < 1500);
        fixture.checkStalledCommandsStopped();
        CHECK(!QFile::exists(fixture.kobo() + "/Kobo.tgz"));
    }
}

void workerStartupFailureSkipsRestoration() {
    {
        Fixture fixture;
        fixture.command("setsid", "exit 1\n");
        CHECK(fixture.run().isEmpty());
        CHECK(fixture.elapsed < 1500);
        CHECK(!QFile::exists(fixture.kobo() + "/Kobo.tgz"));
    }
    {
        Fixture fixture;
        fixture.command("bash",
                        "if [ \"$4\" = --worker ]; then exit 1; fi\n"
                        "exec \"$NMM_HOOK_REAL_BASH\" \"$@\"\n");
        CHECK(fixture.run().isEmpty());
        CHECK(fixture.elapsed < 1500);
        CHECK(!QFile::exists(fixture.kobo() + "/Kobo.tgz"));
    }
}

void normalCompletionStopsRemainingWorkerChildren() {
    Fixture fixture;
    fixture.command("cp",
                    "echo $$ > \"$NMM_HOOK_TEST_ROOT/stalled-parent\"\n"
                    "sh -c 'trap \"\" TERM; echo $$ > \"$NMM_HOOK_TEST_ROOT/stalled-child\"; exec \"$NMM_HOOK_REAL_SLEEP\" 60' &\n"
                    "while [ ! -f \"$NMM_HOOK_TEST_ROOT/stalled-child\" ]; do\n"
                    "    \"$NMM_HOOK_REAL_SLEEP\" 0.01\n"
                    "done\n"
                    "exec \"$NMM_HOOK_REAL_COPY\" \"$@\"\n");
    fixture.queued(fixture.run());
    fixture.checkStalledCommandsStopped();
}

void deadlineStopsWorkerBeforeItsGroupExists() {
    Fixture fixture;
    // Simulate a stalled setsid executable before it can create a session.
    // It has not started a shell or child commands at this point.
    fixture.command("setsid",
                    "echo $$ > \"$NMM_HOOK_TEST_ROOT/launch-pid\"\n"
                    "exec \"$NMM_HOOK_REAL_SLEEP\" 60\n");
    fixture.run();
    CHECK(fixture.elapsed >= 1500 && fixture.elapsed < 10000);
    fixture.checkProcessStopped(read(fixture.path("/launch-pid")));
    CHECK(!QFile::exists(fixture.kobo() + "/Kobo.tgz"));
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

void deadlineStopsStalledCommands() {
    {
        Fixture fixture;
        fixture.stall("sha256sum");
        fixture.checkDeadline();
        CHECK(!QFile::exists(fixture.kobo() + "/Kobo.tgz"));
    }
    {
        Fixture fixture;
        fixture.stall("cp", "printf partial > \"$2\"\n");
        fixture.checkDeadline();
        CHECK(!QFile::exists(fixture.kobo() + "/Kobo.tgz"));
        CHECK(read(fixture.kobo() + "/.Kobo.tgz.nickelmodmanager") == "partial");
        // A later invocation can replace an interrupted temporary copy.
        CHECK(QFile::remove(fixture.path("/bin/cp")));
        fixture.queued(fixture.run());
    }
    {
        Fixture fixture;
        // Stall the final sync, after the complete package has been renamed.
        fixture.stall("sync",
                      "if [ ! -f \"$NMM_HOOK_TEST_ROOT/first-sync\" ]; then\n"
                      "    : > \"$NMM_HOOK_TEST_ROOT/first-sync\"\n"
                      "    exit 0\n"
                      "fi\n");
        fixture.checkDeadline();
        CHECK(read(fixture.kobo() + "/Kobo.tgz") == fixture.package);
        CHECK(!read(fixture.state() + "/restore.log").contains("QUEUED"));
    }
}
} // namespace

int main(int argc, char **argv) {
    QCoreApplication application(argc, argv);
    if (QStandardPaths::findExecutable("sha256sum").isEmpty() ||
        QStandardPaths::findExecutable("gunzip").isEmpty() ||
        QStandardPaths::findExecutable("setsid").isEmpty() ||
        QStandardPaths::findExecutable("bash").isEmpty()) {
        std::puts("Skipped: needs sha256sum, gunzip, setsid and bash.");
        return 77;
    }
    queuesOnceAndAcceptsBootFormats();
    normalRebootAndExistingPackage();
    refusesUnsafeRestoration();
    deadlineStopsStalledCommands();
    timeoutDoesNotWaitForDiagnosticOutput();
    normalCompletionLeavesNoTimerCommand();
    interruptedSupervisorStopsItsCommands();
    workerStartupFailureSkipsRestoration();
    normalCompletionStopsRemainingWorkerChildren();
    deadlineStopsWorkerBeforeItsGroupExists();
    return failures ? 1 : 0;
}
