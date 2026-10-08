#include "startup.h"
#include "lifecycle.h"
#include "controller.h"
#include "compat.h"
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <dlfcn.h>
#include <syslog.h>

namespace {
QString selfPath() {
    // Resolve an address in this library rather than guessing the plugin
    // folder. Qt5 and Qt6 installations use different package layouts.
    Dl_info info;
    if (!dladdr(reinterpret_cast<void *>(&selfPath), &info) || !info.dli_fname) {
        return {};
    }
    return QFileInfo(QFile::decodeName(info.dli_fname)).absoluteFilePath();
}

// revinfo holds the git revision of the firmware build, which every full
// update replaces. Nickel's version string is the fallback.
QString firmwareIdentity() {
    QFile revinfo("/usr/local/Kobo/revinfo");
    if (revinfo.open(QIODevice::ReadOnly)) {
        const QString line = QString::fromUtf8(revinfo.readLine(256)).trimmed();
        if (!line.isEmpty()) {
            return line;
        }
    }
    return QCoreApplication::applicationVersion();
}

} // namespace

namespace NickelModManager {
// --- startup

void startModManager() {
    // Other programs that link Qt, such as Nickel's helpers, can load this
    // plugin too. Only Nickel gets the manager.
    if (!qobject_cast<QApplication *>(QCoreApplication::instance())) {
        return;
    }
    if (QCoreApplication::arguments().isEmpty() ||
        QFileInfo(QCoreApplication::arguments().first()).fileName() != "nickel") {
        return;
    }
    // The plugin can load after QApplication exists. Use a local value so
    // registration in another translation unit cannot precede its initialization.
    const QString storage = "/mnt/onboard/.adds/nickelmodmanager";
    const QString self = selfPath();
    if (self.isEmpty()) {
        syslog(LOG_ERR, "NickelModManager: cannot find its own library");
        return;
    }
    // Keep callback code loaded even if Qt releases its plugin handle. The
    // deliberate process-lifetime reference also survives renaming or unlinking.
    if (!dlopen(QFile::encodeName(self).constData(), RTLD_LAZY | RTLD_NODELETE)) {
        syslog(LOG_ERR, "NickelModManager: cannot retain library: %s", dlerror());
        return;
    }
    const QString firmware = firmwareIdentity();
    static Lifecycle lifecycle(self, storage, firmware);
    QString error;
    const auto result = lifecycle.begin(error);
    if (result != Lifecycle::Result::Start) {
        if (result == Lifecycle::Result::Removed) {
            syslog(LOG_INFO, "NickelModManager: uninstalled");
        } else {
            syslog(LOG_ERR, "NickelModManager: startup: %s", qPrintable(error));
        }
        return;
    }
    syslog(LOG_INFO, lifecycle.armed() ? "NickelModManager: startup failsafe armed"
                                       : "NickelModManager: startup failsafe not needed");
    // Only a full update on firmware 5.x and 6.x replaces the plugin folder.
    static NickelModManager::ModStore store(QFileInfo(self).absolutePath(), storage,
                                            QFileInfo(self).fileName(), firmware,
                                            QT_VERSION >= QT_VERSION_CHECK(6, 0, 0));
    const auto report = store.start();
    for (const auto &file : report.restored) {
        syslog(LOG_INFO, "NickelModManager: put back %s", qPrintable(file));
    }
    for (const auto &file : report.removedOutside) {
        syslog(LOG_INFO, "NickelModManager: %s was removed outside Manage Mods", qPrintable(file));
    }
    for (const auto &file : report.parked) {
        syslog(LOG_WARNING, "NickelModManager: NickelHook's failsafe holds %s", qPrintable(file));
    }
    for (const auto &error : report.errors) {
        syslog(LOG_ERR, "NickelModManager: %s", qPrintable(error));
    }
    syslog(LOG_INFO, "NickelModManager: started with %d mods", int(store.mods().size()));
    NickelModManager::startController(&store, report, self, storage, [] {
        if (!lifecycle.armed()) {
            return;
        }
        // Keep startup protected until Nickel has reached Home and stayed
        // responsive for three seconds. A crash or early exit leaves it parked.
        later(qApp, 3000, [] {
            QString failure;
            if (!lifecycle.confirm(failure)) {
                syslog(LOG_ERR, "NickelModManager: startup failsafe: %s", qPrintable(failure));
            } else {
                syslog(LOG_INFO, "NickelModManager: startup confirmed");
            }
        });
    });
}
} // namespace NickelModManager
