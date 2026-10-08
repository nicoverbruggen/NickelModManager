#pragma once
#include <QIcon>
#include <QPainterPath>
namespace NickelModManager {
namespace ui {
enum class Icon { Back, ChevronRight, ChevronLeft, Restart, Blocks, Lock, Info, Alert };
// iconPath returns the unscaled 24-unit path, also used by geometry tests.
QPainterPath iconPath(Icon kind);
// icon fits stroked bounds to the requested size. Zero stroke selects a scaled width.
QIcon icon(Icon kind, const QColor &color, qreal stroke = 0);
const char *iconNotice();
} // namespace ui
} // namespace NickelModManager
