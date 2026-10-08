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
// firmware uses Georgia and Avenir Next; newer firmware uses Rakuten fonts.
SystemFonts systemFonts(const QStringList &families);
SystemFonts systemFonts();
} // namespace ui
} // namespace NickelModManager
