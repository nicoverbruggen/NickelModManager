#pragma once
#include <QByteArray>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVector>

namespace NickelModManager {
// UpdateHook installs restore-hook.sh on the system partition and prepares
// its restore package on user storage. The script queues that package during
// the reboot into recovery for a full firmware 5.x/6.x update.
class UpdateHook {
  public:
    // UpdateHook takes a system root prefix, storage directory and path to
    // the manager library. root is empty on a device. Tests can substitute
    // the required stock script hashes through contract.
    UpdateHook(QString root,
               QString storage,
               QString self,
               QVector<QPair<QString, QStringList>> contract = UpdateHook::contract());

    // supported returns true if every stock script matches a known hash.
    bool supported() const;

    // enabled returns true unless storage contains the restore-off marker.
    // This preference is independent of support and installation.
    bool enabled() const;

    // installed checks the exact script bytes and both rc6.d link targets.
    bool installed() const;

    // setEnabled records the preference. On prepares the package and installs
    // the hook; off records the opt-out before attempting cleanup. libraries
    // contains paths to saved mod copies. Returns false with error set on
    // failure, including cleanup failure. Earlier changes remain.
    bool setEnabled(bool on, const QStringList &libraries, QString &error);

    // refresh prepares the package and installs missing shutdown entries.
    // libraries contains paths to saved mod copies. Does nothing while off.
    // Returns false with error set on failure, without undoing earlier writes.
    bool refresh(const QStringList &libraries, QString &error);

    // script returns the embedded shutdown script.
    static QByteArray script();

    // contract returns accepted hashes per stock script path. restore-hook.sh
    // checks the same list; a test keeps the two equal.
    static QVector<QPair<QString, QStringList>> contract();

  private:
    using PackageEntries = QVector<QPair<QString, QString>>;
    bool checkInstallPaths(QString &error) const;
    bool writeInitScript(QString &error);
    bool writeShutdownLinks(QString &error);
    PackageEntries packageEntries(const QStringList &libraries) const;
    QByteArray packageManifest(const PackageEntries &entries) const;
    bool packageIsCurrent(const QByteArray &manifest, QString &error) const;
    bool
    writeRestorePackage(const PackageEntries &entries, const QByteArray &manifest, QString &error);
    // Check ownership before writing or removing any system entry. The user
    // preference lives in storage, separately from whether entries exist.
    bool install(QString &error);
    bool uninstall(QString &error);
    QString root_, storage_, self_;
    QVector<QPair<QString, QStringList>> contract_;
};
} // namespace NickelModManager
