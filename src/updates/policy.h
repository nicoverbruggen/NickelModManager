#pragma once

#include <QString>

namespace NickelModManager {
enum class UpdatePolicy { Stock, AlwaysAsk, NoUpdates };

struct UpdatePolicySupport {
    bool supported = false;
    QString reason;
};

// loadUpdatePolicy reads storage/update-policy. A missing file means Stock.
// An invalid or unreadable file returns Stock with error set. No file is changed.
UpdatePolicy loadUpdatePolicy(const QString &storage, QString &error);

// saveUpdatePolicy atomically saves the preference, without applying it.
// Returns false with error set on failure; the previous file remains intact.
bool saveUpdatePolicy(const QString &storage, UpdatePolicy policy, QString &error);

// initializeUpdatePolicy installs process-local hooks once, on Nickel's GUI
// thread. Call after Home appears. Unknown firmware and non-Nickel processes
// remain stock and return supported=false with a reason. The hooks and their
// original functions must remain loaded for the lifetime of the process.
// Every hook bypasses enforcement during setup and after reset begins.
UpdatePolicySupport initializeUpdatePolicy(UpdatePolicy policy);

// updatePolicySupport returns the result of initialization. A supported result
// describes the runtime hooks, not physical-device or recovery validation.
UpdatePolicySupport updatePolicySupport();

// applyUpdatePolicy changes the process preference without writing storage.
// Call only after a successful save. Unsupported runtimes remain stock.
// Changing the policy invalidates pending manual-check grants and confirmations.
void applyUpdatePolicy(UpdatePolicy policy);
} // namespace NickelModManager
