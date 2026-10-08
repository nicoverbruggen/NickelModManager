#include "mod.h"
#include <QHash>
#include <QRegularExpression>

namespace NickelModManager {
bool validModFilename(const QString &file) {
    // State entries become basenames in both the plugin folder and storage.
    // Reject paths and limit names before using them in either location.
    static const QRegularExpression pattern("^[A-Za-z0-9_][A-Za-z0-9_.+-]{0,126}[.]so$");
    return pattern.match(file).hasMatch();
}

QString modDisplayName(const QString &file) {
    // Library names from each project's Makefile (override LIBRARY), found
    // through NickelHook's list of mods and a GitHub search for projects
    // that include NickelHook.mk, on 2026-10-08.
    static const QHash<QString, QString> known{
        {"libnm.so", "NickelMenu"},
        {"libns.so", "NickelSeries"},
        {"libdfh.so", "kobo-dotfile-hack"},
        {"libndb.so", "NickelDBus"},
        {"libnickelclock.so", "NickelClock"},
        {"libbtpt.so", "kobo-btpt"},
        {"libnickelscreensaver.so", "nickel-screensaver"},
        {"libnickelnote.so", "NickelNote"},
        {"libpocket.so", "KoboOmnivoreConverter"},
        {"libnickeltypefix.so", "NickelTypeFix"},
        {"libhardcover.so", "NickelHardcover"},
        {"libnickelcoverfix.so", "NickelCoverFix"},
        {"libnickelhome.so", "NickelHome"},
        {"libnickeldissolve.so", "NickelDissolve"},
        {"libnickelrotate.so", "NickelRotate"},
        {"libnickelreaderfix.so", "NickelReaderFix"},
        {"libkobalt.so", "Kobalt"},
        {"libtweaks.so", "Kobo Tweaks"},
        {"libtiengviet.so", QString::fromUtf8("Kobo Ti\xe1\xba\xbfng Vi\xe1\xbb\x87t")},
        {"libpocketproxy.so", "kobo-pocket-proxy"},
        {"libnickelcloud.so", "NickelCloud"},
        {"libstorygraph.so", "NickelStorygraph"},
        {"libbokopds.so", "BokOPDS"},
        {"libnkpm.so", "kpm"},
        {"libcustomnotebooktemplates.so", "KoboNotebookPlus templates"},
    };
    if (known.contains(file)) {
        return known.value(file);
    }
    QString name = file.left(file.size() - 3);
    return name.startsWith("lib") && name.size() > 3 ? name.mid(3) : name;
}

// Firmware 4.x ships Kobo's own image plugin in the same folder. It is part
// of the firmware, not a mod, and the manager never touches it.
bool isStockLibrary(const QString &file) {
    return file == "libkimg.so";
}

} // namespace NickelModManager
