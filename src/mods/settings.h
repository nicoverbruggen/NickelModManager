#pragma once
#include "mod.h"
#include "../software.h"
#include <QHash>
#include <QVector>

namespace NickelModManager {
struct SettingDeclaration {
    QString key, label, description, section;
    bool fallback = false;
    bool restart = true;
    bool visible = true;
};
struct ModDetails {
    QString library, name, version, description, creator, folder, config;
    QByteArray manifest;
    QVector<SettingDeclaration> settings;
    QString settingsError;
};
// modDetails accepts one unambiguous declaration matching the observed library
// hash. Folders and files must not be symlinks. Invalid settings never block
// ordinary library management or expose unsupported conditions.
QHash<QString, ModDetails> modDetails(const QString &adds, const QVector<Mod> &mods,
                                    SoftwareFamily family);
struct SettingValue {
    SettingValue(bool value = false, bool valid = true) : value(value), valid(valid) {}
    bool value;
    bool valid;
};
struct SettingsSnapshot {
    ModDetails details;
    QByteArray original;
    QHash<QString, SettingValue> values;
};
// readSettings never creates a config or repairs malformed values.
bool readSettings(const ModDetails &details, SettingsSnapshot &snapshot, QString &error);
// saveSettings writes only explicit, visible edits to the first effective key.
// The caller supplies a freshly discovered declaration. Changed declarations
// or config content cause a conflict. Publication uses an atomic replacement;
// failure leaves the prior config intact. No folder or config is created.
bool saveSettings(const SettingsSnapshot &snapshot, const ModDetails &current,
                  const QHash<QString, bool> &edits, QString &error);
} // namespace NickelModManager
