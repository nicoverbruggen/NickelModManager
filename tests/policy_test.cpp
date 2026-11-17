// Include the implementation so these checks drive the production hook
// wrappers without loading or modifying firmware in the test process.
#include "../src/updates/policy.cpp"
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>
#include <cstdlib>

using namespace NickelModManager;

class UpgradeCommand : public QObject {
    Q_OBJECT
};

class OptionalQT6UpgradeDialog : public QDialog {
    Q_OBJECT
};

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "Line %d: %s\n", __LINE__, #condition); std::abort(); \
} } while (false)

namespace {
std::atomic<int> downloads(0);
int rejected = 0, nativePrompts = 0, workflowPrompts = 0;
int resultType = -1, resetCalls = 0;
bool setupDone = true, stockSilent = false, majorOffer = false;
bool syncing = false, updateSync = false;
void *setup = reinterpret_cast<void *>(1);
QString releaseNotes;
QObject *activeCommand = nullptr;

bool completed(void *) { return setupDone; }
bool nativeSilent(void *) { return stockSilent; }
bool nativeMajor(const QObject *) { return majorOffer; }
bool nativeSyncing(const void *) { return syncing; }
bool nativeUpdateSync(void *) { return updateSync; }
void download(QObject *command) {
    CHECK(QThread::currentThread() == command->thread());
    ++downloads;
}
void reject(QObject *) { ++rejected; }
void accept(QObject *command) { upgrade(command); }
void abortCommand(QObject *) {}
void reset(void *) { updateSync = false; }
void startManual(void *manager) { syncReset(manager); updateSync = true; }
void prepareReset(void *, const QString &) { ++resetCalls; }
void request(QObject *command, int type, const QString &notes) {
    releaseNotes = notes;
    if (type == 1 && !silent(nullptr)) {
        ++nativePrompts;
    } else {
        accept(command);
    }
}
int resultUpgradeType(const void *result) { return *static_cast<const int *>(result); }
void request6(QObject *command, const void *result, const QString &notes) {
    request(command, resultUpgradeType(result), notes);
}
void queuedCancel(QObject *) {}
bool queuedIdle(QObject *) { return true; }
bool nativeShowMajor(void *) { return true; }
QString nativeAvailableMajor(void *) { return "major-url"; }
void nativeResult(QObject *, const void *value) { resultType = *static_cast<const int *>(value); }
void nativeResultCtor(void *value) { *static_cast<int *>(value) = 0; }
void nativeResultDtor(void *) {}
void nativeCheckCtor(QObject *, void *, QObject *, bool) {}
void nativeWorkflow(void *) { ++workflowPrompts; }
void nativeWorkflowNotes(void *, const QString &, const QString &, const QString &) { ++workflowPrompts; }
void nativeRejectWorkflow(void *) { ++rejected; }
void nativeAcceptWorkflow(void *) { accept(activeCommand); }
QObject *dialog(QWidget *parent) { return new QDialog(parent); }
void dialogText(QObject *, const QString &) {}

void wireNative() {
    setupInstance = &setup;
    setupCompleted = completed;
    realSilent = nativeSilent;
    isMajorUpgrade = nativeMajor;
    realRequest = request;
    realUpgrade = download;
    rejectUpgrade = reject;
    acceptUpgrade = accept;
    realAbort = abortCommand;
    realSyncReset = reset;
    realManualCheck = startManual;
    isSyncing = nativeSyncing;
    isUpdateSync = nativeUpdateSync;
    realResetPrepare = prepareReset;
    realShowMajor = nativeShowMajor;
    realAvailableMajor = nativeAvailableMajor;
    realCheckResult = nativeResult;
    resultCtor = nativeResultCtor;
    resultDtor = nativeResultDtor;
    realCheckCtor = nativeCheckCtor;
    realWorkflow = nativeWorkflow;
    realWorkflowNotes = nativeWorkflowNotes;
    rejectWorkflow = nativeRejectWorkflow;
    realAcceptWorkflow = nativeAcceptWorkflow;
    makeDialog = dialog;
    setTitle = dialogText;
    setText = dialogText;
    setAcceptText = dialogText;
    setRejectText = dialogText;
    ready.store(true);
}

void choose(bool yes, std::function<void()> before = {}) {
    auto *timer = new QTimer(qApp);
    QObject::connect(timer, &QTimer::timeout, qApp, [timer, yes, before] {
        auto *box = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!box) {
            return;
        }
        timer->stop();
        timer->deleteLater();
        QPointer<QDialog> guard(box);
        if (before) {
            before();
        }
        if (guard) {
            guard->done(yes ? QDialog::Accepted : QDialog::Rejected);
        }
    });
    timer->start(0);
}

void drain(QObject *command) {
    QElapsedTimer deadline;
    deadline.start();
    while (command->property(requestProperty).value<QObject *>()) {
        CHECK(deadline.elapsed() < 3000);
        qApp->processEvents();
    }
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void qt6Consent() {
    realRequest6 = request6;
    upgradeType6 = resultUpgradeType;
    realQueuedCancel = queuedCancel;
    realQueuedIdle = queuedIdle;
    resetStarted.store(false);
    setupDone = true;
    majorOffer = false;
    stockSilent = false;
    QObject manager;
    UpgradeCommand command;
    const int automatic = 2, optional = 1;
    int before = downloads;
    applyUpdatePolicy(UpdatePolicy::Stock);
    requestConfirmation6(&command, &automatic, "qt6 stock");
    CHECK(downloads == before + 1 && releaseNotes == "qt6 stock");

    applyUpdatePolicy(UpdatePolicy::AlwaysAsk);
    before = downloads;
    CHECK(queuedIdle6(&command) && queuedIdle6(&manager));
    choose(false, [&] { CHECK(!queuedIdle6(&command) && queuedIdle6(&manager)); });
    requestConfirmation6(&command, &automatic, "qt6 ask");
    drain(&command);
    CHECK(downloads == before && queuedIdle6(&command));
    choose(true);
    requestConfirmation6(&command, &automatic, "qt6 ask again");
    drain(&command);
    CHECK(downloads == before + 1);
    before = nativePrompts;
    requestConfirmation6(&command, &optional, "qt6 optional");
    CHECK(nativePrompts == before + 1 && releaseNotes == "qt6 optional");
    accept(&command);

    applyUpdatePolicy(UpdatePolicy::NoUpdates);
    before = downloads;
    requestConfirmation6(&command, &automatic, {});
    requestConfirmation6(&command, &optional, {});
    CHECK(downloads == before);
    manualCheck(&manager);
    choose(true);
    requestConfirmation6(&command, &automatic, {});
    drain(&command);
    CHECK(downloads == before + 1);
    requestConfirmation6(&command, &automatic, {});
    CHECK(downloads == before + 1);
    manualCheck(&manager);
    before = nativePrompts;
    requestConfirmation6(&command, &optional, {});
    CHECK(nativePrompts == before + 1);
    accept(&command);
    before = downloads;
    requestConfirmation6(&command, &optional, {});
    CHECK(downloads == before);

    applyUpdatePolicy(UpdatePolicy::AlwaysAsk);
    choose(true, [&] { queuedCancel6(&command); });
    upgrade(&command);
    drain(&command);
    CHECK(downloads == before);
    setupDone = false;
    applyUpdatePolicy(UpdatePolicy::NoUpdates);
    requestConfirmation6(&command, &automatic, {});
    CHECK(downloads == before + 1);
    setupDone = true;
    resetPrepare(nullptr, "qt6 reset");
    requestConfirmation6(&command, &automatic, {});
    CHECK(downloads == before + 2);
}

void persistence() {
    QTemporaryDir folder;
    CHECK(folder.isValid());
    QString error = "old error";
    CHECK(loadUpdatePolicy(folder.path(), error) == UpdatePolicy::Stock && error.isEmpty());
    for (auto policy : {UpdatePolicy::Stock, UpdatePolicy::AlwaysAsk, UpdatePolicy::NoUpdates}) {
        CHECK(saveUpdatePolicy(folder.path(), policy, error) && error.isEmpty());
        CHECK(loadUpdatePolicy(folder.path(), error) == policy && error.isEmpty());
    }
    CHECK(!saveUpdatePolicy(folder.path(), static_cast<UpdatePolicy>(99), error));
    CHECK(!error.isEmpty());
    CHECK(loadUpdatePolicy(folder.path(), error) == UpdatePolicy::NoUpdates);
    QFile file(QDir(folder.path()).filePath("update-policy"));
    for (const QByteArray &bad : {QByteArray(), QByteArray("unknown"), QByteArray("stock\0junk", 10), QByteArray(100, 'x')}) {
        CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        CHECK(file.write(bad) == bad.size());
        file.close();
        CHECK(loadUpdatePolicy(folder.path(), error) == UpdatePolicy::Stock && !error.isEmpty());
    }
    const QString blocked = QDir(folder.path()).filePath("not-a-directory");
    QFile blocker(blocked);
    CHECK(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    CHECK(!saveUpdatePolicy(blocked, UpdatePolicy::Stock, error) && !error.isEmpty());
}
} // namespace

#ifdef NMM_UPDATE_HOOKS
void checkUpdatePathInstructions() {
    // The Qt6 native prompt is the fallthrough of a forward BNE.
    CHECK(forwardCondition(0xd12f, 0xd100));
    CHECK(!forwardCondition(0xd02f, 0xd100)); // Reversed selector.
    CHECK(!forwardCondition(0xd42f, 0xd100)); // Different condition.
    CHECK(!forwardCondition(0xd1ff, 0xd100)); // Backwards branch.
    CHECK(forwardCondition(0xd063, 0xd000)); // isSilent false selects the prompt.
    CHECK(!forwardCondition(0xd163, 0xd000));
    CHECK(isReturningBranch(0xf880)); // BL
    CHECK(isReturningBranch(0xe800)); // BLX
    CHECK(!isReturningBranch(0xb880)); // B.W
    const uint16_t selector[] = {0xf1b8, 0x0f01, 0xd12f};
    const uint16_t changedType[] = {0xf1b8, 0x0f02, 0xd12f};
    CHECK(matchesThumb({selector, 3}, selector, 3));
    CHECK(!matchesThumb({changedType, 3}, selector, 3));
    CHECK(!matchesThumb({selector, 2}, selector, 3));

    // Either optional hook alone must trigger the complete workflow check.
    CHECK(!hasMajorWorkflow());
    realWorkflowNotes = [](void *, const QString &, const QString &, const QString &) {};
    CHECK(hasMajorWorkflow());
    realWorkflowNotes = nullptr;
    realAcceptWorkflow = [](void *) {};
    CHECK(hasMajorWorkflow());
    realAcceptWorkflow = nullptr;

    alignas(4) const uint16_t call[] = {0xf000, 0xe800};
    const uint16_t invalidCall[] = {0xf000, 0xe801};
    const uint16_t conditionalBranch[] = {0xf000, 0x8000};
    CHECK(thumbBranchTarget(call) == reinterpret_cast<uintptr_t>(call) + 4);
    CHECK(!thumbBranchTarget(invalidCall));
    CHECK(!thumbBranchTarget(conditionalBranch));
}
#endif

int main(int argc, char **argv) {
#ifdef NMM_UPDATE_HOOKS
    checkUpdatePathInstructions();
#endif
    QApplication app(argc, argv);
    persistence();
    CHECK(!updatePolicySupport().supported);
    CHECK(!initializeUpdatePolicy(UpdatePolicy::NoUpdates).supported);
    wireNative();
    QObject manager, command, check, majorWorkflow;
    activeCommand = &command;

    // Stock includes the silent optional path and all separate beta behavior.
    applyUpdatePolicy(UpdatePolicy::Stock);
    stockSilent = true;
    requestConfirmation(&command, 1, "stock notes");
    CHECK(downloads == 1 && nativePrompts == 0 && releaseNotes == "stock notes");
    CHECK(showMajor(nullptr) && availableMajor(nullptr) == "major-url");
    workflow(&majorWorkflow);
    CHECK(workflowPrompts == 1);

    // AlwaysAsk changes ordinary automatic and silent updates into consent,
    // while a normal optional prompt keeps its original release notes.
    applyUpdatePolicy(UpdatePolicy::AlwaysAsk);
    requestConfirmation(&command, 1, "ordinary notes");
    CHECK(nativePrompts == 1 && downloads == 1 && releaseNotes == "ordinary notes");
    accept(&command);
    CHECK(downloads == 2);
    for (int type : {2, 3, 4}) {
        const int before = downloads;
        choose(false);
        requestConfirmation(&command, type, "automatic");
        drain(&command);
        CHECK(downloads == before);
        choose(true);
        requestConfirmation(&command, type, "automatic");
        drain(&command);
        CHECK(downloads == before + 1);
    }
    majorOffer = true;
    const int beforeMajor = downloads;
    requestConfirmation(&command, 1, "beta");
    CHECK(downloads == beforeMajor + 1 && nativePrompts == 1);
    workflow(&majorWorkflow);
    CHECK(workflowPrompts == 2 && showMajor(nullptr));
    majorOffer = false;
    // A deferred beta request cannot make a later ordinary optional offer
    // use the stock silent shortcut and download without confirmation.
    realRequest = [](QObject *, int, const QString &) {};
    majorOffer = true;
    requestConfirmation(&command, 1, "deferred beta");
    majorOffer = false;
    realRequest = request;
    requestConfirmation(&command, 1, "ordinary after beta");
    CHECK(downloads == beforeMajor + 1 && nativePrompts == 2);
    abortUpgrade(&command);

    applyUpdatePolicy(UpdatePolicy::NoUpdates);
    int before = downloads;
    int prompts = nativePrompts;
    for (int type : {1, 2, 3, 4}) {
        requestConfirmation(&command, type, "blocked");
    }
    CHECK(downloads == before && nativePrompts == prompts);
    CHECK(!showMajor(nullptr) && availableMajor(nullptr).isEmpty());
    checkCtor(&check, nullptr, nullptr, true);
    int offered = 3;
    checkResult(&check, &offered);
    CHECK(resultType == 0);

    // A real manual check grants one offer, not permission to download.
    manualCheck(&manager);
    requestConfirmation(&command, 1, "manual");
    CHECK(nativePrompts == prompts + 1 && downloads == before);
    accept(&command);
    CHECK(downloads == before + 1);
    upgrade(&command);
    CHECK(downloads == before + 1);
    requestConfirmation(&command, 1, "second offer");
    CHECK(nativePrompts == prompts + 1);
    manualCheck(&manager);
    choose(false);
    requestConfirmation(&command, 3, "manual automatic");
    drain(&command);
    CHECK(downloads == before + 1);

    // No result, cancellation, a failed/busy manual check and the next sync
    // must never leave permission for an ordinary background update.
    manualCheck(&manager);
    syncReset(&manager);
    requestConfirmation(&command, 1, "background");
    CHECK(nativePrompts == prompts + 1);
    syncing = true;
    manualCheck(&manager);
    syncing = false;
    requestConfirmation(&command, 1, "busy manual check");
    CHECK(nativePrompts == prompts + 1);

    // The manual exception also permits one native beta workflow. Only its
    // actual acceptance can authorize the major upgrade command.
    manualCheck(&manager);
    CHECK(showMajor(nullptr));
    checkResult(&check, &offered);
    CHECK(resultType == offered);
    workflow(&majorWorkflow);
    CHECK(workflowPrompts == 3);
    majorOffer = true;
    workflowAccepted(&majorWorkflow);
    CHECK(downloads == before + 2);
    upgrade(&command);
    CHECK(downloads == before + 2);
    majorOffer = false;

    // The older beta dialog also requires a manual check and a real accepted
    // signal. Closing or destroying the dialog does not create approval.
    LegacyUpdateObserver legacyObserver;
    qApp->installEventFilter(&legacyObserver);
    OptionalQT6UpgradeDialog legacyDialog;
    manualCheck(&manager);
    legacyDialog.show();
    CHECK(!manualCheckPending());
    legacyDialog.accept();
    requestConfirmation(&command, 3, "legacy beta");
    CHECK(downloads == before + 3);
    requestConfirmation(&command, 3, "later offer");
    CHECK(downloads == before + 3);
    legacyDialog.show();
    qApp->processEvents();
    CHECK(!legacyDialog.isVisible());
    qApp->removeEventFilter(&legacyObserver);

    // Cancellation and policy changes invalidate an already queued Yes.
    applyUpdatePolicy(UpdatePolicy::AlwaysAsk);
    before = downloads;
    choose(true, [&] { abortUpgrade(&command); });
    upgrade(&command);
    drain(&command);
    CHECK(downloads == before);
    choose(true, [&] { upgrade(&command); });
    upgrade(&command);
    drain(&command);
    CHECK(downloads == before + 1);
    before = downloads;
    choose(true, [] { applyUpdatePolicy(UpdatePolicy::NoUpdates); });
    applyUpdatePolicy(UpdatePolicy::AlwaysAsk);
    upgrade(&command);
    drain(&command);
    CHECK(downloads == before);

    // Answers are delivered to an actual sync worker, never on the GUI thread.
    applyUpdatePolicy(UpdatePolicy::AlwaysAsk);
    QThread worker;
    auto *background = new QObject;
    background->moveToThread(&worker);
    QObject::connect(&worker, &QThread::finished, background, &QObject::deleteLater);
    QTimer dispatch;
    dispatch.setSingleShot(true);
    QObject::connect(&dispatch, &QTimer::timeout, background, [background] { upgrade(background); });
    worker.start();
    choose(true);
    dispatch.start(0);
    QElapsedTimer deadline;
    deadline.start();
    while (downloads == before) {
        CHECK(deadline.elapsed() < 3000);
        qApp->processEvents();
    }
    worker.quit();
    worker.wait();
    CHECK(downloads == before + 1);
    before = downloads;
    applyUpdatePolicy(UpdatePolicy::AlwaysAsk);
    choose(true, [] { delete QApplication::activeModalWidget(); });
    upgrade(&command);
    drain(&command);
    CHECK(downloads == before);

    // Every policy leaves setup and reset alone, including beta results.
    setupDone = false;
    for (auto policy : {UpdatePolicy::Stock, UpdatePolicy::AlwaysAsk, UpdatePolicy::NoUpdates}) {
        applyUpdatePolicy(policy);
        upgrade(&command);
        CHECK(showMajor(nullptr));
        checkResult(&check, &offered);
        CHECK(resultType == offered);
    }
    CHECK(downloads == before + 3);
    setupDone = true;
    resetPrepare(nullptr, "reset");
    CHECK(resetCalls == 1);
    upgrade(&command);
    CHECK(downloads == before + 4 && showMajor(nullptr));
    qt6Consent();
    std::puts("PASS: Qt5/Qt6 policy persistence, consent, beta behavior, manual grants, cancellation, setup and reset");
    return 0;
}

#include "policy_test.moc"
