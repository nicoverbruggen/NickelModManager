#pragma once
#include <QString>
#include <QStringList>

namespace NickelModManager {
namespace ui {
struct SystemFonts {
    QString serif;
    QString sans;
};

// systemFonts selects Nickel's UI fonts from the installed families. Older
// firmware uses Georgia and Avenir Next; newer Kobo firmware uses Rakuten fonts.
// A native Bariol menu selects Tolino's single UI family, even when Rakuten fonts exist.
SystemFonts systemFonts(const QStringList &families, const QString &menuFamily = {});
SystemFonts systemFonts(const QString &menuFamily);
SystemFonts systemFonts();
} // namespace ui
} // namespace NickelModManager
