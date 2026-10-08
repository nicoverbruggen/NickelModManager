// A test mod for the checks: it logs when Nickel loads it, can act like
// NickelHook's failsafe and stops Nickel on request. Build it under two names
// to have two independent mods.
#include "compat.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <cstdio>
#include <dlfcn.h>
#include <QImageIOPlugin>
#include <QTimer>
#include <cstdlib>
#include <syslog.h>

#ifndef PROBE_NAME
#error "Define PROBE_NAME"
#endif

namespace {
void startProbe() {
    if (QCoreApplication::arguments().isEmpty() || QFileInfo(QCoreApplication::arguments().first()).fileName() != "nickel") return;
    Dl_info info;
    const QString library = dladdr(reinterpret_cast<void *>(&startProbe), &info) && info.dli_fname
        ? QFile::decodeName(info.dli_fname) : QString();
    // Qt 5.2 also loads a held <file>.failsafe from the plugin folder.
    // Upstream NickelHook then does nothing, and so does the probe.
    if (library.endsWith(".failsafe")) {
        syslog(LOG_INFO, "nmm-probe %s skipped: loaded from its failsafe", PROBE_NAME);
        return;
    }
    syslog(LOG_INFO, "nmm-probe %s loaded", PROBE_NAME);
    // Like NickelHook, set the library aside while the mod starts and put it
    // back after a delay. Upstream NickelHook, used on firmware 4.x, renames
    // it in place; its Qt 6 port moves it one folder up.
    if (QFileInfo::exists("/mnt/onboard/.adds/nmm-probe/failsafe-" PROBE_NAME)) {
        if (!library.isEmpty()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const QString held = QFileInfo(QFileInfo(library).absolutePath()).absolutePath() + "/" + QFileInfo(library).fileName() + ".failsafe";
#else
            const QString held = library + ".failsafe";
#endif
            if (std::rename(QFile::encodeName(library).constData(), QFile::encodeName(held).constData()) == 0) {
                syslog(LOG_INFO, "nmm-probe %s armed its failsafe", PROBE_NAME);
                NickelModManager::later(QCoreApplication::instance(), 6000, [library, held] {
                    if (std::rename(QFile::encodeName(held).constData(), QFile::encodeName(library).constData()) == 0)
                        syslog(LOG_INFO, "nmm-probe %s disarmed its failsafe", PROBE_NAME);
                });
            }
        }
    }
    // A file on user storage makes this mod stop Nickel a few seconds after
    // it loads, the way a mod that breaks on new firmware would.
    if (QFileInfo::exists("/mnt/onboard/.adds/nmm-probe/crash-" PROBE_NAME)) {
        syslog(LOG_WARNING, "nmm-probe %s stops Nickel in 3 seconds", PROBE_NAME);
        // Nickel's own SIGSEGV handler returns from a raised signal, so the
        // probe aborts instead.
        NickelModManager::later(QCoreApplication::instance(), 3000, [] { std::abort(); });
    }
}
}
Q_COREAPP_STARTUP_FUNCTION(startProbe)

class ProbePlugin final : public QImageIOPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.qt-project.Qt.QImageIOHandlerFactoryInterface" FILE "probe.json")
public:
    Capabilities capabilities(QIODevice *, const QByteArray &) const override { return {}; }
    QImageIOHandler *create(QIODevice *, const QByteArray & = {}) const override { return nullptr; }
};
#include "probe.moc"
