#include "fonts.h"
#include <QFontDatabase>

namespace NickelModManager {
namespace ui {
namespace {
QString selectFamily(const QStringList &families, const QString &modern,
                     const QString &legacy, const QString &fallback) {
    if (families.contains(modern, Qt::CaseInsensitive)) {
        return modern;
    }
    if (families.contains(legacy, Qt::CaseInsensitive)) {
        return legacy;
    }
    return fallback;
}
} // namespace

SystemFonts systemFonts(const QStringList &families, const QString &menuFamily) {
    if (menuFamily.compare("Bariol", Qt::CaseInsensitive) == 0 &&
        families.contains("Bariol", Qt::CaseInsensitive)) {
        return {"Bariol", "Bariol"};
    }
    return {selectFamily(families, "Rakuten Serif", "Georgia", "DefaultSerif"),
            selectFamily(families, "Rakuten Sans", "Avenir Next", "DefaultSansSerif")};
}

SystemFonts systemFonts(const QString &menuFamily) {
    return systemFonts(QFontDatabase().families(), menuFamily);
}

SystemFonts systemFonts() {
    return systemFonts(QFontDatabase().families());
}
} // namespace ui
} // namespace NickelModManager
