#include "controller.h"
#include "compat.h"
#include "nickel.h"
#include "software.h"
#include "mods/metadata.h"
#include "ui/dialogs.h"
#include "ui/fonts.h"
#include "ui/manager.h"
#include "ui/mod_settings.h"
#include "updates/hook.h"
#include "updates/policy.h"
#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QSet>
#include <QScreen>
#include <memory>
#include <syslog.h>

namespace NickelModManager {
namespace {
// "12 Jun 2026", or empty when the library has no modification time.
QString builtDate(const NickelModManager::Mod &mod) {
    if (mod.built <= 0) {
        return QString();
    }
    const QDate day = QDateTime::fromMSecsSinceEpoch(mod.built * 1000).date();
    return QLocale::c().toString(day, "d MMM yyyy");
}

// "412 KB" or "1.2 MB" in binary units, or empty when the size is unknown.
QString fileSize(qint64 bytes) {
    if (bytes < 0) {
        return QString();
    }
    if (bytes < 1024 * 1024) {
        return QString::number(qMax<qint64>(1, (bytes + 512) / 1024)) + " KB";
    }
    return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MB";
}

// Controller connects the store and update hook to Nickel's widgets on the
// GUI thread. It borrows the process-lifetime store and owns the update hook.
// Each manager dialog deletes itself when closed.
class Controller final : public QObject {
  public:
    Controller(NickelModManager::ModStore *store, NickelModManager::StartReport report,
               const QString &self, const QString &storage, std::function<void()> homeReady)
        : QObject(qApp), store_(store), report_(std::move(report)), self_(self),
          adds_(QFileInfo(storage).absolutePath()), storage_(storage),
          homeReady_(std::move(homeReady)) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        // Only a full update on 5.x and 6.x needs the shutdown hook.
        hook_.reset(new NickelModManager::UpdateHook(QString(), storage, self));
#else
        Q_UNUSED(storage);
#endif
        QString policyError;
        policy_ = loadUpdatePolicy(storage_, policyError);
        if (!policyError.isEmpty()) {
            syslog(LOG_ERR, "NickelModManager: %s", qPrintable(policyError));
        }
        font_ = QApplication::font();
        font_.setFamily(NickelModManager::ui::systemFonts().sans);
        // The Elipsa 2E has a 1404-pixel short edge. Nickel can start with the
        // screen in landscape before switching to portrait, so use both axes.
        const auto *screen = QApplication::primaryScreen();
        density_ = screen && qMin(screen->size().width(), screen->size().height()) == 1404
                       ? 227
                       : 300;
        attachNickel(this, [this](const QFont &menuFont) { openManager(menuFont); },
                     [this](QWidget *main) { reachedHome(main); });
    }

  private:
    NickelModManager::ModStore *store_;
    NickelModManager::StartReport report_;
    // Mods that failed to load and that the user turned on again in this
    // process. Turning a mod on clears its parked flag and note in the store,
    // but the row stays under Failed to load until the next start.
    QSet<QString> retrying_;
    QHash<QString, SettingValue> settingsAtStart_;
    QSet<QString> pendingSettings_;
    QString self_;
    QString adds_;
    QString storage_;
    UpdatePolicy policy_ = UpdatePolicy::Stock;
    std::function<void()> homeReady_;
    // WA_DeleteOnClose destroys the dialog. QPointer clears the reference
    // automatically so a later opening cannot reuse a deleted window.
    QPointer<NickelModManager::ui::Manager> manager_;
    std::unique_ptr<NickelModManager::UpdateHook> hook_;
    QFont font_;
    int density_ = 300;

    QVector<NickelModManager::ui::Row> rows() const {
        // The UI gets values rather than references into the store, which can
        // reorder its mod list during a rescan or a state change.
        QVector<NickelModManager::ui::Row> result;
        const auto versions = NickelModManager::modVersions(adds_, store_->mods());
        const auto details = modDetails(adds_, store_->mods(), detectSoftwareFamily());
        for (const auto &mod : store_->mods()) {
            result.append({mod.file, mod.name, mod.note, fileSize(store_->librarySize(mod.file)),
                           builtDate(mod), versions.value(mod.file), mod.enabled, mod.loaded, failedToLoad(mod), details.value(mod.file)});
            if (!details.value(mod.file).name.isEmpty()) {
                result.last().name = details.value(mod.file).name;
            }
        }
        return result;
    }
    // A mod failed to load when its NickelHook failsafe holds the library, or
    // when the user turned on such a mod again in this process. The second case
    // keeps the row in its section until the next start shows whether it loads.
    bool failedToLoad(const NickelModManager::Mod &mod) const {
        return mod.parked || retrying_.contains(mod.file);
    }
    bool failedToLoad(const QString &file) const {
        for (const auto &mod : store_->mods()) {
            if (mod.file == file) {
                return failedToLoad(mod);
            }
        }
        return false;
    }
    // Add the installed library's filename and build date to the details page.
    // During the first seconds of an armed start the library is parked one
    // folder up, so read the date from there.
    void showManager() {
        QFileInfo library(self_);
        if (!library.exists()) {
            library = QFileInfo(QFileInfo(library.absolutePath()).absolutePath() + "/" +
                                library.fileName() + ".failsafe");
        }
        const QString built = QLocale::c().toString(library.lastModified().date(), "d MMM yyyy");
        const auto family = detectSoftwareFamily();
        const QString software = family == SoftwareFamily::Tolino ? "Tolino" : "Kobo";
        manager_->setManager(QFileInfo(self_).fileName(), built, software);
    }
    // Closed dialogs can report visible without repainting on 6.0.276679.
    // Create a new dialog for each opening, then rescan before showing it.
    void openManager(const QFont &menuFont) {
        font_.setFamily(NickelModManager::ui::systemFonts(menuFont.family()).sans);
        if (manager_ && !manager_->isVisible()) {
            manager_->deleteLater();
        }
        if (!manager_ || !manager_->isVisible()) {
            createManager();
        }

        store_->rescan();
        showManager();
        showRestore();
        showPolicy();
        manager_->setRows(rows(), store_->pendingChanges());
        manager_->showMaximized();
        manager_->raise();
        manager_->activateWindow();
    }

    // The dialog owns its widgets and deletes itself on close. Callbacks are
    // requests to the controller; the window never persists mod state itself.
    void createManager() {
        manager_ = new NickelModManager::ui::Manager(density_, font_);
        manager_->setAttribute(Qt::WA_DeleteOnClose);
        manager_->toggled = [this](QString file, bool enabled) { changeModEnabled(file, enabled); };
        manager_->forgotten = [this](QString file) { forgetMod(file); };
        manager_->detailsRequested = [this](QString file) { openModSettings(file); };
        manager_->setPendingSettings(pendingSettings_.size());
        manager_->preferencesSaved = [this](QString policy, bool restore) { savePreferences(policy, restore); };
        manager_->restart = [this] { requestRestart(); };
    }

    // Refresh the package only after a successful state change. Always refresh
    // the visible rows because a failed operation can still have changed files.
    void changeModEnabled(const QString &file, bool enabled) {
        QString error;
        // Check before the change, which clears the parked flag in the store.
        const bool retrying = enabled && failedToLoad(file);
        if (!store_->setEnabled(file, enabled, error)) {
            syslog(LOG_ERR, "NickelModManager: %s", qPrintable(error));
            manager_->showNotice("Change not saved", error.left(512));
        } else {
            syslog(LOG_INFO, "NickelModManager: %s turned %s", qPrintable(file),
                   enabled ? "on" : "off");
            if (retrying) {
                retrying_.insert(file);
            }
            refreshHook();
        }
        manager_->setRows(rows(), store_->pendingChanges());
    }

    void openModSettings(const QString &file) {
        store_->rescan();
        ModDetails details = modDetails(adds_, store_->mods(), detectSoftwareFamily()).value(file);
        QString name = modDisplayName(file);
        if (!details.name.isEmpty()) {
            name = details.name;
        }
        if (details.library.isEmpty()) {
            details.library = file;
        }
        SettingsSnapshot snapshot;
        QString unavailable;
        if (details.settings.isEmpty()) {
            unavailable = "This mod does not expose settings to NickelModManager.";
        } else {
            readSettings(details, snapshot, unavailable);
        }
        ui::showModSettings(manager_, manager_->context(), name, details, snapshot, unavailable,
                            [this, file, snapshot](const QHash<QString, bool> &edits, QString &error) {
            store_->rescan();
            const auto current = modDetails(adds_, store_->mods(), detectSoftwareFamily()).value(file);
            if (!saveSettings(snapshot, current, edits, error)) {
                return false;
            }
            for (const auto &setting : current.settings) {
                if (!setting.restart || !edits.contains(setting.key)) {
                    continue;
                }
                const QString identity = file + "/" + setting.key;
                if (!settingsAtStart_.contains(identity)) {
                    settingsAtStart_.insert(identity, snapshot.values.value(setting.key));
                }
                const auto original = settingsAtStart_.value(identity);
                if (original.valid && original.value == edits.value(setting.key)) {
                    pendingSettings_.remove(identity);
                } else {
                    pendingSettings_.insert(identity);
                }
            }
            manager_->setPendingSettings(pendingSettings_.size());
            return true;
        });
    }

    void showPolicy() {
        const auto support = updatePolicySupport();
        const QString policy = policy_ == UpdatePolicy::AlwaysAsk ? "ask" : policy_ == UpdatePolicy::NoUpdates ? "none" : "stock";
        manager_->setUpdatePolicy(policy, support.supported, support.supported ? QString() :
                                  "Update policies are unavailable because Nickel's update interface could not be verified. Updates keep their normal behavior.");
    }

    void savePreferences(const QString &policy, bool restore) {
        const UpdatePolicy selected = policy == "ask" ? UpdatePolicy::AlwaysAsk : policy == "none" ? UpdatePolicy::NoUpdates : UpdatePolicy::Stock;
        QString error;
        if (selected != policy_) {
            if (!updatePolicySupport().supported) {
                error = "Update policies are unavailable on this firmware.";
            } else if (saveUpdatePolicy(storage_, selected, error)) {
                policy_ = selected;
                applyUpdatePolicy(policy_);
            }
        }
        if (error.isEmpty() && hook_ && restore != hook_->enabled()) {
            hook_->setEnabled(restore, store_->keptOn(), error);
        }
        if (!error.isEmpty()) {
            manager_->showNotice("Preferences not fully saved", error, "The page will show the preferences that were saved.");
        }
        showRestore();
        showPolicy();
    }

    // Refresh the enabled-library package first, so a stale package cannot
    // reinstall a forgotten mod. A failure keeps the saved copy available.
    void forgetMod(const QString &file) {
        store_->rescan();
        QString error;
        if (hook_ && hook_->enabled() && !hook_->refresh(store_->keptOn(), error)) {
            manager_->showNotice("Mod not forgotten", error.left(512));
        } else if (!store_->forget(file, error)) {
            manager_->showNotice("Mod not forgotten", error.left(512));
        } else {
            retrying_.remove(file);
            syslog(LOG_INFO, "NickelModManager: forgot %s", qPrintable(file));
        }
        manager_->setRows(rows(), store_->pendingChanges());
    }

    void showRestore() {
        // Preference and support are separate: an enabled preference does
        // not permit restoration when the stock scripts are unknown.
        if (!manager_) {
            return;
        }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        using Restore = NickelModManager::ui::Manager::Restore;
        manager_->setRestore(hook_ && hook_->supported() ? Restore::Available : Restore::Unknown,
                             hook_ && hook_->enabled());
#else
        // Firmware 4.x updates keep the plugin folder.
        manager_->setRestore(NickelModManager::ui::Manager::Restore::NotNeeded, false);
#endif
    }
    // Rebuilds the restore package after a change, and puts back the
    // shutdown entries that a full firmware update removed.
    void refreshHook() {
        if (!hook_ || !hook_->enabled()) {
            return;
        }
        QString error;
        if (hook_->refresh(store_->keptOn(), error)) {
            syslog(LOG_INFO, "NickelModManager: restore package ready");
        } else {
            syslog(LOG_ERR, "NickelModManager: restore after updates: %s", qPrintable(error));
        }
    }
    // Show a notice first, so the screen does not simply freeze while the device
    // shuts down. E-ink needs a moment to draw it before the reboot begins.
    void requestRestart() {
        const bool inManager = manager_ && manager_->isVisible();
        QWidget *over = inManager ? manager_.data() : QApplication::activeWindow();
        const auto context = inManager ? manager_->context()
                                       : NickelModManager::ui::Context(density_, font_,
                                                                       QApplication::palette());
        const QString text = QString::fromUtf8("Restarting now\xe2\x80\xa6");
        QPointer<QWidget> notice(NickelModManager::ui::showStatus(over, context, text));
        NickelModManager::later(this, 500, [this, notice] {
            if (QProcess::startDetached("/sbin/reboot", {})) {
                return;
            }
            syslog(LOG_ERR, "NickelModManager: restart request failed");
            if (notice) {
                notice->close();
            }
            if (manager_) {
                manager_->showNotice("Restart failed",
                                     "Turn the eReader off and on again to apply the changes.");
            }
        });
    }
    void reachedHome(QWidget *main) {
        // Nickel integration reports Home once per process, after More is visible.
        syslog(LOG_INFO, "NickelModManager: Home is up");
        homeReady_();
        const auto support = initializeUpdatePolicy(policy_);
        if (!support.supported) {
            syslog(LOG_INFO, "NickelModManager: update policies unavailable: %s", qPrintable(support.reason));
        }
        // Not during start-up: the package takes a moment to compress.
        NickelModManager::later(this, 3000, [this] { refreshHook(); });
        // Nickel's own start-up dialogs come first.
        QPointer<QWidget> window(main);
        NickelModManager::later(this, 2000, [this, window] {
            if (window) {
                showRestoredMods(window);
            }
        });
    }
    // Offers the restart required to load libraries that were copied back
    // during this startup.
    void showRestoredMods(QWidget *main) {
        const NickelModManager::ui::Context context(density_, font_, main->palette());
        QStringList names;
        for (const auto &file : NickelModManager::constant(report_.restored)) {
            names << NickelModManager::modDisplayName(file);
        }
        if (names.isEmpty()) {
            return;
        }
        const bool one = names.size() == 1;
        if (NickelModManager::ui::choose(
                main, context, one ? "Mod put back" : "Mods put back",
                "The firmware update removed " + names.join(", ") +
                    (one ? ". It was on, so its copy is back."
                         : ". They were on, so their copies are back."),
                one ? "It loads after a restart." : "They load after a restart.",
                {{"Restart now", "nmmRestartNow"}, {"Later", "nmmLater"}}) == 0) {
            requestRestart();
        }
    }
};
} // namespace

void startController(ModStore *store, StartReport report, const QString &self,
                     const QString &storage, std::function<void()> homeReady) {
    new Controller(store, std::move(report), self, storage, std::move(homeReady));
}
} // namespace NickelModManager
