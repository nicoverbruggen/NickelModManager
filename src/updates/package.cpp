#include "package.h"
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>
#include <cstring>

namespace NickelModManager {
namespace {
bool save(const QString &path, const QByteArray &bytes, QString &error) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = path + ": " + file.errorString();
        return false;
    }
    return true;
}
quint32 crc32(const QByteArray &data) {
    // gzip's trailer uses CRC-32 of the uncompressed tar, not the Adler-32
    // that qCompress supplies in its zlib stream.
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k) {
                c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        ready = true;
    }
    quint32 c = 0xffffffffu;
    for (const char byte : data) {
        c = table[(c ^ quint8(byte)) & 0xff] ^ (c >> 8);
    }
    return c ^ 0xffffffffu;
}
void little(QByteArray &out, quint32 value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.append(char((value >> shift) & 0xff));
    }
}
// qCompress returns a length prefix and zlib stream. gzip uses the same
// deflate bytes with its own header, CRC-32 and uncompressed-length trailer.
QByteArray gzipArchive(const QByteArray &tar) {
    const QByteArray zlib = qCompress(tar, 6);
    QByteArray gzip("\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\x03", 10);
    gzip += zlib.mid(6, zlib.size() - 10);
    little(gzip, crc32(tar));
    little(gzip, quint32(tar.size()));
    return gzip;
}

// One ustar header block, owned by root.
QByteArray header(const QString &name, qint64 size, qint64 mtime, bool directory) {
    QByteArray block(512, '\0');
    const auto put = [&block](int offset, const QByteArray &value) {
        memcpy(block.data() + offset, value.constData(), size_t(value.size()));
    };
    const auto octal = [](qint64 value, int width) {
        return QByteArray::number(value, 8).rightJustified(width - 1, '0');
    };
    put(0, name.toUtf8().left(99));
    put(100, octal(0755, 8));
    put(108, octal(0, 8));
    put(116, octal(0, 8));
    put(124, octal(size, 12));
    put(136, octal(mtime, 12));
    // ustar computes the header checksum with this field filled with spaces.
    put(148, QByteArray(8, ' '));
    block[156] = directory ? '5' : '0';
    put(257, QByteArray("ustar\0"
                        "00",
                        8));
    put(265, "root");
    put(297, "root");
    unsigned sum = 0;
    for (const char byte : block) {
        sum += quint8(byte);
    }
    put(148, QByteArray::number(sum, 8).rightJustified(6, '0') + QByteArray("\0 ", 2));
    return block;
}
} // namespace

bool writePackage(const QString &target, const QVector<QPair<QString, QString>> &entries,
                  QString &error) {
    // Build the archive in memory, then replace the output with QSaveFile.
    // The device installer expects paths relative to /usr/local/Kobo.
    QByteArray tar;
    QStringList folders;
    for (const auto &entry : entries) {
        const QString folder = entry.first.section('/', 0, -2);
        if (!folder.isEmpty() && !folders.contains(folder)) {
            folders << folder;
            tar += header(folder + "/", 0,
                          QFileInfo(entry.second).lastModified().toMSecsSinceEpoch() / 1000, true);
        }
        QFile file(entry.second);
        if (!file.open(QIODevice::ReadOnly)) {
            error = entry.second + ": " + file.errorString();
            return false;
        }
        const QByteArray bytes = file.readAll();
        // The archive keeps the file time, so the build date survives.
        tar += header(entry.first, bytes.size(),
                      QFileInfo(entry.second).lastModified().toMSecsSinceEpoch() / 1000, false);
        tar += bytes;
        // Every member ends on a 512-byte tar block boundary.
        tar += QByteArray((512 - bytes.size() % 512) % 512, '\0');
    }
    // Two empty blocks terminate the archive, including an empty package.
    tar += QByteArray(1024, '\0');
    return save(target, gzipArchive(tar), error);
}
} // namespace NickelModManager
