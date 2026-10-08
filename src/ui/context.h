#pragma once
#include <QFont>
#include <QPalette>
#include <QWidget>
namespace NickelModManager {
namespace ui {
struct Context {
    int density = 300;
    QFont font;
    QPalette palette;
    Context(int density, const QFont &font, const QPalette &palette)
        : density(density > 0 ? density : 300), font(font), palette(palette) {}
    int px(int designPixels) const;
    void apply(QWidget *widget, int fontPixels = 36) const;
};
} // namespace ui
} // namespace NickelModManager
