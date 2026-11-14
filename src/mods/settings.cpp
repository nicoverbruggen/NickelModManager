#include "settings.h"
#include "files.h"
#include "metadata.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>

namespace NickelModManager {
namespace {
using Fields = QHash<QString, QString>;
struct Section { QString name; Fields fields; };
bool readFile(const QString &path, int limit, QByteArray &bytes) {
    QFile file(path);
    if (!isRegularFile(path) || !file.open(QIODevice::ReadOnly)) {
        return false;
    }
    bytes = file.read(limit + 1);
    return bytes.size() <= limit && file.error() == QFile::NoError && !bytes.contains('\0');
}
bool regularFolder(const QString &path) {
    const QFileInfo info(path);
    return info.isDir() && !info.isSymLink();
}
bool basename(const QString &name) {
    return !name.isEmpty() && name != "." && name != ".." && !name.contains('/') &&
           !name.contains('\\') && !name.contains(QChar::Null);
}
bool parse(const QByteArray &bytes, QVector<Section> &sections) {
    // A round trip rejects malformed UTF-8 instead of publishing replacement characters.
    const QString decoded = QString::fromUtf8(bytes);
    if (decoded.toUtf8() != bytes) {
        return false;
    }
    QSet<QString> names;
    for (const QString &raw : decoded.split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) {
            continue;
        }
        if (line.startsWith('[') && line.endsWith(']')) {
            const QString name = line.mid(1, line.size() - 2);
            if (name.isEmpty() || names.contains(name)) {
                return false;
            }
            names.insert(name);
            sections.append({name, {}});
        } else {
            const int separator = line.indexOf('=');
            if (sections.isEmpty() || separator <= 0) {
                return false;
            }
            const QString key = line.left(separator).trimmed();
            auto &fields = sections.last().fields;
            if (fields.contains(key)) {
                return false;
            }
            fields.insert(key, line.mid(separator + 1).trimmed());
        }
    }
    return true;
}
Fields fieldsFor(const QVector<Section> &sections, const QString &name) {
    for (const auto &section : sections) {
        if (section.name == name) {
            return section.fields;
        }
    }
    return {};
}
bool condition(const Fields &fields, SoftwareFamily family, bool &valid) {
    bool visible = true;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (!it.key().startsWith("when.")) {
            continue;
        }
        if (it.key() != "when.os") {
            valid = false;
            return false;
        }
        bool match = false;
        for (const QString &raw : it.value().split(',')) {
            const QString os = raw.trimmed();
            if (os != "kobo" && os != "tolino") {
                valid = false;
                return false;
            }
            match |= os == (family == SoftwareFamily::Tolino ? "tolino" : "kobo");
        }
        visible &= match;
    }
    return visible;
}
QString configKey(const QByteArray &line) {
    const QByteArray content = line.left(line.indexOf('#') < 0 ? line.size() : line.indexOf('#'));
    const int colon = content.indexOf(':');
    return colon < 0 ? QString() : QString::fromUtf8(content.left(colon)).trimmed();
}
SettingValue valueFor(const QByteArray &line, bool fallback) {
    QByteArray content = line.left(line.indexOf('#') < 0 ? line.size() : line.indexOf('#'));
    const QByteArray value = content.mid(content.indexOf(':') + 1).trimmed().toLower();
    if (value.isEmpty()) {
        return {fallback, true};
    }
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return {true, true};
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return {false, true};
    }
    return {fallback, false};
}
} // namespace

QHash<QString, ModDetails> modDetails(const QString &adds, const QVector<Mod> &mods,
                                    SoftwareFamily family) {
    QHash<QString, ModDetails> result;
    const auto versions = modVersions(adds, mods);
    if (!regularFolder(adds)) {
        return result;
    }
    const QDir root(adds);
    for (const QString &folder : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        QByteArray bytes;
        const QString directory = root.filePath(folder);
        QVector<Section> sections;
        if (!readFile(directory + "/mod.manifest", 16384, bytes) || !parse(bytes, sections)) {
            continue;
        }
        const auto fields = fieldsFor(sections, "mod");
        const QString library = fields.value("library");
        QString expected;
        for (const auto &mod : mods) {
            if (mod.file == library) {
                expected = mod.hash;
                break;
            }
        }
        if (fields.value("schema") != "1" || expected.isEmpty() || fields.value("sha256").toLower() != expected ||
            !versions.contains(library) || versions.value(library) != fields.value("version")) {
            continue;
        }
        ModDetails details;
        details.library = library;
        details.name = fields.value("name");
        details.version = fields.value("version");
        details.description = fields.value("description");
        details.creator = fields.value("creator");
        if (details.name.size() > 128) { details.name.clear(); }
        if (details.description.size() > 512) { details.description.clear(); }
        if (details.creator.size() > 128) { details.creator.clear(); }
        details.folder = directory;
        details.manifest = bytes;
        const auto settings = fieldsFor(sections, "settings");
        if (!settings.isEmpty()) {
            details.config = settings.value("config");
            if (!basename(details.config) || (details.config == "mod.manifest" || details.config == "uninstall" || details.config == "uninstall-now") ||
                settings.value("format") != "colon-bool-first-v1" || (settings.value("apply") != "restart" && settings.value("apply") != "live")) {
                details.settingsError = "This settings declaration is not supported.";
            }
        }
        for (const auto &section : sections) {
            if (!section.name.startsWith("setting.")) {
                continue;
            }
            const auto &setting = section.fields;
            SettingDeclaration declaration;
            declaration.key = section.name.mid(8);
            declaration.label = setting.value("label");
            declaration.description = setting.value("description");
            declaration.section = setting.value("section");
            declaration.fallback = setting.value("fallback") == "1";
            declaration.restart = setting.value("apply", settings.value("apply")) == "restart";
            bool valid = true;
            declaration.visible = condition(setting, family, valid);
            if ((setting.contains("apply") && setting.value("apply") != "restart" && setting.value("apply") != "live") ||
                !valid || declaration.key.isEmpty() || declaration.key.contains(':') ||
                declaration.key.contains('#') || declaration.key.trimmed() != declaration.key ||
                declaration.label.isEmpty() || declaration.label.size() > 256 || declaration.description.size() > 512 ||
                declaration.section.size() > 128 || setting.value("type") != "bool" ||
                (setting.value("fallback") != "0" && setting.value("fallback") != "1") || settings.isEmpty()) {
                details.settingsError = "This manifest contains an unsupported settings declaration.";
            }
            details.settings.append(declaration);
        }
        result.insert(library, details);
    }
    return result;
}

bool readSettings(const ModDetails &details, SettingsSnapshot &snapshot, QString &error) {
    error.clear();
    QByteArray bytes;
    if (!details.settingsError.isEmpty()) {
        error = details.settingsError;
        return false;
    }
    if (!basename(details.config) || !regularFolder(details.folder) ||
        !readFile(details.folder + "/" + details.config, 262144, bytes)) {
        error = "The mod's config is missing, linked or unavailable. No file was created.";
        return false;
    }
    snapshot = {};
    snapshot.details = details;
    snapshot.original = bytes;
    const auto lines = bytes.split('\n');
    for (const auto &setting : details.settings) {
        if (!setting.visible) {
            continue;
        }
        SettingValue value{setting.fallback, true};
        for (const auto &line : lines) {
            if (configKey(line) == setting.key) {
                value = valueFor(line, setting.fallback);
                break;
            }
        }
        snapshot.values.insert(setting.key, value);
    }
    return true;
}

bool saveSettings(const SettingsSnapshot &snapshot, const ModDetails &current,
                  const QHash<QString, bool> &edits, QString &error) {
    error.clear();
    SettingsSnapshot fresh;
    if (current.library != snapshot.details.library || current.folder != snapshot.details.folder ||
        current.manifest != snapshot.details.manifest || !readSettings(current, fresh, error) ||
        fresh.original != snapshot.original) {
        if (error.isEmpty()) {
            error = "The manifest or config changed. Reopen settings before saving.";
        }
        return false;
    }
    for (auto it = edits.begin(); it != edits.end(); ++it) {
        if (!fresh.values.contains(it.key())) {
            error = "A setting is no longer available on this software.";
            return false;
        }
    }
    if (edits.isEmpty()) {
        return true;
    }
    auto lines = fresh.original.split('\n');
    QSet<QString> remaining;
    for (auto it = edits.begin(); it != edits.end(); ++it) {
        remaining.insert(it.key());
    }
    for (auto &line : lines) {
        const QString key = configKey(line);
        if (!remaining.remove(key)) {
            continue;
        }
        const int colon = line.indexOf(':'), comment = line.indexOf('#', colon);
        const QByteArray ending = line.endsWith('\r') ? QByteArray("\r") : QByteArray();
        const QByteArray suffix = comment >= 0 ? QByteArray(" ") + line.mid(comment) : ending;
        line = line.left(colon + 1) + (edits.value(key) ? "1" : "0") + suffix;
    }
    QByteArray output;
    for (int i = 0; i < lines.size(); ++i) {
        if (i) {
            output += '\n';
        }
        output += lines[i];
    }
    for (const auto &setting : current.settings) {
        if (remaining.contains(setting.key)) {
            if (!output.isEmpty() && !output.endsWith('\n')) {
                output += '\n';
            }
            output += setting.key.toUtf8() + ':' + (edits.value(setting.key) ? "1\n" : "0\n");
        }
    }
    // Recheck immediately before opening the atomic writer. Disable its direct
    // write fallback so a failed rename cannot truncate the original config.
    QByteArray check;
    const QString path = current.folder + "/" + current.config;
    if (!regularFolder(current.folder) || !readFile(path, 262144, check) || check != fresh.original) {
        error = "The config changed before saving. Reopen settings.";
        return false;
    }
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit()) {
        error = "Could not save settings: " + file.errorString();
        return false;
    }
    return true;
}
} // namespace NickelModManager
