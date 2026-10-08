#pragma once
#include <QPair>
#include <QString>
#include <QVector>

namespace NickelModManager {
// writePackage replaces target with a gzip-compressed tar archive. Each
// entry pairs an archive name with a source path. Names must fit the ustar
// name field and be relative to /usr/local/Kobo. Returns false with error
// set on failure. The archive preserves each source's modification time.
bool writePackage(const QString &target, const QVector<QPair<QString, QString>> &entries,
                  QString &error);

} // namespace NickelModManager
