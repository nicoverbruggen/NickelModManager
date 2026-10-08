#pragma once
#include <QByteArray>
#include <QString>

namespace NickelModManager {
// Lifecycle owns the manager's installation markers and startup failsafe.
// Paths name the loaded library and user-storage directory. firmware is the
// current firmware identity. root is empty on a device; tests supply a
// temporary system root for shutdown-hook cleanup.
class Lifecycle {
  public:
    enum class Result { Start, Removed, Error };
    Lifecycle(QString self, QString storage, QString firmware, QString root = QString());

    // begin handles removal before any mod discovery or controller startup.
    // Otherwise it arms the startup failsafe when this library or the firmware
    // differs from the last confirmed start: it moves the library outside the
    // plugin directory, so a crash leaves it parked and the next boot skips it.
    // Other starts leave the library in place, so a crash in another mod cannot
    // park the manager. Errors leave earlier file changes in place. Saved mod
    // copies and settings are never removed.
    Result begin(QString &error);

    // armed reports whether begin moved the library aside in this start.
    bool armed() const {
        return armed_;
    }

    // confirm restores the parked library after Home has stayed up, then
    // records this library and firmware as confirmed. Call it only when armed.
    // It refuses to replace a newly installed library or restore a replaced
    // parked file; that failure leaves the failsafe armed, and reinstalling the
    // package retries startup. A failure to write the record returns false
    // with the library already restored; the next start arms again.
    bool confirm(QString &error);

  private:
    bool initializeMarkers(QString &error);
    bool remove(QString &error);
    bool arm(QString &error);
    bool confirmedBefore();
    QByteArray record() const;
    QString self_, storage_, firmware_, root_, parked_, receipt_, confirmed_;
    QString hash_; // SHA-256 of the library at begin; empty if it could not be read.
    quint64 device_ = 0, inode_ = 0;
    bool armed_ = false;
};
} // namespace NickelModManager
