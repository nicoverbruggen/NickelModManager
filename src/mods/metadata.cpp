#include "metadata.h"
#include "files.h"
#include <QDir>
#include <QFile>
#include <QSet>

namespace NickelModManager {
namespace {
// A small, literal UTF-8 section/key format. Only [mod] is interpreted here;
// other sections are reserved for future declarations, never setting values.
QHash<QString, QString> readManifest(const QString &path) {
    QHash<QString, QString> fields;
    if (!isRegularFile(path)) {
        return fields;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fields;
    }
    const QByteArray bytes = file.read(16385);
    if (bytes.size() > 16384 || file.error() != QFile::NoError) {
        return {};
    }
    bool metadata = false;
    for (const QByteArray &raw : bytes.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty() || line.startsWith('#') || line.startsWith(';')) {
            continue;
        }
        if (line.startsWith('[') && line.endsWith(']')) {
            metadata = line == "[mod]";
            continue;
        }
        if (!metadata) {
            continue;
        }
        const int separator = line.indexOf('=');
        if (separator <= 0) {
            return {};
        }
        const QString key = line.left(separator).trimmed();
        if (fields.contains(key)) {
            return {};
        }
        fields.insert(key, line.mid(separator + 1).trimmed());
    }
    return fields;
}
} // namespace

QHash<QString, QString> modVersions(const QString &adds, const QVector<Mod> &mods) {
    QHash<QString, QString> hashes, versions;
    QSet<QString> ambiguous;
    for (const auto &mod : mods) {
        hashes.insert(mod.file, mod.hash);
    }
    const QDir root(adds);
    for (const QString &folder : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks)) {
        const auto fields = readManifest(root.filePath(folder + "/mod.manifest"));
        const QString library = fields.value("library"), version = fields.value("version"),
                      hash = fields.value("sha256").toLower();
        if (fields.value("schema") != "1" || !validModFilename(library) ||
            !hashes.contains(library) || hash.size() != 64 || hash != hashes.value(library) ||
            version.isEmpty() || version.size() > 128) {
            continue;
        }
        bool printable = true;
        for (const QChar character : version) {
            if (!character.isPrint()) {
                printable = false;
            }
        }
        if (!printable) {
            continue;
        }
        if (versions.contains(library)) {
            ambiguous.insert(library);
        }
        versions.insert(library, version);
    }
    for (const auto &library : ambiguous) {
        versions.remove(library);
    }
    return versions;
}
} // namespace NickelModManager
