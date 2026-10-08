// Check glyph geometry and firmware font selection without a GUI platform.
#include "ui/fonts.h"
#include "ui/icons.h"
#include <QTransform>
#include <cmath>
#include <cstdio>

namespace {
int failures = 0;
#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition);            \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)
void checkBounds() {
    using namespace NickelModManager::ui;
    for (const auto kind : {Icon::Back, Icon::ChevronRight, Icon::ChevronLeft, Icon::Restart,
                            Icon::Blocks, Icon::Lock, Icon::Info, Icon::Alert}) {
        const auto path = iconPath(kind);
        CHECK(!path.isEmpty());
        CHECK(QRectF(0, 0, 24, 24).contains(path.boundingRect()));
        for (int index = 0; index < path.elementCount(); ++index) {
            const auto point = path.elementAt(index);
            CHECK(std::isfinite(point.x) && std::isfinite(point.y));
        }
    }
    CHECK(iconPath(Icon::Back).boundingRect() == QRectF(5, 5, 14, 14));
    CHECK(iconPath(Icon::ChevronRight).boundingRect() == QRectF(9, 6, 6, 12));
    CHECK(iconPath(Icon::Restart).boundingRect() == QRectF(3, 3, 18, 18));
    CHECK(iconPath(Icon::Info).boundingRect() == QRectF(2, 2, 20, 20));
    QTransform mirror;
    mirror.translate(24, 0);
    mirror.scale(-1, 1);
    CHECK(iconPath(Icon::ChevronLeft) == mirror.map(iconPath(Icon::ChevronRight)));
    const QByteArray notice = iconNotice();
    CHECK(notice.contains("Lucide") && notice.contains("Cole Bemis"));
}
void checkSystemFonts() {
    using NickelModManager::ui::systemFonts;
    const auto old = systemFonts({"Georgia", "Avenir Next", "KBJ-UDKakugo Pr6N M"});
    CHECK(old.serif == "Georgia" && old.sans == "Avenir Next");
    const auto modern = systemFonts({"Rakuten Serif", "Rakuten Sans"});
    CHECK(modern.serif == "Rakuten Serif" && modern.sans == "Rakuten Sans");
    const auto both = systemFonts({"Georgia", "Avenir Next", "Rakuten Serif", "Rakuten Sans"});
    CHECK(both.serif == "Rakuten Serif" && both.sans == "Rakuten Sans");
    const auto mixed = systemFonts({"Georgia", "Rakuten Sans"});
    CHECK(mixed.serif == "Georgia" && mixed.sans == "Rakuten Sans");
    const auto tolino = systemFonts({"Bariol", "Rakuten Serif", "Rakuten Sans"}, "Bariol");
    CHECK(tolino.serif == "Bariol" && tolino.sans == "Bariol");
    const auto kobo = systemFonts({"Bariol", "Rakuten Serif", "Rakuten Sans"}, "Rakuten Serif");
    CHECK(kobo.serif == "Rakuten Serif" && kobo.sans == "Rakuten Sans");
    const auto missingTolino = systemFonts({"Rakuten Serif", "Rakuten Sans"}, "Bariol");
    CHECK(missingTolino.serif == "Rakuten Serif" && missingTolino.sans == "Rakuten Sans");
    const auto tolinoCase = systemFonts({"bariol"}, "BARIOL");
    CHECK(tolinoCase.serif == "Bariol" && tolinoCase.sans == "Bariol");
    const auto unknown = systemFonts(QStringList{});
    CHECK(unknown.serif == "DefaultSerif" && unknown.sans == "DefaultSansSerif");
}
} // namespace
int main() {
    checkBounds();
    checkSystemFonts();
    return failures ? 1 : 0;
}
