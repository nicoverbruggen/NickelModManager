#pragma once
#include "context.h"
#include "../mods/settings.h"
#include <functional>

namespace NickelModManager {
namespace ui {
// showModSettings owns a modal settings/details page for this invocation.
// save receives explicit edits and returns an error without closing on failure.
void showModSettings(QWidget *parent, const Context &context, const QString &name,
                     const ModDetails &details, const SettingsSnapshot &snapshot,
                     const QString &unavailable,
                     const std::function<bool(const QHash<QString, bool> &, QString &)> &save);
} // namespace ui
} // namespace NickelModManager
