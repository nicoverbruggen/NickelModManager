#include "persistence.h"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>

namespace NickelModManager {
namespace {
// Apply filename rules before saved entries can become library paths. Repeated
// entries are ignored so one library cannot carry conflicting saved states.
void readModEntries(const QJsonArray &entries,
                    const QString &managerFilename,
                    SavedModState &state) {
    QSet<QString> known;
    for (const auto &value : entries) {
        const auto entry = value.toObject();
        const QString name = entry.value("file").toString();
        if (!validModFilename(name) || isStockLibrary(name) || name == managerFilename ||
            known.contains(name)) {
            continue;
        }
        Mod mod(name, modDisplayName(name));
        mod.hash = entry.value("sha256").toString();
        mod.enabled = entry.value("enabled").toBool();
        mod.note = entry.value("note").toString().left(512);
        mod.added = qint64(entry.value("added").toDouble());
        mod.built = qint64(entry.value("built").toDouble());
        state.mods.append(mod);
        known.insert(name);
    }
}

QJsonObject encodeMod(const Mod &mod) {
    QJsonObject entry;
    entry.insert("file", mod.file);
    entry.insert("sha256", mod.hash);
    entry.insert("enabled", mod.enabled);
    entry.insert("note", mod.note);
    entry.insert("added", double(mod.added));
    entry.insert("built", double(mod.built));
    return entry;
}

// Bound reads to the state-file limit. Return an empty object on any read or
// parse failure so the caller handles it through damaged-state recovery.
QJsonObject readStateObject(QFile &file) {
    QJsonParseError parse;
    const QJsonDocument document = file.open(QIODevice::ReadOnly) && file.size() <= 1024 * 1024
                                       ? QJsonDocument::fromJson(file.readAll(), &parse)
                                       : QJsonDocument();
    file.close();
    return document.object();
}

QJsonObject encodeState(const SavedModState &state) {
    QJsonArray mods;
    for (const auto &mod : state.mods) {
        mods.append(encodeMod(mod));
    }
    QJsonObject root;
    root.insert("schema", 1);
    root.insert("firmware", state.firmware);
    root.insert("mods", mods);
    return root;
}
} // namespace

LoadedModState loadModState(const QString &path, const QString &managerFilename) {
    LoadedModState result;
    QFile file(path);
    if (!file.exists()) {
        result.fresh = true;
        return result;
    }

    const QJsonObject root = readStateObject(file);
    if (root.value("schema").toInt() != 1) {
        QFile::remove(path + ".bad");
        QFile::rename(path, path + ".bad");
        result.error = "state.json could not be read. It was kept as state.json.bad.";
        result.fresh = true;
        return result;
    }

    result.state.firmware = root.value("firmware").toString();
    readModEntries(root.value("mods").toArray(), managerFilename, result.state);
    return result;
}

bool saveModState(const QString &path, const SavedModState &state, QString &error) {
    QSaveFile file(path);
    const QByteArray bytes = QJsonDocument(encodeState(state)).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) < 0 || !file.commit()) {
        error = path + ": " + file.errorString();
        return false;
    }
    return true;
}
} // namespace NickelModManager
