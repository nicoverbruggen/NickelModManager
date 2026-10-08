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

SystemFonts systemFonts(const QStringList &families) {
    return {selectFamily(families, "Rakuten Serif", "Georgia", "DefaultSerif"),
            selectFamily(families, "Rakuten Sans", "Avenir Next", "DefaultSansSerif")};
}

SystemFonts systemFonts() {
    return systemFonts(QFontDatabase().families());
}
} // namespace ui
} // namespace NickelModManager
