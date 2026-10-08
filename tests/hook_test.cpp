// Checks the shutdown hook's files in a temporary system and user storage.
#include "updates/hook.h"
#include "mods/files.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QTemporaryDir>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <utime.h>

namespace {
int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

void write(const QString &path, const QByteArray &bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) std::abort();
}
QByteArray read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
QString link(const QString &path) {
    char buffer[256];
    const ssize_t size = readlink(QFile::encodeName(path).constData(), buffer, sizeof buffer);
    return size > 0 ? QString::fromLocal8Bit(buffer, int(size)) : QString();
}
QString run(const QString &program, const QStringList &arguments, int timeout = 10000) {
    QProcess process;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0) && defined(Q_OS_UNIX)
    // QEMU rejects the clone flags Qt uses for vfork. A child modifier
    // selects Qt's fork path, so ARM tests can still run the host tools.
    process.setChildProcessModifier([] {});
#endif
    process.start(program, arguments);
    if (!process.waitForStarted(timeout) || !process.waitForFinished(timeout)) {
        process.kill();
        process.waitForFinished(10000);
        return "FAILED";
    }
    return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0 ? QString::fromUtf8(process.readAllStandardOutput()) : QString("FAILED");
}

struct System {
    QTemporaryDir dir;
    QString root() const { return dir.path() + "/root"; }
    QString storage() const { return dir.path() + "/onboard/.adds/nickelmodmanager"; }
    QString self() const { return root() + "/usr/local/Kobo/imageformats/libnickelmm.so"; }
    QVector<QPair<QString, QStringList>> contract;
    System() {
        for (const char *name : {"/etc/init.d/ota", "/etc/init.d/rc", "/etc/init.d/mount-userdata", "/usr/libexec/platform/utils.sh"}) {
            write(root() + name, QByteArray("stock ") + name);
            QString error;
            contract.append({name, {NickelModManager::hashFile(root() + name, error)}});
        }
        QDir().mkpath(root() + "/etc/rc6.d");
        write(self(), "manager");
        write(storage() + "/mods/libnm.so", "menu");
        struct utimbuf times = {1700000000, 1700000000};
        utime(QFile::encodeName(storage() + "/mods/libnm.so").constData(), &times);
    }
    NickelModManager::UpdateHook hook() const { return NickelModManager::UpdateHook(root(), storage(), self(), contract); }
};

void installsAndPackages() {
    System system;
    auto hook = system.hook();
    // On by default: the first refresh sets it up.
    CHECK(hook.supported() && hook.enabled() && !hook.installed());
    QString error;
    CHECK(hook.refresh({system.storage() + "/mods/libnm.so"}, error));
    CHECK(hook.enabled() && hook.installed());
    const QString init = system.root() + "/etc/init.d/nickelmodmanager";
    CHECK(read(init) == NickelModManager::UpdateHook::script());
    CHECK(QFileInfo(init).permissions() & QFileDevice::ExeOther);
    CHECK(link(system.root() + "/etc/rc6.d/K00nickelmodmanager") == "/etc/init.d/nickelmodmanager");
    CHECK(link(system.root() + "/etc/rc6.d/S00nickelmodmanager") == "/etc/init.d/nickelmodmanager");
    const QString package = system.storage() + "/restore/Kobo.tgz";
    CHECK(NickelModManager::hashFile(package, error) + "\n" == QString::fromLatin1(read(system.storage() + "/restore/Kobo.tgz.sha256")));
    CHECK(NickelModManager::hashFile(system.self(), error) + "\n" == QString::fromLatin1(read(system.storage() + "/restore/manager.sha256")));
    // For a check with the firmware's BusyBox tar, which the stock ota script uses.
    if (const char *keep = std::getenv("NMM_KEEP_PACKAGE")) QFile::copy(package, QString::fromLocal8Bit(keep));
    CHECK(run("gzip", {"-t", package}) != "FAILED");
    const QString listing = run("tar", {"-tvzf", package});
    CHECK(listing.contains("imageformats/libnickelmm.so") && listing.contains("imageformats/libnm.so"));
    CHECK(listing.contains("root"));
    // The archive keeps the build date of the kept copy.
    QTemporaryDir out;
    CHECK(run("tar", {"-xzf", package, "-C", out.path()}) != "FAILED");
    CHECK(read(out.path() + "/imageformats/libnm.so") == "menu");
    CHECK(QFileInfo(out.path() + "/imageformats/libnm.so").lastModified().toMSecsSinceEpoch() / 1000 == 1700000000);

    // An unchanged set of libraries keeps the package; a new one rewrites it.
    struct utimbuf old = {1600000000, 1600000000};
    utime(QFile::encodeName(package).constData(), &old);
    CHECK(hook.refresh({system.storage() + "/mods/libnm.so"}, error));
    CHECK(QFileInfo(package).lastModified().toMSecsSinceEpoch() / 1000 == 1600000000);
    CHECK(hook.refresh({}, error));
    CHECK(QFileInfo(package).lastModified().toMSecsSinceEpoch() / 1000 != 1600000000);
    const QString withoutMenu = run("tar", {"-tzf", package});
    CHECK(withoutMenu != "FAILED" && !withoutMenu.contains("libnm.so"));

    // A firmware update removes the entries; the next refresh puts them back.
    QFile::remove(system.root() + "/etc/rc6.d/K00nickelmodmanager");
    QFile::remove(init);
    CHECK(!hook.installed() && hook.refresh({}, error) && hook.installed());

    CHECK(hook.setEnabled(false, {}, error));
    CHECK(!hook.enabled() && !QFileInfo::exists(init) && link(system.root() + "/etc/rc6.d/S00nickelmodmanager").isEmpty());
    CHECK(!QFileInfo::exists(system.storage() + "/restore") && QFileInfo::exists(system.storage() + "/restore-off"));
    // Off stays off: a refresh does nothing until the user turns it on.
    CHECK(hook.refresh({}, error) && !hook.installed());
    CHECK(hook.setEnabled(true, {}, error) && hook.enabled() && hook.installed());
    CHECK(!QFileInfo::exists(system.storage() + "/restore-off"));
}

void refusesWhatIsNotOurs() {
    System system;
    write(system.root() + "/etc/init.d/nickelmodmanager", "#!/bin/sh\nsomeone else\n");
    auto hook = system.hook();
    QString error;
    CHECK(!hook.setEnabled(true, {}, error) && !error.isEmpty());
    CHECK(!hook.installed() && read(system.root() + "/etc/init.d/nickelmodmanager") == "#!/bin/sh\nsomeone else\n");
    // Turning off leaves foreign files alone.
    CHECK(symlink("/etc/init.d/other", QFile::encodeName(system.root() + "/etc/rc6.d/K00nickelmodmanager").constData()) == 0);
    CHECK(hook.setEnabled(false, {}, error));
    CHECK(link(system.root() + "/etc/rc6.d/K00nickelmodmanager") == "/etc/init.d/other");
    CHECK(QFileInfo::exists(system.root() + "/etc/init.d/nickelmodmanager"));

    // Unknown stock scripts: the hook stays off.
    System changed;
    write(changed.root() + "/etc/init.d/ota", "changed");
    auto other = changed.hook();
    CHECK(!other.supported() && !other.refresh({}, error) && !other.installed());
}

// The shell script and the library check the same stock hashes.
void contractMatchesScript() {
    QSet<QString> script, library;
    const auto matches = QRegularExpression("[0-9a-f]{64}").globalMatch(QString::fromLatin1(NickelModManager::UpdateHook::script()));
    for (auto it = matches; it.hasNext();) script.insert(it.next().captured());
    for (const auto &entry : NickelModManager::UpdateHook::contract())
        for (const auto &hash : entry.second) library.insert(hash);
    CHECK(!script.isEmpty() && script == library);
}
}

int main() {
    // A process that has not finished can still report an exit code of zero.
    CHECK(run("/nickelmodmanager-test-missing-program", {}) == "FAILED");
    CHECK(run("sleep", {"0"}) != "FAILED");
    CHECK(run("sleep", {"1"}, 50) == "FAILED");
    installsAndPackages();
    refusesWhatIsNotOurs();
    contractMatchesScript();
    if (failures) { std::fprintf(stderr, "%d checks failed\n", failures); return 1; }
    std::puts("All hook checks passed.");
    return 0;
}
