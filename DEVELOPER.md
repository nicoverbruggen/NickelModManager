# NickelModManager development

## Build

```sh
sh tools/build.sh
```

Start Docker. The build wrapper uses POSIX shell; CMake embeds the shutdown script and icon licence. Icon geometry is ordinary C++ source, so there is no SVG converter or Python dependency. The script builds both packages with the public [kobuild](https://github.com/nicoverbruggen/kobuild) SDK images, `ghcr.io/nicoverbruggen/kobuild:qt5-v0.1.0` and `ghcr.io/nicoverbruggen/kobuild:qt6-v0.1.0`. Qt5 targets firmware 4.x; Qt6 targets firmware 5.x and 6.x. No local Qt SDK is required.

Releases include the Qt5 package for firmware 4.x from 4.23 and the Qt6 package for Kobo 5.18, Kobo 6.0 and Tolino Vision Color 5.18.264769. Stock-firmware runtime checks on Kobo 5.18.270971 and Tolino 5.18.264769 are sufficient for these Qt6 support claims; separate physical checks are not required. Older Android-based Tolino devices are not supported.

To build only Qt6:

```sh
sh tools/build.sh --qt 6
```

Use `--qt 5` for Qt5. Packages go to `build/qt5/KoboRoot.tgz` and `build/qt6/Kobo.tgz`. Set `CONTAINER_ENGINE=podman` to use Podman. To use another kobuild image, pass `--qt5-image` or `--qt6-image`, or set `NMM_QT5_IMAGE` or `NMM_QT6_IMAGE`. When changing SDKs in an existing build folder, remove that folder's `CMakeCache.txt` and `CMakeFiles/` before rebuilding.

Add `--probes` to build and package the `probe-a` and `probe-b` test mods. These are test mods, not for a device: each logs when it loads and can act like NickelHook's failsafe or stop Nickel on request.

One C++14 source tree serves both Qt versions. `src/compat.h` and a few checks of `QT_VERSION` cover the parts of Qt that 5.2.1 does not have. Each kobuild image supplies its CMake toolchain.

The version comes from the build, not from `CMakeLists.txt`. `tools/build.sh` uses `--version` or `NMM_VERSION` when given, and otherwise `git describe --tags --always --dirty` for the checkout, such as `0.1.0` on a tagged commit or `71e1e8e-dirty` between releases. A leading `v` is dropped, and a version with characters other than letters, digits, `.`, `_`, `+` and `-` is refused. CMake receives it as `NICKELMODMANAGER_VERSION`, `dev` when unset, and generates `project_info.h` from `cmake/project_info.h.in` with it and the author, project URL and licence. The manager's details page displays this information from the compiled library. The CMake target retains the project name and sets its output filename to `libnickelmm.so`.

On firmware 4.x, Nickel delivers taps as touch events. Its own buttons take them directly, and Qt 5.2 does not turn an unhandled touch into a mouse click for other widgets. Every button and switch in NickelModManager takes touch directly and clicks when the finger lifts inside it. Qt6 controls also accept touch directly, so a parent row cannot consume a switch's touch before mouse synthesis. A release without a matching press never clicks a control.

## Source layout

C++ code uses the `NickelModManager` namespace.

- `src/entrypoint/` owns Qt plugin registration, startup, uninstall markers and the manager's startup failsafe. It starts the store before Home.
- `src/software.*` identifies the active Kobo or Tolino software family, with Kobo as the fallback.
- `src/controller.*` connects the store and restore hook to the manager dialog.
- `src/nickel.*` detects Home and inserts Manage Mods into Nickel's More menu.
- `src/mods/` owns the mod model, optional metadata, installed libraries, saved copies and state persistence. Persistence reads and writes snapshots; it does not manage libraries.
- `src/updates/` owns the shutdown hook, restore package and tar/gzip encoding.
- `src/ui/` owns the manager dialog, controls, pagination, notices, menu row and icon paths. `icons/phosphor/` keeps the Phosphor regular references, checksums and licence.
- `tests/` holds C++ tests. CTest runs them; `BUILD_TESTING=OFF` excludes them from the build.
- `tools/` holds the shell build wrapper and its container commands. `cmake/` holds byte embedding.
- `.github/workflows/` holds the checks and the release workflow.
- `probe/` holds test mods. `NICKELMODMANAGER_PROBES=ON` builds them independently of the manager plugin.

## Startup and removal

The entry point uses Qt’s image-plugin discovery and startup callback. It does not link NickelHook. `src/nickel.cpp` observes Qt widget events and checks widget names and layout shape. Software-family detection resolves optional Device methods. The update-policy backend resolves and intercepts private Nickel update functions after Home appears, only on verified firmware. These paths need runtime checks because private functions, widget names and layouts can change.

The minimum supported firmware is 4.23.15548. The Qt5 compatibility baselines also include Libra Colour 4.42.23033, Libra 2 4.38.23697 and Clara BW 4.45. Checks on these baselines do not establish compatibility with every device and firmware combination.

On first startup, `src/entrypoint/lifecycle.cpp` creates `.adds/nickel-mod-manager/uninstall` and records completion in `.adds/.nickel-mod-manager.initialized`. Later starts never recreate a deleted marker. The receipt is outside the settings folder, so deleting that folder requests removal too. An `uninstall-now` file also requests removal, including before the first initialization.

Removal runs before the store or UI starts. It records the restoration opt-out, removes owned shutdown entries and restore-package files, and then unlinks the manager library. It consumes the request and clears the receipt only after removal. It retains settings, saved mod copies and other installed libraries. Cleanup errors stop removal and keep the request for another attempt; completed operations remain. Reinstalling initializes the marker again and keeps existing saved state and the restoration opt-out.

The startup failsafe protects the first start of a new manager library and the first start after a firmware change. On such a start it moves `libnickelmm.so` one folder above `imageformats/`, to `libnickelmm.so.failsafe`. Both paths are on the system partition. The entry point keeps a process-lifetime loader reference so Qt cannot unload callback code after the rename. After Home has been visible for three seconds, confirmation links the parked library back into place without overwriting a new installation, then removes the parked name. It then writes `.adds/nickel-mod-manager/confirmed` with the library's SHA-256 and the firmware identity. A crash, early exit or missing Home leaves the library outside Qt's scan directory. Reinstalling the package retries startup and replaces a previous parked copy.

Later starts with the same library and firmware leave the library in place. NickelHook's failsafe parks a mod when Nickel stops while that mod starts. If the manager's failsafe were armed at the same time, the manager would be parked too, and it could not show that mod at the next start. A missing, damaged or different `confirmed` file arms the failsafe. Removal deletes the file, so a reinstall of the same library is protected again. The failsafe does not catch failures before the Qt callback or after confirmation. A crash caused by another mod during an armed start still leaves the manager parked. After such a failure, reinstall the manager.

## Mod management

At every start NickelModManager lists the `*.so` files in the folder its own library was loaded from. It leaves out itself, anything that is not a regular file, and `libkimg.so`, Kobo's own image plugin, which firmware 4.x ships in the same folder. On 6.0.276679 that folder holds no stock plugins. Every other library there counts as a mod. A table in `src/mods/mod.cpp` gives 25 known libraries their project names, for example `libnm.so` shows as NickelMenu and `libnickelclock.so` as NickelClock. The library names come from each project's `Makefile`, found through NickelHook's list of mods and a GitHub search for projects that use `NickelHook.mk`. Other mods show their file name without `lib` and `.so`.

Mods without metadata show no version. Each row shows the build date, the size and the file name, such as "12 Jun 2026 · 412 KB · libnm.so". The size comes from the installed library, or from the held or saved copy when the mod is off. The build date is the library's modification time. Kobo's package step keeps file times from the archive, so it is when the author built the package, close to a release date; NickelModManager keeps it on every copy. NickelModManager also records an install date: when it first saw those bytes, which covers updates too. The list does not show it. Mods that were already there at NickelModManager's first start have no install date.

NickelModManager copies each mod to `.adds/nickel-mod-manager/mods/` when it first sees the mod, and again when the bytes change, for example after the mod's own installer updated it. `state.json` next to that folder records, per file, the SHA-256, whether the mod is on and both dates. It also records the firmware identity from the last start.

- **Turn off.** NickelModManager checks that the copy holds the same bytes as the library, then deletes the library from the plugin folder and from NickelHook's failsafe, described below. The running Nickel keeps the library it already loaded.
- **Turn on.** NickelModManager copies the kept file back through a temporary file one folder up, then renames it into place. Qt 6 loads any file in a plugin folder, so a half-written copy must never sit there. It refuses a copy whose hash differs from the recorded one.
- **Restart.** While a change waits, the footer shows Restart to apply and the number of changes. The button shows "Restarting now…", waits half a second so e-ink can draw it, then runs `/sbin/reboot`.

Opening the manager reads the plugin folder and NickelHook's failsafe again, so changes made while Nickel runs show up. A library that appears during the session loads at the next start. The check for a firmware update runs only when Nickel starts.

If a mod's own installer puts it back after it was turned off, it counts as on again. If a mod that was on disappears for any reason other than a full firmware update on 5.x or 6.x, something else removed it, such as its own uninstaller. NickelModManager then marks it off with a note and keeps its copy.

### Optional mod metadata

See [Mod manifests](docs/mod-manifest.md) for the metadata, settings declarations, config format, restart requirements and conditional availability rules. Keep that document as the format reference. Native settings support currently accepts boolean declarations and the `colon-bool-first-v1` config format.

`src/software.*` queries Nickel's optional `Device::getCurrentDevice()` and `Device::isTolino()` methods when opening the manager. The About page shows Kobo or Tolino. Font selection remains separate. A missing method or device defaults to Kobo by design. Hardware, fonts and firmware version are not alternative brand-detection signals.

`tests/metadata_test.cpp` checks optional declarations, hash association, unsupported schemas, malformed and repeated fields, file-size limits, ambiguous declarations, unrelated sections and symlinked folders. `tests/software_test.cpp` checks the Kobo fallback outside Nickel and the stable condition identifiers. Both run for each Qt target.

### Permanently removing disabled mods

Press and hold a mod row for 550 ms to open its removal confirmation. Moving outside the row, moving more than 16 units at 300 ppi, cancelling touch or closing the window cancels the hold. A short tap on the row does nothing. The switch keeps its own input and never opens the card. Touch is handled directly on both Qt versions. The confirmation describes the saved-copy removal and retained settings. Choosing Permanently remove completes the action without another modal.

Permanently remove is available only when the mod is disabled, not loaded and not parked. A mod turned off in this process must finish a restart first. The store also refuses any installed or held file at either failsafe location, including files created since the UI snapshot. It deletes only the saved library and its state entry, leaving mod-owned settings and support files untouched. The controller refreshes the enabled-library restore package first; a failed refresh prevents forgetting. If saving state fails after deleting the saved library, the row remains for retry and the error reports the partial change. A successful forget survives reopening and restarting. Reinstalling the mod discovers it again normally.

### NickelHook's failsafe

While a NickelHook mod starts, its failsafe moves the mod's library out of the way and moves it back after the mod's delay. Upstream NickelHook, which firmware 4.x mods use, renames `imageformats/libnm.so` to `imageformats/libnm.so.failsafe`. Its Qt 6 port moves it one folder up, to `/usr/local/Kobo/libnm.so.failsafe`, because Qt 6 loads any file in a plugin folder. When Nickel stops before the delay ends, the library stays there and the mod no longer runs. Qt 5.2 still loads a held `libnm.so.failsafe` from the plugin folder, but NickelHook then finds itself loaded from its failsafe and does nothing. The Qt 6 port also leaves it there when the mod's own compatibility check fails.

NickelModManager looks in both places:

- If this Nickel process has the library loaded, NickelHook is at work in this start, and the mod counts as on.
- Otherwise NickelHook parked it. The row moves to the Failed to load section. Turning it on asks for confirmation first, then renames the held library back into the plugin folder. It loads at the next start.
- Turning a mod off removes the held file first and the library second, so a failsafe that restores the library later finds nothing to restore.

The manager uses Kobalt-style interface typography, grey pressed feedback and bordered action cards. Titles and rows use the installed sans-serif UI font. Section labels, names and supporting text use consistent sizes and density-scaled spacing. Dark grey top and bottom borders separate tappable rows. Text and control outlines stay black. Section headers use Kobalt’s light grey fill. The manager retains its own C++14 controls and Qt 5.2.1 compatibility; it does not link Kobalt.

The first card opens NickelModManager’s Settings and About tabs. Settings contains the three update policies and the independent restoration switch on one page, with short descriptions and a shared scope note. Unsaved preferences use a separate footer with a single top divider. The mod restart footer appears only on the mod list. Each policy and restoration row has an icon and the same caption spacing. Radio indicators stay transparent so row press feedback covers their whole cell. Choices stay unsaved until Save changes; Cancel restores saved preferences. Leaving with unsaved preferences offers a discard confirmation. About shows the version, creator, project URL, build date, software family, library and licence.

The mod list groups entries by their state in this process: Enabled mods, Disabled mods and Failed to load. Each compact row shows the name and verified version, a manifest description when available, a circular settings or info button and the enable switch. A mod without descriptive metadata retains the build date, size and filename. A tap on the row or circular button opens its Settings/About page. Long press opens the single permanent-removal confirmation. The switch changes only the library state. Turning on a mod that failed to load still requires confirmation. The list uses pages rather than scrolling. Rows use a single shared divider; About dividers use lighter grey. Tabs keep their filled selection without outside outlines. Grey section headers use a shared 64-pixel design height and vertically centered captions in the list, manager pages and mod settings.

The restart footer counts mod changes and saved settings that require a restart separately. `src/mods/settings.*` reads the manifest and edits config independently of the installed-library store. It preserves unrelated content and first-key semantics, refuses linked or missing files and reports stale snapshots as conflicts. The settings page stages edits until Save changes. Its footer separates unsaved changes and actions from the settings with a single top divider. It never writes while browsing. `apply=restart` is the manifest default and can be overridden per setting; `apply=live` means the mod handles live config reads. See [the manifest contract](docs/mod-manifest.md).

The manager uses the UI library in `src/ui/`: `Header` with Back, `PagedList`, `Toggle`, `ActionButton`, `choose()`, `MenuEntry` and the Phosphor regular outline icons, pinned to the same assets as Kobalt. The Manage Mods entry in Nickel’s More menu uses the original Lucide blocks icon. It builds a page before it shows it, turns one tap into one rebuild, keeps the footer height fixed when a change starts or stops waiting, and builds a new window each time it opens. Each of these avoids a redraw on e-ink. `src/ui/fonts.cpp` selects Rakuten Serif and Rakuten Sans on newer Kobo firmware, or Georgia and Avenir Next on older firmware. The Nickel integration passes the stock Help label's font when opening the manager. When that font is Bariol, titles, rows and secondary text all use Bariol to match Tolino. This native font hint matters because Tolino ships Rakuten fonts too. Otherwise the manager uses the selected sans-serif for titles, rows, section labels, notes and buttons. If neither known family is available, selection falls back to Nickel's `DefaultSerif` and `DefaultSansSerif` aliases. Hidden symbol visibility keeps this library's classes apart from those of other mods in the same process.

The About rows show “Label:” on the left and the value on the right, as Device information does. Update policies and restoration occupy the Settings tab. Mod settings use the same page controls, spacing and pagination as the mod list.

## Firmware updates

**Firmware 4.x keeps mods.** On 4.46.23836, `/etc/init.d/rcS` extracts `.kobo/KoboRoot.tgz` over `/` and the upgrade script flashes only the boot images. Under `/usr/local/Kobo/imageformats` the update package holds only `libkimg.so`, so mods stay where they are. The cached 4.38 to 4.46 packages hold the same single file. NickelModManager never puts a mod back on 4.x, because a mod missing there was removed on purpose.

**A full update on 6.x removes them.** The stock `/etc/init.d/ota` script on 6.0.276679 runs at every boot. It first extracts `.kobo/Kobo.tgz` under `/usr/local/Kobo` and deletes it. Then, in the same boot, it runs stage 1 of `.kobo/update.tar`, which flashes the kernel and boot images and reboots into recovery. Stage 2 runs in recovery and writes `rootfs.img` over `system_a`. Nothing on the system partition survives. This was read from the extracted 6.0.276679 root filesystem and the `driver.sh` in its update archive. The recovery partition's own scripts were not available.

Nothing in the new system reads a package queued before the update started: the old system uses up `.kobo/Kobo.tgz` one boot too early, and the new system runs nothing from user storage. A shutdown script on the old system can still bring a mod back. Stage 1 ends with a reboot into recovery, and that reboot runs the old system's `/etc/rc6.d` shutdown entries after the stock script has already used `Kobo.tgz`. A package queued there survives recovery stage 2, and the stock script applies it on the first boot of the new system. NickelModManager uses this when Restore after firmware updates is on, as described below. The approach comes from an earlier experiment, Nickel Persist, which restored its own library this way.

With the switch off, install NickelModManager's `Kobo.tgz` again after a full update on 6.x. At its first start, NickelModManager compares the firmware identity with the one in `state.json`. The identity is the git revision in `/usr/local/Kobo/revinfo`, with Nickel's version string as a fallback. When it differs, NickelModManager copies every mod that was on back into the plugin folder and leaves the ones that were off alone. A card says which mods came back and offers a restart. They load after it.

A Kobo app update that arrives as `Kobo.tgz` deletes only the files listed in that archive. Mods in `imageformats/` survive it, and NickelModManager has nothing to do.

The first start on other firmware arms NickelModManager's own startup failsafe again. Each mod protects itself through its NickelHook failsafe; see Mods that fail to load.

### Update policies

`src/updates/policy.*` stores the preference in `.adds/nickel-mod-manager/update-policy` using an atomic replacement. Stock is the default. The controller initializes the backend after Home appears. Saving changes the running policy immediately; it does not need a restart. Saving mod restoration is a separate operation. If one preference saves and another fails, the UI reports the partial result and reloads the saved values.

Stock preserves automatic, optional and beta update behavior. Always ask converts automatic updates to prompts and preserves optional and beta prompts. No updates declines offers unless a native manual Check for updates grants one prompt. The grant belongs to the current update sync and is invalidated by cancellation, reset and policy changes. An unused grant must not authorize a later background sync.

The hooks check that setup has completed and bypass enforcement as soon as reset/sign-out preparation starts. They do not edit updater URLs, boot scripts, recovery files or manually installed packages. This design does not establish complete physical reset/recovery behavior; that remains a hardware check.

The backend supports ARM Qt5 on 4.38.23697, 4.45.23697, 4.45.23792, 4.46.23836 and 4.46.23852, and ARM Qt6 on 6.0.276679, with required symbol and function-table checks. Other firmware uses stock behavior and disables the policy controls with a reason. Qt6 reads the update type through the native result accessor. Its queued-command idle hook keeps the command alive while consent is pending; its cancellation hook revokes that consent without changing unrelated sync commands. The GUI thread caches the existing setup singleton before hooks run on sync workers. The saved preference is retained separately from support. Mod restoration remains independent. Do not expand this allowlist from a compile check alone.

On 2026-11-14, ordinary update-flow checks passed on 4.38.23697 and 4.46.23852: Always ask prompted before automatic download and preserved decline/repeat/accept behavior; No updates blocked background offers, allowed a manual prompt and blocked a later background sync. Stock automatic download passed on 4.46.23852. These checks used local invalid download targets. The listed 4.45 versions have symbol and earlier-prototype evidence, but no new-policy runtime pass. The separate beta workflow remains unverified at runtime because the same test fixture did not show its prompt under Stock. Production-wrapper tests cover its consent rules. Sanitizer checks passed for the backend; they do not establish physical-device or reset/recovery behavior.

On 2026-11-14, ordinary update-flow checks also passed on Qt6 firmware 6.0.276679. Always ask preserved decline, repeat and accept behavior without downloading before consent. No updates blocked background offers, allowed a manual optional prompt, blocked a later background sync and permitted download after manual acceptance. Stock permitted automatic download. The targets were local invalid archives; these checks did not install firmware. Both SDK suites passed with the Qt6 request, queue-lifetime and cancellation regressions. Real beta offers, physical touch and full reset/recovery cycles remain unverified.

### Restore after firmware updates

Automatic restoration was verified on a physical Clara Colour during a full reinstall of 6.0.276679. The same hook passes stock-script and system-replacement checks on Kobo 5.18.270971 and Tolino 5.18.264769. The hook accepts only known stock scripts, so these results do not establish compatibility with future update procedures.

This setting is in the Mod protection section of NickelModManager's Settings page. On firmware 5.x and 6.x it is a switch, on by default, and greyed out when the stock update scripts are not the ones NickelModManager knows. Turning it off writes `.adds/nickel-mod-manager/restore-off`; turning it on again deletes that file. On firmware 4.x the row shows a lock instead of a switch and says the feature is not needed, because system updates there keep installed mods.

While it is on and the stock scripts are known, NickelModManager, a few seconds after Home is up:

- writes `/etc/init.d/nickelmodmanager` and links `/etc/rc6.d/K00nickelmodmanager` and `/etc/rc6.d/S00nickelmodmanager` to it. The script is `src/updates/restore-hook.sh`, embedded in the library; no package carries it.
- keeps `.adds/nickel-mod-manager/restore/Kobo.tgz`, a package with its own library and the kept copies of the mods that are on, with `Kobo.tgz.sha256`, `manager.sha256` and a `manifest` of the inputs. It rebuilds the package a few seconds after Home is up and after each change, and skips the write when the inputs are unchanged. The archive keeps each file's time, so build dates survive.

`rc` runs runlevel 6 differently after a reboot from early boot, where stage 1 reboots: it skips the `K` entries and runs the `S` entries with `stop`. After a normal reboot it runs both. The script acts on `stop` and is safe to run twice. It returns at once when there is no `.kobo/update.tar`, so a normal reboot costs nothing. Otherwise it queues the package only when all of these hold, and logs the first one that fails to `.adds/nickel-mod-manager/restore.log`:

1. The setting is on: `.adds/nickel-mod-manager/restore/` exists. Turning the setting off removes it.
2. `/etc/init.d/ota`, `/etc/init.d/rc`, `/etc/init.d/mount-userdata` and `/usr/libexec/platform/utils.sh` have the SHA-256 of the stock 5.18 or 6.0 scripts. Kobo 5.18.270971, Tolino 5.18.264769, 6.0.274403 and 6.0.276679 ship the same first three; `utils.sh` differs between 5.18 and 6.0. A firmware with other scripts may run its update differently, so the hook does nothing there and the switch is greyed out.
3. `/mnt/onboard` is a mounted FAT filesystem.
4. The boot target that `ntx_hwconfig` reports for `BootPartNo` is the number of the recovery partition. The read gets three seconds; the firmware has no `timeout` command, so the script ends a slower read itself. A decimal or `0x` value is accepted; anything else stops the hook.
5. NickelModManager's library has the hash recorded with the package, so a removed or replaced manager is not brought back.
6. There is no `.kobo/Kobo.tgz` yet. One the user put there stays.
7. The package matches its recorded SHA-256 and passes `gunzip -t`.

It then copies the package to a temporary name in `.kobo`, checks the copy's hash, renames it to `Kobo.tgz` and syncs. Nothing else writes `.kobo` during shutdown; the only other writer is the script's own second run, which finds the package and stops.

On the first boot of the new system, the stock `ota` script extracts the package under `/usr/local/Kobo` before Nickel starts. NickelModManager arms its own startup failsafe when the library or firmware identity changed, and writes its shutdown entries again a few seconds after Home is up, because the new system has none. A same-version reinstall also restores mods; the shutdown hook does not require a version change. Mods that were off stay off; they are not in the package.

Turning the switch off removes the shutdown entries and the package. NickelModManager only removes files that are its own: the script must carry its marker line and the links must point at it. It refuses to write over a script or link that is not its own. Marker-based uninstall turns restoration off and removes these entries before deleting the manager library.

The hook uses Bash, which the supported Qt6 firmware supplies. It runs its checks and file operations in a separate process group under a supervisor. Bash's built-in timed read waits up to 30 seconds on a completion pipe, without a timer process. The supervisor sends SIGKILL to the worker leader first, then its group, after completion, timeout or HUP, INT or TERM. It returns success so `rc` can continue the reboot. The worker stays alive after reporting completion until the supervisor stops it, keeping its PID reserved during cleanup. If `setsid` is unavailable or the worker fails to start, the supervisor skips restoration.

Timeout cleanup does not write to storage or standard output or error, because those writes could block. It does not wait for the killed worker. An interrupted copy can leave the temporary package, which the updater ignores and the next copy removes. If the final rename already completed, a complete package can remain queued after a timeout. A deadline cannot fix uninterruptible kernel I/O or guarantee that later shutdown steps can sync or unmount stalled storage. SIGKILL cannot be trapped; killing the supervisor that way bypasses its cleanup.

Restoration and deadline fixture checks passed with stock Bash and `setsid` under QEMU for the supported Qt6 firmware versions. Forced deadline expiry and cleanup have not been tested on hardware.

## Mods that fail to load

NickelModManager does not watch other mods. Each NickelHook mod protects itself: its failsafe moves the library aside while the mod starts, and leaves it there when Nickel stops before the mod's delay ends. NickelModManager shows such a mod in the Failed to load section. Limits:

- NickelHook protects a mod only during its own failsafe delay. Kobalt, for example, uses 3 seconds. A crash after that delay is not caught.
- A mod without NickelHook has no such protection.

## Checks

Build and run the store, metadata, software-family, persistence and shutdown-hook tests for both Qt versions with:

```sh
sh tools/build.sh --test
```

Add `--qt 5` or `--qt 6` to check one target. The tests run under QEMU inside the kobuild images, with each SDK's Qt and the container's ARM runtime. The Qt5 run uses a newer glibc than firmware 4.x. These checks do not verify firmware runtime compatibility or device behavior.

`tests/mods_test.cpp` checks the store rules in temporary folders: first start, turning off and on in one session, a mod removed outside the manager, an installer that replaces or restores a mod, a full update with and without putting mods back, both failsafe locations, a rescan, both dates, a changed copy and a damaged `state.json`. Forget checks cover refusal while loaded, new installed files, both failsafe locations, retained settings and other mods, persistence across rescans and firmware changes, reinstallation, and retry after a state-write failure. It needs only Qt Core. For a host build without UI, configure with `cmake -S . -B build/host -DNICKELMODMANAGER_PLUGINS=OFF`, build with `cmake --build build/host`, then run `ctest --test-dir build/host --output-on-failure`. For older CTest versions, run `ctest --output-on-failure` from the build directory.

`tests/policy_test.cpp` drives the production hook wrappers with native-call fixtures. It covers the three policies, ordinary and beta offers, one manual grant, cancellation, setup/reset bypass, worker-thread confirmation and preference persistence. Qt6 fixtures also cover the result-object signature and queue lifetime while consent is pending. The SDK test runner sets the cross-SDK platform plugin path for this QApplication-based test. These fixtures do not prove the firmware hooks or a full reset cycle.

`tests/settings_test.cpp` checks authenticated declarations, software conditions, restart declarations, missing config, first-key behavior, comments and CRLF preservation, malformed values, hidden-key rejection, changed snapshots, appended keys and symlink refusal.

`tests/persistence_test.cpp` checks saved-state round trips, exclusion of session flags, filename validation, damaged-state recovery and failed writes. It calls persistence directly without running mod discovery or changing installed libraries.

`tests/hook_test.cpp` checks the shutdown entries and the package in a temporary system: installation, executable mode, the link targets, the package's hashes and contents with the host `tar`, build dates in the archive, the skipped rewrite, entries put back after an update removed them, removal, refusal of a script or link that is not its own, and refusal on unknown stock scripts. It also checks that the library and `src/updates/restore-hook.sh` list the same stock hashes. Its package also passes `gunzip -t`, `tar -tf` and `tar -xzf` from the BusyBox of 6.0.276679, which the stock `ota` script uses.

`tests/hook_script_test.cpp` runs the embedded shutdown script against temporary fixture systems. By default, it replaces the accepted hashes in the temporary script with hashes of synthetic stock files. It also shortens the fixture deadline to two seconds. The installed script is unchanged. It covers queuing once over two runs, boot target formats, a normal reboot, the setting off, unknown stock scripts, unmounted storage, a changed manager, a changed or damaged package, and an existing package. Deadline checks stall hashing, a partial copy and the final sync. They check that the supervisor returns success, stops child commands even when they ignore TERM, leaves an incomplete package unqueued and allows a later copy to replace it. A stall after the final rename leaves the complete package queued. Regression checks cover blocked diagnostic output, timer-command cleanup, HUP, INT and TERM interruption, worker startup failure, a stall before process-group creation, and child cleanup after normal completion. Every invocation checks that `update.tar` is unchanged. It needs `sha256sum`, `gunzip`, `setsid` and Bash; CTest reports a skip if they are unavailable.

To use real stock scripts on a Linux host, set `FIRMWARE_ROOT` to an extracted 5.18 or 6.0 root filesystem when running `nmm_hook_script_test`. In this mode, the test uses the production hash checks without substitution. The container build uses synthetic fixtures and needs no firmware files.

`tests/ui_test.cpp` checks the fixed icon paths, mirrored chevron, finite coordinates, view-box bounds, embedded licence and font selection for legacy, modern, mixed and unknown firmware font sets. It also checks Tolino's native Bariol hint when Rakuten fonts are installed, Kobo's selection with the same installed families, and fallback when Bariol is missing. The former converter's malformed-input tests are no longer needed because the project does not parse SVG at build time or runtime.

`tests/lifecycle_test.cpp` checks first-run marker creation, confirmation, arming only for a new library or firmware, damaged or missing confirmation records, both uninstall triggers, settings-folder deletion, shutdown-hook cleanup, preservation of other mods and saved state, reinstall after removal or interrupted startup, refusal to overwrite a newer package, and cleanup failures. These filesystem tests do not establish Qt's plugin-loading behavior on firmware.

A normal `sh tools/build.sh` excludes tests and probes. `--test` enables tests and runs them through CTest under QEMU. `--probes` enables the separate probe packages. Direct CMake builds default to including tests through `BUILD_TESTING`; use `-DBUILD_TESTING=OFF` for a library-only build.

## Continuous integration and releases

`.github/workflows/checks.yml` runs on every push to `main` or `develop` and on pull requests. It runs `sh tools/build.sh --qt 5 --test` and `sh tools/build.sh --qt 6 --test`, the same commands as a local build. It uploads Qt5 `KoboRoot.tgz` as the `NickelModManager` artifact and Qt6 `Kobo.tgz` as `NickelModManager-Qt6`. Each upload fails if its package is missing.

A tag build passes the tag to `tools/build.sh` as the version, so the released library shows it on the details page. To release, add or rename a section in `CHANGELOG.md` to the chosen version, such as `## v0.3`, and push the matching tag. `.github/workflows/release.yml` waits for both build and test jobs, downloads both artifacts, refuses a tag without release notes, and publishes `KoboRoot.tgz` and `Kobo.tgz`. A missing package fails the release. Both packages contain only the manager library, with the paths required by their firmware; they do not include other mods.

## Qt6 device test of the restore hook

Use this procedure to check restoration on a physical device. The baseline is Clara Colour 6.0.276679:

1. Back up user storage. Check that `.kobo` holds no `Kobo.tgz`, `KoboRoot.tgz` or `update.tar`.
2. Copy `build/qt6/Kobo.tgz` to `.kobo/`, eject and disconnect USB. Add one compatible Qt6 mod the same way, then confirm that both work.
3. Open More, Manage Mods. Check that both show as On, and that Restore after firmware updates is on.
4. Connect by USB. `.adds/nickel-mod-manager/restore/` should hold `Kobo.tgz`, `Kobo.tgz.sha256`, `manager.sha256` and `manifest`. `Kobo.tgz` should list `imageformats/libnickelmm.so` and the real mod.
5. Copy `update.tar` from the device's full firmware ZIP to `.kobo/`, eject and disconnect USB. Reinstalling 6.0.276679 this way passed the baseline check. An update to a different version needs its own check.
6. After the update, open Manage Mods. Both should show as On, the real mod should work, and no reinstall should be needed.
7. Connect by USB and keep `.adds/nickel-mod-manager/restore.log`. It shows the raw boot target value and `QUEUED`, or the first check that failed.

If the update ends without the mods, read `restore.log` first. A `SKIP` line names the check that stopped the hook. No line at all means `rc` did not run the hook after stage 1, or `.kobo/update.tar` was gone by then.

## Qt6 hardware validation

Automatic restoration passed a same-version reinstall of 6.0.276679 on a physical Clara Colour. Validation with an update to a new firmware version on a physical device is still required.

### Additional device coverage

Version 0.3 passed stock-firmware runtime checks on Clara Colour 5.18.270971 and Tolino Vision Color 5.18.264769 on 2026-10-09. Both passed installation, startup confirmation, About navigation and the displayed version, touch controls, mod enable/disable across restarts, and deletion-marker removal while preserving another mod. The manager uses Rakuten fonts on Kobo and Bariol on Tolino. Tolino includes both families, and its `DefaultSerif` and `DefaultSansSerif` substitutions still name Georgia and Avenir Next, so the stock menu label supplies the native font hint.

The installed restoration hook passed its production hash checks on both versions. A fixture supplied mounted storage and the recovery boot target; two hook invocations queued one valid package and preserved it. After replacement with stock system files, the manager and enabled probe loaded again, the disabled probe stayed off, and the manager rebuilt its restore package. These checks cover package restoration and saved state, without running the device's recovery firmware. They are accepted as sufficient release coverage for these 5.18 versions.

Qt5 has been tested on physical devices. This does not cover every device and firmware combination. Stock-firmware runtime checks on Elipsa 2E 4.38.23697 passed for 227 ppi layout geometry, About navigation, touch controls, mod enable/disable across restarts and deletion-marker removal. The density check uses the screen's shorter edge because Qt initially reports landscape dimensions before Nickel sets portrait orientation. Physical Elipsa 2E touch and e-ink behavior still need a device check.

### Software-family checks

On 2026-11-14, both SDK builds and their automated checks passed with native software-family detection. Stock-firmware runtime checks showed Kobo on Libra 2 4.38.23697 and Clara Colour 6.0.276679, and Tolino on Vision Color 5.18.264769. The manager's About page remained usable on all three. The software test verifies the Kobo fallback outside Nickel. These earlier results validate family detection. Settings editing and condition checks have separate coverage below. Physical-device coverage is unchanged.

### Mod metadata and forget checks

On 2026-11-14, both SDK builds and their automated checks passed with optional version metadata and the forget action. Stock-firmware runtime checks passed on Libra 2 4.38.23697 and Clara Colour 6.0.276679 with NickelHome installed. Checks covered startup, version display, a short row tap, long press, cancelled hold, the enable switch, pending-restart restrictions, restart through the manager, confirmation cancellation, forgetting, unchanged mod config, persistence across restart and discovery after reinstall. Follow-up UI checks verified one removal confirmation, cancellation, retained settings, a bold empty-state title, `KoboRoot.tgz` instructions on Qt5 and `Kobo.tgz` instructions on Qt6. These results do not establish physical touch or e-ink behavior. Manifest installation in mod release packages remains separate work; the runtime checks supplied test declarations on user storage.

### Native settings UI checks

On 2026-11-14, SDK builds and all ten registered checks passed for Qt5 and Qt6. Stock-firmware UI checks passed on Libra 2 4.38.23697, Clara Colour 6.0.276679 and Tolino Vision Color 5.18.264769 with NickelHome installed. Checks covered compact metadata, circular settings controls, settings navigation, saved config edits, restart-count display, reverting to the original value, software-family display and Qt5 policy preference Save/Cancel. The Tolino settings page showed its OS-specific declaration. A row tap also opened NickelHome settings. Restoration Save/Cancel and re-enabling passed on 6.0.276679 while preserving the starting preference.

A follow-up Qt6 check with the live NickelHome build showed and hid the bottom Home row after repeated saves in the same process. Its manifest uses `apply=live`, so these saves do not add a restart count. The Qt5 manifest keeps `apply=restart`.

These checks used test manifests on user storage. Release-package manifest installation remains separate work. Physical touch and e-ink checks, a full physical reset/setup cycle and real beta offers remain unverified.

## History

NickelModManager started as a cut-down sibling of NickelLoader, a larger loader prototype with an SDK, a shared hook engine, compatibility checks and quarantine. That prototype is shelved: hook conflicts belong in NickelHook, and checks before load are covered by NickelHook's failsafe and each mod's own checks. The UI library in `src/ui/` comes from it.

## Licence

NickelModManager is released under the [MIT licence](LICENSE). The Phosphor icons in `src/ui/icons/phosphor/` keep their pinned source manifest and MIT notice. The library embeds the combined icon notices from `src/ui/icons/LICENSE`. Legacy Lucide asset notices remain with their source files.
