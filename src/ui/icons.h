#pragma once
#include <QIcon>
#include <QPainterPath>
namespace NickelModManager {
namespace ui {
enum class Icon { Back, ChevronRight, ChevronLeft, Restart, Blocks, Lock, Info, Alert, Settings, Download, Ask, Shield, Restore };
// iconPath returns the unscaled 24-unit path, also used by geometry tests.
QPainterPath iconPath(Icon kind);
// icon fits filled outline bounds to the requested size. The legacy stroke
// argument is retained for existing callers; Phosphor outlines fix their weight.
QIcon icon(Icon kind, const QColor &color, qreal stroke = 0);
const char *iconNotice();
} // namespace ui
} // namespace NickelModManager
