// The PLT lookup and hook chaining below are adapted from NickelHook.
// Copyright (c) 2020-2025 Patrick Gaskin
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include "policy.h"
#include "policy_p.h"

#include <QApplication>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSaveFile>
#include <QThread>
#include <QVariant>
#include <QVector>
#include <atomic>
#include <cstring>
#include <dlfcn.h>

#if defined(__linux__) && defined(__arm__)
#include <elf.h>
#include <link.h>
#include <sys/mman.h>
#include <unistd.h>
#define NMM_UPDATE_HOOKS 1
#endif

namespace NickelModManager {
namespace {
const char consentProperty[] = "nickelModManagerUpdateConsent";
const char requestProperty[] = "nickelModManagerUpdateRequest";
const char revisionProperty[] = "nickelModManagerUpdateRevision";
const char majorProperty[] = "nickelModManagerMajorCheck";

std::atomic<UpdatePolicy> currentPolicy(UpdatePolicy::Stock);
std::atomic<unsigned> revision(1);
std::atomic<bool> ready(false), resetStarted(false), manualAvailable(false);
std::atomic<unsigned> majorAcceptedRevision(0), legacyAcceptedRevision(0);
std::atomic<bool> stockMajorPending(false);
thread_local bool startingManualCheck = false;
UpdatePolicySupport support;

void **setupInstance = nullptr;
bool (*setupCompleted)(void *) = nullptr;
void (*realResetPrepare)(void *, const QString &) = nullptr;
void (*realRequest)(QObject *, int, const QString &) = nullptr;
void (*realRequest6)(QObject *, const void *, const QString &) = nullptr;
int (*upgradeType6)(const void *) = nullptr;
void (*realQueuedCancel)(QObject *) = nullptr;
bool (*realQueuedIdle)(QObject *) = nullptr;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void *setupObject6 = nullptr;
#endif
bool (*realSilent)(void *) = nullptr;
void (*realUpgrade)(QObject *) = nullptr;
void (*rejectUpgrade)(QObject *) = nullptr;
void (*acceptUpgrade)(QObject *) = nullptr;
void (*realAbort)(QObject *) = nullptr;
bool (*isMajorUpgrade)(const QObject *) = nullptr;
bool (*realShowMajor)(void *) = nullptr;
QString (*realAvailableMajor)(void *) = nullptr;
void (*realCheckResult)(QObject *, const void *) = nullptr;
void (*realCheckCtor)(QObject *, void *, QObject *, bool) = nullptr;
void (*resultCtor)(void *) = nullptr;
void (*resultDtor)(void *) = nullptr;
void (*realWorkflow)(void *) = nullptr;
void (*realWorkflowNotes)(void *, const QString &, const QString &, const QString &) = nullptr;
void (*rejectWorkflow)(void *) = nullptr;
void (*realAcceptWorkflow)(void *) = nullptr;
void (*realManualCheck)(void *) = nullptr;
void (*realSyncReset)(void *) = nullptr;
bool (*isSyncing)(const void *) = nullptr;
bool (*isUpdateSync)(void *) = nullptr;
std::atomic<void *> manualManager(nullptr);

QObject *(*makeDialog)(QWidget *) = nullptr;
void (*setTitle)(QObject *, const QString &) = nullptr;
void (*setText)(QObject *, const QString &) = nullptr;
void (*setAcceptText)(QObject *, const QString &) = nullptr;
void (*setRejectText)(QObject *, const QString &) = nullptr;

bool validPolicy(UpdatePolicy policy) {
    return policy == UpdatePolicy::Stock || policy == UpdatePolicy::AlwaysAsk ||
           policy == UpdatePolicy::NoUpdates;
}

QByteArray policyName(UpdatePolicy policy) {
    switch (policy) {
    case UpdatePolicy::AlwaysAsk: return "always-ask";
    case UpdatePolicy::NoUpdates: return "no-updates";
    default: return "stock";
    }
}

// Never construct Nickel's setup singleton in a sync worker. Its absence
// means setup is not ready, and all hooks must leave stock behavior alone.
bool enforcePolicy() {
    if (!ready.load() || resetStarted.load() || currentPolicy.load() == UpdatePolicy::Stock) {
        return false;
    }
    void *setup = setupInstance ? *setupInstance : nullptr;
    return setup && setupCompleted && setupCompleted(setup);
}

bool denyOffers() {
    return enforcePolicy() && currentPolicy.load() == UpdatePolicy::NoUpdates;
}

void invalidateConsent() {
    ++revision;
    manualAvailable.store(false);
    majorAcceptedRevision.store(0);
    legacyAcceptedRevision.store(0);
    stockMajorPending.store(false);
}

// The native update-sync flag prevents an unused manual grant from being
// spent by an ordinary sync. Reset and cancellation also revoke the grant.
bool manualCheckPending() {
    void *manager = manualManager.load();
    return manualAvailable.load() && manager && isUpdateSync && isUpdateSync(manager);
}

bool claimManualCheck() {
    return manualCheckPending() && manualAvailable.exchange(false);
}

Q_DECL_UNUSED void resetPrepare(void *self, const QString &message) {
    resetStarted.store(true);
    invalidateConsent();
    realResetPrepare(self, message);
}

Q_DECL_UNUSED void syncReset(void *self) {
    if (!startingManualCheck) {
        manualAvailable.store(false);
    }
    realSyncReset(self);
}

Q_DECL_UNUSED void manualCheck(void *self) {
    // A tap while another sync is active must not authorize that sync.
    const bool grant = enforcePolicy() && !isSyncing(self);
    invalidateConsent();
    manualManager = self;
    manualAvailable.store(grant);
    startingManualCheck = true;
    realManualCheck(self);
    startingManualCheck = false;
}

bool commandHasConsent(QObject *command) {
    return command->property(consentProperty).toBool() &&
           command->property(revisionProperty).toUInt() == revision.load();
}

void grantCommand(QObject *command) {
    command->setProperty(revisionProperty, revision.load());
    command->setProperty(consentProperty, true);
}

Q_DECL_UNUSED void requestConfirmation(QObject *self, int type, const QString &notes) {
    if (!enforcePolicy()) {
        realRequest(self, type, notes);
        return;
    }
    self->setProperty(consentProperty, false);
    self->setProperty("nickelModManagerManualOffer", QVariant());
    const bool major = isMajorUpgrade && isMajorUpgrade(self);
    stockMajorPending.store(major && currentPolicy.load() == UpdatePolicy::AlwaysAsk);
    if (major && currentPolicy.load() == UpdatePolicy::AlwaysAsk) {
        realRequest(self, type, notes);
        return;
    }
    if (major && denyOffers()) {
        // The separate workflow owns its native prompt and spends the manual
        // opportunity when it opens. Its acceptance signal grants download.
        if (manualCheckPending()) {
            realRequest(self, type, notes);
        } else {
            rejectUpgrade(self);
        }
        return;
    }
    if (denyOffers() && legacyAcceptedRevision.exchange(0) == revision.load()) {
        grantCommand(self);
        // The legacy beta dialog already recorded a deliberate acceptance.
        acceptUpgrade(self);
        return;
    }
    if (denyOffers() && !claimManualCheck()) {
        rejectUpgrade(self);
        return;
    }
    // Type 1 has a native prompt. A forced non-silent manager ensures that
    // its acceptance, rather than a silent shortcut, grants this consent.
    if (type == 1) {
        grantCommand(self);
    } else if (currentPolicy.load() == UpdatePolicy::NoUpdates) {
        self->setProperty("nickelModManagerManualOffer", revision.load());
    }
    realRequest(self, type, notes);
}

// Qt6 sends the result object instead of the enum. Read its type through
// Nickel's accessor; the private result layout is not shared with Qt5.
Q_DECL_UNUSED void requestConfirmation6(QObject *self, const void *result, const QString &notes) {
    if (!enforcePolicy()) {
        realRequest6(self, result, notes);
        return;
    }
    self->setProperty(consentProperty, false);
    self->setProperty("nickelModManagerManualOffer", QVariant());
    if (denyOffers() && !claimManualCheck()) {
        rejectUpgrade(self);
        return;
    }
    if (upgradeType6(result) == 1) {
        grantCommand(self);
    } else if (denyOffers()) {
        self->setProperty("nickelModManagerManualOffer", revision.load());
    }
    realRequest6(self, result, notes);
}

Q_DECL_UNUSED bool silent(void *self) {
    return enforcePolicy() && !stockMajorPending.load() ? false : realSilent(self);
}
} // namespace

// The older optional Qt6 offer is a QDialog, rather than the later workflow.
// Observe its real acceptance signal so a manual exception cannot authorize
// a download merely because a dialog was created or closed.
class LegacyUpdateObserver : public QObject {
  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (event->type() != QEvent::Show || !denyOffers() ||
            !object->inherits("OptionalQT6UpgradeDialog")) {
            return false;
        }
        auto *dialog = qobject_cast<QDialog *>(object);
        if (!dialog) {
            return false;
        }
        if (!claimManualCheck()) {
            QMetaObject::invokeMethod(dialog, "reject", Qt::QueuedConnection);
            return false;
        }
        const unsigned value = revision.load();
        QObject::connect(dialog, &QDialog::accepted, dialog, [value] {
            if (denyOffers() && value == revision.load()) {
                legacyAcceptedRevision.store(value);
            }
        });
        return false;
    }
};

void UpdateConsentUi::ask() {
    if (cancelled_) {
        deleteLater();
        return;
    }
    if (!enforcePolicy() || revision_ != revision.load()) {
        emit answered(false);
        deleteLater();
        return;
    }
    asking_ = true;
    QObject *object = makeDialog(QApplication::activeWindow());
    auto *box = qobject_cast<QDialog *>(object);
    if (!box) {
        if (object) {
            object->deleteLater();
        }
        asking_ = false;
        emit answered(false);
        deleteLater();
        return;
    }
    box->setAttribute(Qt::WA_DeleteOnClose, false);
    setTitle(object, tr("Software update"));
    setText(object, tr("Download and install this software update?"));
    setAcceptText(object, tr("Update"));
    setRejectText(object, tr("Not now"));
    dialog_ = box;
    const int result = box->exec();
    // A parent can destroy the dialog during the nested event loop.
    const bool accepted = dialog_ && result == QDialog::Accepted;
    if (dialog_) {
        dialog_->deleteLater();
    }
    dialog_ = nullptr;
    asking_ = false;
    emit answered(accepted);
    deleteLater();
}

void UpdateConsentUi::cancel() {
    cancelled_ = true;
    if (dialog_) {
        dialog_->reject();
    } else if (!asking_) {
        deleteLater();
    }
}

void UpdateConsentRequest::finish(bool accepted) {
    QObject *command = parent();
    if (command->property(requestProperty).value<QObject *>() != this) {
        deleteLater();
        return;
    }
    command->setProperty(requestProperty, QVariant());
    deleteLater();
    if (accepted && enforcePolicy() && revision_ == revision.load()) {
        grantCommand(command);
        acceptUpgrade(command);
    } else {
        rejectUpgrade(command);
    }
}

namespace {
Q_DECL_UNUSED void abortUpgrade(QObject *self) {
    self->setProperty(consentProperty, false);
    QObject *request = self->property(requestProperty).value<QObject *>();
    self->setProperty(requestProperty, QVariant());
    if (request) {
        request->deleteLater();
    }
    invalidateConsent();
    realAbort(self);
}

// Qt6 calls UpgradeCommand::cancel through its vtable, then calls the base
// through PLT. Cancel only this command's consent, leaving other sync work alone.
Q_DECL_UNUSED bool queuedIdle6(QObject *self) {
    // Native automatic acceptance clears UpgradeCommand's busy flag before
    // doUpgrade. Keep its queue waiting while our consent request is pending,
    // otherwise Qt6 destroys the command and closes its prompt immediately.
    if (self->inherits("UpgradeCommand") && self->property(requestProperty).value<QObject *>()) {
        return false;
    }
    return realQueuedIdle(self);
}

Q_DECL_UNUSED void queuedCancel6(QObject *self) {
    if (self->inherits("UpgradeCommand")) {
        self->setProperty(consentProperty, false);
        QObject *request = self->property(requestProperty).value<QObject *>();
        self->setProperty(requestProperty, QVariant());
        if (request) {
            request->deleteLater();
        }
        invalidateConsent();
    }
    realQueuedCancel(self);
}

Q_DECL_UNUSED void upgrade(QObject *self) {
    if (!enforcePolicy()) {
        realUpgrade(self);
        return;
    }
    const bool major = isMajorUpgrade && isMajorUpgrade(self);
    if (major && currentPolicy.load() == UpdatePolicy::AlwaysAsk) {
        stockMajorPending.store(false);
        realUpgrade(self);
        return;
    }
    if (major && denyOffers() && majorAcceptedRevision.exchange(0) == revision.load()) {
        realUpgrade(self);
        return;
    }
    if (commandHasConsent(self)) {
        self->setProperty(consentProperty, false);
        realUpgrade(self);
        return;
    }
    if (denyOffers() && self->property("nickelModManagerManualOffer").toUInt() != revision.load()) {
        rejectUpgrade(self);
        return;
    }
    if (self->property(requestProperty).value<QObject *>()) {
        return;
    }
    self->setProperty("nickelModManagerManualOffer", QVariant());
    if (!qApp) {
        rejectUpgrade(self);
        return;
    }
    auto *request = new UpdateConsentRequest(self, revision.load());
    self->setProperty(requestProperty, QVariant::fromValue<QObject *>(request));
    auto *ui = new UpdateConsentUi(revision.load());
    ui->moveToThread(qApp->thread());
    QObject::connect(ui, SIGNAL(answered(bool)), request, SLOT(finish(bool)), Qt::QueuedConnection);
    QObject::connect(request, SIGNAL(destroyed(QObject*)), ui, SLOT(cancel()), Qt::QueuedConnection);
    QMetaObject::invokeMethod(ui, "ask", Qt::QueuedConnection);
}

Q_DECL_UNUSED bool showMajor(void *self) {
    return denyOffers() && !manualCheckPending() ? false : realShowMajor(self);
}

Q_DECL_UNUSED QString availableMajor(void *self) {
    return denyOffers() && !manualCheckPending() ? QString() : realAvailableMajor(self);
}

Q_DECL_UNUSED void checkCtor(QObject *self, void *state, QObject *parent, bool major) {
    realCheckCtor(self, state, parent, major);
    self->setProperty(majorProperty, major);
}

Q_DECL_UNUSED void checkResult(QObject *self, const void *result) {
    if (!denyOffers() || !self->property(majorProperty).toBool() || manualCheckPending()) {
        realCheckResult(self, result);
        return;
    }
    // The verified Qt5 result is 16 bytes. Use its own constructor and
    // destructor in larger aligned storage instead of copying private fields.
    alignas(16) unsigned char empty[64];
    resultCtor(empty);
    realCheckResult(self, empty);
    resultDtor(empty);
}

bool allowWorkflow(void *self) {
    if (!denyOffers()) {
        return true;
    }
    if (static_cast<QObject *>(self)->property("nickelModManagerManualWorkflow").toUInt() == revision.load()) {
        return true;
    }
    if (!claimManualCheck()) {
        return false;
    }
    static_cast<QObject *>(self)->setProperty("nickelModManagerManualWorkflow", revision.load());
    return true;
}

Q_DECL_UNUSED void workflow(void *self) {
    if (!allowWorkflow(self)) {
        rejectWorkflow(self);
    } else {
        realWorkflow(self);
    }
}

Q_DECL_UNUSED void workflowNotes(void *self, const QString &a, const QString &b, const QString &c) {
    if (!allowWorkflow(self)) {
        rejectWorkflow(self);
    } else {
        realWorkflowNotes(self, a, b, c);
    }
}

Q_DECL_UNUSED void workflowAccepted(void *self) {
    if (denyOffers()) {
        if (static_cast<QObject *>(self)->property("nickelModManagerManualWorkflow").toUInt() != revision.load()) {
            rejectWorkflow(self);
            return;
        }
        static_cast<QObject *>(self)->setProperty("nickelModManagerManualWorkflow", QVariant());
        majorAcceptedRevision.store(revision.load());
    }
    realAcceptWorkflow(self);
}

#ifdef NMM_UPDATE_HOOKS
// Hooks change only libnickel's in-memory PLT slots. Resolve and validate the
// complete group before writing any slot. No firmware file or boot script is
// changed. This is restricted to matching ARM32 firmware interfaces.
struct Mapping {
    uintptr_t base;
    uintptr_t address;
    size_t length;
    unsigned flags;
    bool found = false;
};

int findMapping(dl_phdr_info *info, size_t, void *data) {
    auto &range = *static_cast<Mapping *>(data);
    if (range.base != info->dlpi_addr) {
        return 0;
    }
    for (unsigned i = 0; i < info->dlpi_phnum; ++i) {
        const auto &segment = info->dlpi_phdr[i];
        if (segment.p_type != PT_LOAD || (segment.p_flags & range.flags) != range.flags) {
            continue;
        }
        const uintptr_t start = info->dlpi_addr + segment.p_vaddr;
        if (range.address >= start && range.address - start < segment.p_memsz &&
            range.length <= segment.p_memsz - (range.address - start)) {
            range.found = true;
            return 1;
        }
    }
    return 0;
}

bool mapped(uintptr_t base, const void *address, size_t length, unsigned flags) {
    Mapping range{base, reinterpret_cast<uintptr_t>(address), length, flags, false};
    dl_iterate_phdr(findMapping, &range);
    return range.found;
}

bool executable(void *function) {
    Dl_info info;
    return function && dladdr(function, &info) &&
           mapped(reinterpret_cast<uintptr_t>(info.dli_fbase),
                  reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(function) & ~uintptr_t(1)),
                  1, PF_X);
}

struct Hook {
    const char *symbol;
    void *replacement;
    void *original;
    void **slot = nullptr;
};

// readSlot verifies every table access against libnickel's mapped segments.
// A symbol with no PLT relocation cannot be intercepted by this mechanism.
void **findSlot(link_map *map, const char *name) {
    Elf32_Rel *relocations = nullptr;
    Elf32_Sym *symbols = nullptr;
    const char *strings = nullptr;
    size_t bytes = 0, stringBytes = 0, entryBytes = 0;
    bool rel = false;
    for (const Elf32_Dyn *item = map->l_ld;
         mapped(map->l_addr, item, sizeof(*item), PF_R); ++item) {
        if (item->d_tag == DT_NULL) {
            break;
        }
        switch (item->d_tag) {
        case DT_JMPREL: relocations = reinterpret_cast<Elf32_Rel *>(item->d_un.d_ptr); break;
        case DT_PLTRELSZ: bytes = item->d_un.d_val; break;
        case DT_PLTREL: rel = item->d_un.d_val == DT_REL; break;
        case DT_RELENT: entryBytes = item->d_un.d_val; break;
        case DT_SYMTAB: symbols = reinterpret_cast<Elf32_Sym *>(item->d_un.d_ptr); break;
        case DT_STRTAB: strings = reinterpret_cast<const char *>(item->d_un.d_ptr); break;
        case DT_STRSZ: stringBytes = item->d_un.d_val; break;
        default: break;
        }
    }
    if (!rel || entryBytes != sizeof(Elf32_Rel) || !bytes || bytes % entryBytes ||
        !symbols || !mapped(map->l_addr, relocations, bytes, PF_R) ||
        !strings || !stringBytes || !mapped(map->l_addr, strings, stringBytes, PF_R)) {
        return nullptr;
    }
    for (size_t i = 0; i < bytes / entryBytes; ++i) {
        const auto &relocation = relocations[i];
        const size_t index = ELF32_R_SYM(relocation.r_info);
        if (index > (UINTPTR_MAX - reinterpret_cast<uintptr_t>(symbols)) / sizeof(*symbols)) {
            return nullptr;
        }
        const auto *symbol = reinterpret_cast<const Elf32_Sym *>(
            reinterpret_cast<uintptr_t>(symbols) + index * sizeof(*symbols));
        if (!mapped(map->l_addr, symbol, sizeof(*symbol), PF_R) || symbol->st_name >= stringBytes ||
            !std::memchr(strings + symbol->st_name, 0, stringBytes - symbol->st_name)) {
            return nullptr;
        }
        if (std::strcmp(strings + symbol->st_name, name)) {
            continue;
        }
        if (ELF32_R_TYPE(relocation.r_info) != R_ARM_JUMP_SLOT ||
            ELF32_ST_TYPE(symbol->st_info) != STT_FUNC ||
            ELF32_ST_BIND(symbol->st_info) != STB_GLOBAL ||
            relocation.r_offset > UINTPTR_MAX - map->l_addr) {
            return nullptr;
        }
        auto **slot = reinterpret_cast<void **>(map->l_addr + relocation.r_offset);
        if (reinterpret_cast<uintptr_t>(slot) % sizeof(void *) ||
            !mapped(map->l_addr, slot, sizeof(*slot), PF_R | PF_W)) {
            return nullptr;
        }
        return slot;
    }
    return nullptr;
}

bool readSlot(void *library, link_map *map, Hook &hook) {
    auto **slot = findSlot(map, hook.symbol);
    if (!slot) {
        return false;
    }
    void *original = dlsym(library, hook.symbol);
    Dl_info previous;
    if (*slot != original && dladdr(*slot, &previous)) {
        original = previous.dli_fbase == reinterpret_cast<void *>(map->l_addr)
            ? dlsym(RTLD_DEFAULT, hook.symbol) : *slot;
    }
    if (original == hook.replacement || !executable(original) || !executable(hook.replacement)) {
        return false;
    }
    hook.original = original;
    hook.slot = slot;
    return true;
}

template <typename Function>
bool lookup(void *library, const char *symbol, Function &function) {
    void *address = dlsym(library, symbol);
    function = reinterpret_cast<Function>(address);
    return executable(address);
}

template <typename Function>
bool prepareHook(void *library, link_map *map, QVector<Hook> &hooks, const char *symbol,
                 Function replacement, Function &original, QString &error, bool optional = false) {
    if (optional && !dlsym(library, symbol)) {
        return true;
    }
    Hook hook{symbol, reinterpret_cast<void *>(replacement), nullptr, nullptr};
    if (!readSlot(library, map, hook)) {
        error = QString("Cannot intercept %1; updates remain stock.").arg(symbol);
        return false;
    }
    original = reinterpret_cast<Function>(hook.original);
    hooks.append(hook);
    return true;
}

// Read only the exported Thumb function's mapped symbol range. The calls we
// need go through the ARM PLT, sometimes with a Thumb veneer in front of it.
struct ThumbFunction {
    const uint16_t *code = nullptr;
    size_t count = 0;
};

ThumbFunction thumbFunction(uintptr_t base, void *function) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(function);
    Dl_info info;
    void *entry = nullptr;
    if (!(address & 1) || !dladdr1(function, &info, &entry, RTLD_DL_SYMENT) || !entry ||
        reinterpret_cast<uintptr_t>(info.dli_fbase) != base ||
        (reinterpret_cast<uintptr_t>(info.dli_saddr) & ~uintptr_t(1)) != address - 1) {
        return {};
    }
    const size_t size = static_cast<const Elf32_Sym *>(entry)->st_size;
    auto *code = reinterpret_cast<const uint16_t *>(address - 1);
    if (!size || size > 16384 || (size & 1) || !mapped(base, code, size, PF_R | PF_X)) {
        return {};
    }
    return {code, size / 2};
}

uint32_t armImmediate(uint32_t instruction) {
    const uint32_t value = instruction & 0xff;
    const unsigned rotate = ((instruction >> 8) & 15) * 2;
    return rotate ? (value >> rotate) | (value << (32 - rotate)) : value;
}

void **pltSlot(uintptr_t base, uintptr_t target) {
    if (target & 3 || !mapped(base, reinterpret_cast<void *>(target), 12, PF_R | PF_X)) {
        return nullptr;
    }
    auto *arm = reinterpret_cast<const uint32_t *>(target);
    if ((arm[0] & 0xffff) == 0x4778) { // bx pc followed by linker padding
        target += 4;
        if (!mapped(base, reinterpret_cast<void *>(target), 12, PF_R | PF_X)) {
            return nullptr;
        }
        ++arm;
    }
    if ((arm[0] & 0xfffff000) != 0xe28fc000 ||
        (arm[1] & 0xfffff000) != 0xe28cc000 ||
        (arm[2] & 0xfffff000) != 0xe5bcf000) {
        return nullptr;
    }
    return reinterpret_cast<void **>(target + 8 + armImmediate(arm[0]) +
                                    armImmediate(arm[1]) + (arm[2] & 0xfff));
}

uintptr_t thumbBranchTarget(const uint16_t *instruction) {
    const uint16_t first = instruction[0], second = instruction[1];
    const unsigned form = second & 0xd000;
    if ((first & 0xf800) != 0xf000 ||
        (form != 0x9000 && form != 0xc000 && form != 0xd000) ||
        (form == 0xc000 && (second & 1))) {
        return 0;
    }
    const uint32_t sign = (first >> 10) & 1;
    const uint32_t i1 = 1 ^ (((second >> 13) & 1) ^ sign);
    const uint32_t i2 = 1 ^ (((second >> 11) & 1) ^ sign);
    const uint32_t encoded = (sign << 24) | (i1 << 23) | (i2 << 22) |
                             ((first & 0x3ff) << 12) | ((second & 0x7ff) << 1);
    const int32_t offset = int32_t(encoded) - (sign ? (1 << 25) : 0);
    const uintptr_t pc = reinterpret_cast<uintptr_t>(instruction) + 4;
    return (form == 0xc000 ? pc & ~uintptr_t(3) : pc) + offset;
}

bool isReturningBranch(uint16_t secondHalfword) {
    return (secondHalfword & 0xc000) == 0xc000; // BL and BLX, never B.W.
}

bool callsReturningSlot(uintptr_t base, const uint16_t *instruction, void **slot) {
    return slot && isReturningBranch(instruction[1]) &&
        pltSlot(base, thumbBranchTarget(instruction)) == slot;
}

bool callsSlot(uintptr_t base, ThumbFunction from, void **slot) {
    if (!slot) {
        return false;
    }
    for (size_t i = 0; i + 1 < from.count;) {
        const bool wide = from.code[i] >= 0xe800;
        if (wide && pltSlot(base, thumbBranchTarget(from.code + i)) == slot) {
            return true;
        }
        i += wide ? 2 : 1;
    }
    return false;
}

bool matchesThumb(ThumbFunction function, const uint16_t *shape, size_t count) {
    if (function.count < count) {
        return false;
    }
    for (size_t i = 0; i < count; ++i) {
        if (shape[i] && function.code[i] != shape[i]) {
            return false;
        }
    }
    return true;
}

Q_DECL_UNUSED bool forwardCondition(uint16_t branch, uint16_t condition) {
    return (branch & 0xff80) == condition;
}

bool hasOptionalSelector(link_map *map, ThumbFunction manager) {
    const auto base = map->l_addr;
    auto **silentSlot = findSlot(map, "_ZN14UpgradeManager8isSilentEv");
    auto **acceptSlot = findSlot(map, "_ZN15UpgradeDelegate27upgradeConfirmationAcceptedEv");
    auto **dialogSlot = findSlot(map, "_ZN25ConfirmationDialogFactory21getConfirmationDialogEP7QWidget");
    if (!silentSlot || !acceptSlot || !dialogSlot) {
        return false;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // The accessor returns the offer type in r0, which is copied to r8 across
    // URL processing. Type 1 falls through to the native silent check. The
    // other types branch to the delegate's automatic acceptance.
    static const uint16_t shape[] = {0, 0, 0x4631, 0x4680, 0x4620, 0, 0, 0xf04f, 0x62fe,
        0x4621, 0x4648, 0, 0, 0x4620, 0, 0, 0xf1b8, 0x0f01};
    auto **typeSlot = findSlot(map, "_ZNK18UpgradeCheckResult14getUpgradeTypeEv");
    if (!typeSlot) {
        return false;
    }
    static const uint16_t prompt[] = {0x9b1d, 0xb16b, 0xf3bf, 0x8f5b, 0xe853, 0x2f00,
        0x1e51, 0xe843, 0x1000, 0x2800, 0xd1f8, 0x2a01, 0xf3bf, 0x8f5b, 0xf000, 0x822c, 0x2000, 0, 0};
    constexpr size_t branch = sizeof(shape) / sizeof(*shape);
    constexpr size_t factory = sizeof(prompt) / sizeof(*prompt) - 2;
    for (size_t i = 0; i + branch + 1 < qMin<size_t>(manager.count, 64);) {
        const auto *code = manager.code + i;
        if (matchesThumb({code, manager.count - i}, shape, branch) &&
            callsReturningSlot(base, code, typeSlot) && forwardCondition(code[branch], 0xd100)) {
            const size_t automatic = branch + 2 + (code[branch] & 0x7f);
            if (i + automatic + 2 >= manager.count || code[automatic] != 0x4628 ||
                !callsReturningSlot(base, code + automatic + 1, acceptSlot)) {
                return false;
            }
            // A false silent result must select the dialog. The short
            // cleanup block releases a temporary string before creating it.
            for (size_t j = branch + 1; j + 3 < automatic;) {
                if (callsReturningSlot(base, code + j, silentSlot) && code[j + 2] == 0x2800 &&
                    forwardCondition(code[j + 3], 0xd000)) {
                    const size_t target = j + 5 + (code[j + 3] & 0x7f);
                    if (i + target < manager.count &&
                        matchesThumb({code + target, manager.count - i - target}, prompt, factory + 2) &&
                        callsReturningSlot(base, code + target + factory, dialogSlot)) {
                        return true;
                    }
                }
                j += code[j] >= 0xe800 ? 2 : 1;
            }
            return false;
        }
        i += *code >= 0xe800 ? 2 : 1;
    }
    return false;
#else
    // Qt5 saves the enum argument in r4. Its equality branch enters the
    // silent check, whereas the fallthrough calls automatic acceptance.
    static const uint16_t shape[] = {0xe92d, 0x4ff0, 0xb099, 0x460c, 0x4681,
        0xaf06, 0, 0x4611, 0, 0, 0, 0x2c01, 0x447d, 0xd006, 0x4648, 0, 0,
        0x374c, 0x46bd, 0xe8bd, 0x8ff0, 0, 0, 0x4682, 0x2800, 0xd1f3, 0, 0};
    return matchesThumb(manager, shape, sizeof(shape) / sizeof(*shape)) &&
        (manager.code[6] == 0x3008 || manager.code[6] == 0x300c) &&
        callsReturningSlot(base, manager.code + 15, acceptSlot) &&
        callsReturningSlot(base, manager.code + 21, silentSlot) &&
        callsReturningSlot(base, manager.code + 26, dialogSlot);
#endif
}

Q_DECL_UNUSED bool legacyResultLayout(link_map *map) {
    const auto base = map->l_addr;
    const auto ctor = thumbFunction(base, reinterpret_cast<void *>(resultCtor));
    const auto dtor = thumbFunction(base, reinterpret_cast<void *>(resultDtor));
    // The native default result consists of the selector, two URLs and a
    // string. Check both sides of its lifetime before using our own storage.
    static const uint16_t construct[] = {
        0xb5f8, 0x1d06, 0x4d0b, 0x4604, 0xaf00, 0x4630, 0, 0,
        0xf104, 0x0008, 0x447d, 0, 0, 0x4b07, 0x2200, 0x4620,
        0x58eb, 0x6022, 0x60e3, 0xbdf8
    };
    static const uint16_t destroy[] = {
        0xb598, 0x4604, 0x68c0, 0xaf00, 0x6803, 0xb963, 0x2204, 0x2102, 0, 0,
        0xf104, 0x0008, 0, 0, 0x1d20, 0, 0, 0x4620, 0xbd98, 0x3301, 0xd0f4,
        0xe850, 0x3f00, 0xf1a3, 0x0301, 0xe840, 0x3200, 0xf092, 0x0f00, 0xd1f6,
        0x2b00, 0xd1e9, 0x68e0, 0xe7e3
    };
    const auto calls = [=](ThumbFunction function, size_t index, const char *symbol) {
        auto **slot = findSlot(map, symbol);
        return callsReturningSlot(base, function.code + index, slot);
    };
    return matchesThumb(ctor, construct, sizeof(construct) / sizeof(*construct)) &&
        matchesThumb(dtor, destroy, sizeof(destroy) / sizeof(*destroy)) &&
        calls(ctor, 6, "_ZN4QUrlC1Ev") && calls(ctor, 11, "_ZN4QUrlC1Ev") &&
        calls(dtor, 8, "_ZN10QArrayData10deallocateEPS_jj") &&
        calls(dtor, 12, "_ZN4QUrlD1Ev") && calls(dtor, 15, "_ZN4QUrlD1Ev");
}

bool validateUpdatePath(void *library, link_map *map, QString &error) {
    const auto base = map->l_addr;
    const auto code = [=](const char *symbol) { return thumbFunction(base, dlsym(library, symbol)); };
    const auto calls = [&](const char *from, const char *to) {
        if (callsSlot(base, code(from), findSlot(map, to))) {
            return true;
        }
        error = QString("Nickel's update call from %1 to %2 did not match.").arg(from, to);
        return false;
    };
    constexpr auto download = "_ZN14UpgradeCommand9doUpgradeEv";
    constexpr auto notes = "_ZN14UpgradeCommand17onGotReleaseNotesERK7QString";
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    constexpr auto request = "_ZN14UpgradeCommand26requestUpgradeConfirmationERK18UpgradeCheckResultRK7QString";
    constexpr auto manager = "_ZN14UpgradeManager28onRequestUpgradeConfirmationERK18UpgradeCheckResultRK7QString";
    if (!calls("_ZN14UpgradeCommand6cancelEv", "_ZN17QueuedSyncCommand6cancelEv") ||
        !calls("_ZN14UpgradeCommand6isIdleEv", "_ZN17QueuedSyncCommand6isIdleEv")) {
        return false;
    }
#else
    constexpr auto request = "_ZN14UpgradeCommand26requestUpgradeConfirmationE11UpgradeTypeRK7QString";
    constexpr auto manager = "_ZN14UpgradeManager26requestUpgradeConfirmationE11UpgradeTypeRK7QString";
    if (realCheckCtor && !legacyResultLayout(map)) {
        error = "Nickel's legacy update result layout did not match.";
        return false;
    }
#endif
    if (!hasOptionalSelector(map, code(manager))) {
        error = "Nickel's optional-update selector did not match.";
        return false;
    }
    return calls(notes, request) && calls(notes, download) &&
        calls("_ZN14UpgradeCommand29onUpgradeConfirmationAcceptedEv", download) &&
        calls("_ZN37N3SettingsDeviceInformationController12onUpdateSyncEv", "_ZN13N3SyncManager10updateSyncEv");
}

Q_DECL_UNUSED bool hasMajorWorkflow() {
    return realAvailableMajor || realCheckCtor || realWorkflow || realWorkflowNotes || realAcceptWorkflow;
}

bool installHooks(QString &error) {
    void *library = dlopen("libnickel.so.1.0.0", RTLD_LAZY | RTLD_NOLOAD);
    if (!library) {
        error = "Nickel's library is not loaded.";
        return false;
    }
    link_map *map = nullptr;
    if (dlinfo(library, RTLD_DI_LINKMAP, &map) || !map) {
        dlclose(library);
        error = "Cannot inspect Nickel's loaded library.";
        return false;
    }
    QVector<Hook> hooks;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void *(*sharedSetup)() = nullptr;
    const bool foundSetup = lookup(library, "_ZN24N3PostFTEWorkflowManager14sharedInstanceEv", sharedSetup);
#else
    setupInstance = static_cast<void **>(dlsym(library, "_ZN24N3PostFTEWorkflowManager9_INSTANCEE"));
    const bool foundSetup = setupInstance && mapped(map->l_addr, setupInstance, sizeof(*setupInstance), PF_R);
#endif
    const bool found = foundSetup &&
        lookup(library, "_ZN24N3PostFTEWorkflowManager19isWorkflowCompletedEv", setupCompleted) &&
        lookup(library, "_ZN25ConfirmationDialogFactory21getConfirmationDialogEP7QWidget", makeDialog) &&
        lookup(library, "_ZN18ConfirmationDialog8setTitleERK7QString", setTitle) &&
        lookup(library, "_ZN18ConfirmationDialog7setTextERK7QString", setText) &&
        lookup(library, "_ZN18ConfirmationDialog19setAcceptButtonTextERK7QString", setAcceptText) &&
        lookup(library, "_ZN18ConfirmationDialog19setRejectButtonTextERK7QString", setRejectText) &&
        lookup(library, "_ZN14UpgradeCommand29onUpgradeConfirmationAcceptedEv", acceptUpgrade) &&
        lookup(library, "_ZN14UpgradeCommand29onUpgradeConfirmationRejectedEv", rejectUpgrade) &&
        lookup(library, "_ZNK15SyncManagerBase9isSyncingEv", isSyncing) &&
        lookup(library, "_ZN13N3SyncManager12isUpdateSyncEv", isUpdateSync);
    if (!found) {
        dlclose(library);
        error = "Required update or setup functions are unavailable; updates remain stock.";
        return false;
    }
#define PREPARE(symbol, replacement, original, optional) \
    prepareHook(library, map, hooks, symbol, replacement, original, error, optional)
    bool complete =
        PREPARE("_ZN24AccountSignOutController7prepareERK7QString", resetPrepare, realResetPrepare, false) &&
        PREPARE("_ZN14UpgradeManager8isSilentEv", silent, realSilent, false) &&
        PREPARE("_ZN14UpgradeCommand9doUpgradeEv", upgrade, realUpgrade, false) &&
        PREPARE("_ZN13N3SyncManager10updateSyncEv", manualCheck, realManualCheck, false) &&
        PREPARE("_ZN13N3SyncManager5resetEv", syncReset, realSyncReset, false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    complete = complete &&
        lookup(library, "_ZNK18UpgradeCheckResult14getUpgradeTypeEv", upgradeType6) &&
        PREPARE("_ZN14UpgradeCommand26requestUpgradeConfirmationERK18UpgradeCheckResultRK7QString", requestConfirmation6, realRequest6, false) &&
        PREPARE("_ZN17QueuedSyncCommand6cancelEv", queuedCancel6, realQueuedCancel, false) &&
        PREPARE("_ZN17QueuedSyncCommand6isIdleEv", queuedIdle6, realQueuedIdle, false);
#else
    complete = complete &&
        PREPARE("_ZN14UpgradeCommand26requestUpgradeConfirmationE11UpgradeTypeRK7QString", requestConfirmation, realRequest, false) &&
        PREPARE("_ZN14UpgradeCommand33abortUpgradeWithoutCancellingSyncEv", abortUpgrade, realAbort, false) &&
        PREPARE("_ZN23OneStoreServiceSettings38getShowOptionalQT6AccessibilityUpgradeEv", showMajor, realShowMajor, true) &&
        PREPARE("_ZN19ApplicationSettings19availableQt6UpgradeEv", availableMajor, realAvailableMajor, true) &&
        PREPARE("_ZN19UpgradeCheckCommand13upgradeResultERK18UpgradeCheckResult", checkResult, realCheckResult, false) &&
        PREPARE("_ZN19UpgradeCheckCommandC1EP9SyncStateP7QObjectb", checkCtor, realCheckCtor, true) &&
        PREPARE("_ZN25Qt6UpgradeWorkflowManager13startWorkflowEv", workflow, realWorkflow, true) &&
        PREPARE("_ZN25Qt6UpgradeWorkflowManager13startWorkflowERK7QStringS2_S2_", workflowNotes, realWorkflowNotes, true) &&
        PREPARE("_ZN25Qt6UpgradeWorkflowManager27upgradeConfirmationAcceptedEv", workflowAccepted, realAcceptWorkflow, true);
    if (hasMajorWorkflow()) {
        complete = complete && realAvailableMajor && realCheckCtor && realWorkflow && realWorkflowNotes && realAcceptWorkflow &&
            lookup(library, "_ZN18UpgradeCheckResultC1Ev", resultCtor) &&
            lookup(library, "_ZN18UpgradeCheckResultD1Ev", resultDtor) &&
            lookup(library, "_ZNK14UpgradeCommand17isQt5ToQt6UpgradeEv", isMajorUpgrade) &&
            lookup(library, "_ZN25Qt6UpgradeWorkflowManager27upgradeConfirmationRejectedEv", rejectWorkflow);
    }
    if (dlsym(library, "_ZN24OptionalQT6UpgradeDialog16staticMetaObjectE") && !realShowMajor) {
        complete = false;
    }
#endif
#undef PREPARE
    if (!complete) {
        dlclose(library);
        if (error.isEmpty()) {
            error = "The separate upgrade workflow is unsupported; updates remain stock.";
        }
        return false;
    }
    if (!validateUpdatePath(library, map, error)) {
        dlclose(library);
        return false;
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    // Resolve the singleton on the GUI thread after validating all interfaces.
    setupObject6 = sharedSetup();
    if (!setupObject6) {
        dlclose(library);
        error = "Nickel's setup state is unavailable; updates remain stock.";
        return false;
    }
    setupInstance = &setupObject6;
#endif
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0 || (pageSize & (pageSize - 1))) {
        dlclose(library);
        error = "Cannot determine memory page size; updates remain stock.";
        return false;
    }
    // Perform all fallible work before replacing the first function. A page
    // can remain writable after a failed protection change, but no hook runs.
    for (const auto &hook : hooks) {
        void *page = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(hook.slot) &
                                              ~uintptr_t(pageSize - 1));
        if (mprotect(page, size_t(pageSize), PROT_READ | PROT_WRITE)) {
            dlclose(library);
            error = "Cannot write Nickel's function table; updates remain stock.";
            return false;
        }
    }
    for (const auto &hook : hooks) {
        *hook.slot = hook.replacement;
    }
    // Keep the original functions loaded for the process lifetime.
    ready.store(true);
    auto *observer = new LegacyUpdateObserver;
    observer->setParent(qApp);
    qApp->installEventFilter(observer);
    return true;
}
#endif
} // namespace

UpdatePolicy loadUpdatePolicy(const QString &storage, QString &error) {
    error.clear();
    QFile file(QDir(storage).filePath("update-policy"));
    if (!file.exists()) {
        return UpdatePolicy::Stock;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("Cannot read update policy: %1").arg(file.errorString());
        return UpdatePolicy::Stock;
    }
    const QByteArray bytes = file.read(65);
    if (file.error() != QFile::NoError || !file.atEnd() || bytes.size() > 64) {
        error = "Cannot read update policy or the file is too large.";
        return UpdatePolicy::Stock;
    }
    for (UpdatePolicy policy : {UpdatePolicy::Stock, UpdatePolicy::AlwaysAsk, UpdatePolicy::NoUpdates}) {
        if (bytes == policyName(policy) + '\n' || bytes == policyName(policy)) {
            return policy;
        }
    }
    error = "The saved update policy is invalid; using stock updates.";
    return UpdatePolicy::Stock;
}

bool saveUpdatePolicy(const QString &storage, UpdatePolicy policy, QString &error) {
    error.clear();
    if (!validPolicy(policy)) {
        error = "Invalid update policy.";
        return false;
    }
    if (!QDir().mkpath(storage)) {
        error = "Cannot create the update-policy folder.";
        return false;
    }
    QSaveFile file(QDir(storage).filePath("update-policy"));
    file.setDirectWriteFallback(false);
    const QByteArray bytes = policyName(policy) + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error = QString("Cannot save update policy: %1").arg(file.errorString());
        return false;
    }
    return true;
}

UpdatePolicySupport initializeUpdatePolicy(UpdatePolicy policy) {
    if (!qApp || QThread::currentThread() != qApp->thread()) {
        return {false, "Update policies require Nickel's GUI thread."};
    }
    if (QCoreApplication::arguments().isEmpty() ||
        QFileInfo(QCoreApplication::arguments().first()).fileName() != "nickel") {
        support = {false, "Update policies are only available inside Nickel."};
        return support;
    }
    if (ready.load()) {
        applyUpdatePolicy(policy);
        return support;
    }
#ifdef NMM_UPDATE_HOOKS
    QString error;
    if (!installHooks(error)) {
        support = {false, error};
        return support;
    }
    support = {true, QString()};
    applyUpdatePolicy(policy);
#else
    Q_UNUSED(policy);
    support = {false, "Update policies have not been verified for this Qt or processor version."};
#endif
    return support;
}

UpdatePolicySupport updatePolicySupport() {
    if (support.reason.isEmpty() && !support.supported) {
        return {false, "Update policies have not been initialized."};
    }
    return support;
}

void applyUpdatePolicy(UpdatePolicy policy) {
    if (!validPolicy(policy)) {
        policy = UpdatePolicy::Stock;
    }
    if (currentPolicy.exchange(policy) != policy) {
        invalidateConsent();
    }
}
} // namespace NickelModManager
