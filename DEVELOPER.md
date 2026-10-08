# NickelModManager development

## Build

```sh
sh tools/build.sh
```

Start Docker. The build wrapper uses POSIX shell; CMake embeds the shutdown script and icon licence. Icon geometry is ordinary C++ source, so there is no SVG converter or Python dependency. The script builds both packages with the public [kobuild](https://github.com/nicoverbruggen/kobuild) SDK images, `ghcr.io/nicoverbruggen/kobuild:qt5-v0.1.0` and `ghcr.io/nicoverbruggen/kobuild:qt6-v0.1.0`. Qt5 targets firmware 4.x; Qt6 targets firmware 5.x and 6.x. No local Qt SDK is required.

Releases include the Qt5 package for firmware 4.x from 4.23 and the Qt6 package for firmware 6.0. Firmware 5.x uses the same Qt6 build but remains experimental.

To build only Qt6:

```sh
sh tools/build.sh --qt 6
```

Use `--qt 5` for Qt5. Packages go to `build/qt5/KoboRoot.tgz` and `build/qt6/Kobo.tgz`. Set `CONTAINER_ENGINE=podman` to use Podman. To use another kobuild image, pass `--qt5-image` or `--qt6-image`, or set `NMM_QT5_IMAGE` or `NMM_QT6_IMAGE`. When changing SDKs in an existing build folder, remove that folder's `CMakeCache.txt` and `CMakeFiles/` before rebuilding.

Add `--probes` to build and package the `probe-a` and `probe-b` test mods. These are test mods, not for a device: each logs when it loads and can act like NickelHook's failsafe or stop Nickel on request.

One C++14 source tree serves both Qt versions. `src/compat.h` and a few checks of `QT_VERSION` cover the parts of Qt that 5.2.1 does not have. Each kobuild image supplies its CMake toolchain.

The version comes from the build, not from `CMakeLists.txt`. `tools/build.sh` uses `--version` or `NMM_VERSION` when given, and otherwise `git describe --tags --always --dirty` for the checkout, such as `0.1.0` on a tagged commit or `71e1e8e-dirty` between releases. A leading `v` is dropped, and a version with characters other than letters, digits, `.`, `_`, `+` and `-` is refused. CMake receives it as `NICKELMODMANAGER_VERSION`, `dev` when unset, and generates `project_info.h` from `cmake/project_info.h.in` with it and the author, project URL and licence. The manager's details page displays this information from the compiled library. The CMake target retains the project name and sets its output filename to `libnickelmm.so`.

On firmware 4.x, Nickel delivers taps as touch events. Its own buttons take them directly, and Qt 5.2 does not turn an unhandled touch into a mouse click for other widgets. So on Qt 5 every button and switch in NickelModManager takes the touch itself and clicks when the finger lifts inside it.

## Source layout

C++ code uses the `NickelModManager` namespace.

- `src/entrypoint/` owns Qt plugin registration, startup, uninstall markers and the manager's startup failsafe. It starts the store before Home.
- `src/controller.*` connects the store and restore hook to the manager dialog.
- `src/nickel.*` detects Home and inserts Manage Mods into Nickel's More menu.
- `src/mods/` owns the mod model, installed libraries, saved copies and state persistence. Persistence reads and writes snapshots; it does not manage libraries.
- `src/updates/` owns the shutdown hook, restore package and tar/gzip encoding.
- `src/ui/` owns the manager dialog, controls, pagination, notices, menu row and icon paths. `icons/` keeps the pinned Lucide references and licence.
- `tests/` holds C++ tests. CTest runs them; `BUILD_TESTING=OFF` excludes them from the build.
- `tools/` holds the shell build wrapper and its container commands. `cmake/` holds byte embedding.
- `.github/workflows/` holds the checks and the release workflow.
- `probe/` holds test mods. `NICKELMODMANAGER_PROBES=ON` builds them independently of the manager plugin.

## Startup and removal

The entry point uses Qt's image-plugin discovery and startup callback. It does not link NickelHook, resolve private Nickel symbols or patch Nickel functions. `src/nickel.cpp` observes Qt widget events and checks widget names and layout shape. Firmware compatibility still needs runtime checks because those names and layouts can change.

The minimum supported firmware is 4.23.15548. The Qt5 compatibility baselines also include Libra Colour 4.42.23033, Libra 2 4.38.23697 and Clara BW 4.45. Checks on these baselines do not establish compatibility with every device and firmware combination.

On first startup, `src/entrypoint/lifecycle.cpp` creates `.adds/nickel-mod-manager/uninstall` and records completion in `.adds/.nickel-mod-manager.initialized`. Later starts never recreate a deleted marker. The receipt is outside the settings folder, so deleting that folder requests removal too. An `uninstall-now` file also requests removal, including before the first initialization.

Removal runs before the store or UI starts. It records the restoration opt-out, removes owned shutdown entries and restore-package files, and then unlinks the manager library. It consumes the request and clears the receipt only after removal. It retains settings, saved mod copies and other installed libraries. Cleanup errors stop removal and keep the request for another attempt; completed operations remain. Reinstalling initializes the marker again and keeps existing saved state and the restoration opt-out.

The startup failsafe protects the first start of a new manager library and the first start after a firmware change. On such a start it moves `libnickelmm.so` one folder above `imageformats/`, to `libnickelmm.so.failsafe`. Both paths are on the system partition. The entry point keeps a process-lifetime loader reference so Qt cannot unload callback code after the rename. After Home has been visible for three seconds, confirmation links the parked library back into place without overwriting a new installation, then removes the parked name. It then writes `.adds/nickel-mod-manager/confirmed` with the library's SHA-256 and the firmware identity. A crash, early exit or missing Home leaves the library outside Qt's scan directory. Reinstalling the package retries startup and replaces a previous parked copy.

Later starts with the same library and firmware leave the library in place. NickelHook's failsafe parks a mod when Nickel stops while that mod starts. If the manager's failsafe were armed at the same time, the manager would be parked too, and it could not show that mod at the next start. A missing, damaged or different `confirmed` file arms the failsafe. Removal deletes the file, so a reinstall of the same library is protected again. The failsafe does not catch failures before the Qt callback or after confirmation. A crash caused by another mod during an armed start still leaves the manager parked. After such a failure, reinstall the manager.

## Mod management

At every start NickelModManager lists the `*.so` files in the folder its own library was loaded from. It leaves out itself, anything that is not a regular file, and `libkimg.so`, Kobo's own image plugin, which firmware 4.x ships in the same folder. On 6.0.276679 that folder holds no stock plugins. Every other library there counts as a mod. A table in `src/mods/mod.cpp` gives 25 known libraries their project names, for example `libnm.so` shows as NickelMenu and `libnickelclock.so` as NickelClock. The library names come from each project's `Makefile`, found through NickelHook's list of mods and a GitHub search for projects that use `NickelHook.mk`. Other mods show their file name without `lib` and `.so`.

Mods carry no version. Each row shows the build date, the size and the file name instead, such as "12 Jun 2026 · 412 KB · libnm.so". The size comes from the installed library, or from the held or saved copy when the mod is off. The build date is the library's modification time. Kobo's package step keeps file times from the archive, so it is when the author built the package, close to a release date; NickelModManager keeps it on every copy. NickelModManager also records an install date: when it first saw those bytes, which covers updates too. The list does not show it. Mods that were already there at NickelModManager's first start have no install date.

NickelModManager copies each mod to `.adds/nickel-mod-manager/mods/` when it first sees the mod, and again when the bytes change, for example after the mod's own installer updated it. `state.json` next to that folder records, per file, the SHA-256, whether the mod is on and both dates. It also records the firmware identity from the last start.

- **Turn off.** NickelModManager checks that the copy holds the same bytes as the library, then deletes the library from the plugin folder and from NickelHook's failsafe, described below. The running Nickel keeps the library it already loaded.
- **Turn on.** NickelModManager copies the kept file back through a temporary file one folder up, then renames it into place. Qt 6 loads any file in a plugin folder, so a half-written copy must never sit there. It refuses a copy whose hash differs from the recorded one.
- **Restart.** While a change waits, the footer shows Restart to apply and the number of changes. The button shows "Restarting now…", waits half a second so e-ink can draw it, then runs `/sbin/reboot`.

Opening the manager reads the plugin folder and NickelHook's failsafe again, so changes made while Nickel runs show up. A library that appears during the session loads at the next start. The check for a firmware update runs only when Nickel starts.

If a mod's own installer puts it back after it was turned off, it counts as on again. If a mod that was on disappears for any reason other than a full firmware update on 5.x or 6.x, something else removed it, such as its own uninstaller. NickelModManager then marks it off with a note and keeps its copy.

### NickelHook's failsafe

While a NickelHook mod starts, its failsafe moves the mod's library out of the way and moves it back after the mod's delay. Upstream NickelHook, which firmware 4.x mods use, renames `imageformats/libnm.so` to `imageformats/libnm.so.failsafe`. Its Qt 6 port moves it one folder up, to `/usr/local/Kobo/libnm.so.failsafe`, because Qt 6 loads any file in a plugin folder. When Nickel stops before the delay ends, the library stays there and the mod no longer runs. Qt 5.2 still loads a held `libnm.so.failsafe` from the plugin folder, but NickelHook then finds itself loaded from its failsafe and does nothing. The Qt 6 port also leaves it there when the mod's own compatibility check fails.

NickelModManager looks in both places:

- If this Nickel process has the library loaded, NickelHook is at work in this start, and the mod counts as on.
- Otherwise NickelHook parked it. The row moves to the Failed to load section. Turning it on asks for confirmation first, then renames the held library back into the plugin folder. It loads at the next start.
- Turning a mod off removes the held file first and the library second, so a failsafe that restores the library later finds nothing to restore.

The pages are styled after Nickel's settings pages, such as Device information, without copying them. The header has a Back arrow, the page title large and centred in Nickel's serif font, and a line below. Section labels are small uppercase text in Nickel's sans-serif font, with a line below. Rows use the serif and are separated by lines. Text and lines are black, because grey looks lighter than Nickel's own UI; size and weight set secondary text apart. Grey only fills the card and marks pressed or disabled controls. Labels and lines run from edge to edge; text keeps a 36-unit inset at 300 ppi.

Manage Mods opens on a list. Below the header, a grey card stands for NickelModManager itself, with an info icon. The icon becomes an exclamation mark when automatic restoration is available but turned off. Firmware 4 keeps the info icon because updates there keep installed mods. The card omits the manager's library filename. Below it come three sections, each with a label and a count: Enabled mods, Disabled mods, and Failed to load for mods that NickelHook parked. A row goes into a section by its state in this start, not by its switch, so a tap never moves it to another section. A section without mods is left out. Each row has the name in the serif, a note when there is one, a line with the build date, size and file name, and a switch. While a change waits for a restart, a bold status at the same size replaces the date, size and file line. Toggling therefore never changes a row's height, and a note stays visible until the window closes, so the rows below do not move. Turning on a mod that failed to load asks for confirmation first. After that its row stays in Failed to load and says "Tries again after restart" until the next start shows whether it loads. While no change waits, the footer says "No changes waiting". The list breaks into pages instead of scrolling. The card opens NickelModManager's About page. A short description comes first, followed by rows with the version, creator, project URL, build date, library filename and licence. The System updates section sits at the bottom, above the footer. Back leaves that page for the list, and the list for More.

The manager uses the UI library in `src/ui/`: `Header` with Back, `PagedList`, `Toggle`, `ActionButton`, `choose()`, `MenuEntry` and the embedded [Lucide](https://lucide.dev) icons. It builds a page before it shows it, turns one tap into one rebuild, keeps the footer height fixed when a change starts or stops waiting, and builds a new window each time it opens. Each of these avoids a redraw on e-ink. `src/ui/fonts.cpp` selects Rakuten Serif and Rakuten Sans when installed, or Georgia and Avenir Next on older firmware. Titles and rows use the serif; section labels, notes and buttons use the sans-serif. If neither known family is available, selection falls back to Nickel's `DefaultSerif` and `DefaultSansSerif` aliases. Hidden symbol visibility keeps this library's classes apart from those of other mods in the same process.

The About rows show "Label:" on the left and the value on the right, as Device information does. The System updates label and its setting row sit at the bottom of the page, above the footer. The setting's title and explanation have a 12-unit gap at 300 ppi. The explanation uses escaped rich text with 125% line height, while other labels remain plain text.

## Firmware updates

**Firmware 4.x keeps mods.** On 4.46.23836, `/etc/init.d/rcS` extracts `.kobo/KoboRoot.tgz` over `/` and the upgrade script flashes only the boot images. Under `/usr/local/Kobo/imageformats` the update package holds only `libkimg.so`, so mods stay where they are. The cached 4.38 to 4.46 packages hold the same single file. NickelModManager never puts a mod back on 4.x, because a mod missing there was removed on purpose.

**A full update on 6.x removes them.** The stock `/etc/init.d/ota` script on 6.0.276679 runs at every boot. It first extracts `.kobo/Kobo.tgz` under `/usr/local/Kobo` and deletes it. Then, in the same boot, it runs stage 1 of `.kobo/update.tar`, which flashes the kernel and boot images and reboots into recovery. Stage 2 runs in recovery and writes `rootfs.img` over `system_a`. Nothing on the system partition survives. This was read from the extracted 6.0.276679 root filesystem and the `driver.sh` in its update archive. The recovery partition's own scripts were not available.

Nothing in the new system reads a package queued before the update started: the old system uses up `.kobo/Kobo.tgz` one boot too early, and the new system runs nothing from user storage. A shutdown script on the old system can still bring a mod back. Stage 1 ends with a reboot into recovery, and that reboot runs the old system's `/etc/rc6.d` shutdown entries after the stock script has already used `Kobo.tgz`. A package queued there survives recovery stage 2, and the stock script applies it on the first boot of the new system. NickelModManager uses this when Restore after firmware updates is on, as described below. The approach comes from an earlier experiment, Nickel Persist, which restored its own library this way.

With the switch off, install NickelModManager's `Kobo.tgz` again after a full update on 6.x. At its first start, NickelModManager compares the firmware identity with the one in `state.json`. The identity is the git revision in `/usr/local/Kobo/revinfo`, with Nickel's version string as a fallback. When it differs, NickelModManager copies every mod that was on back into the plugin folder and leaves the ones that were off alone. A card says which mods came back and offers a restart. They load after it.

A Kobo app update that arrives as `Kobo.tgz` deletes only the files listed in that archive. Mods in `imageformats/` survive it, and NickelModManager has nothing to do.

The first start on other firmware arms NickelModManager's own startup failsafe again. Each mod protects itself through its NickelHook failsafe; see Mods that fail to load.

### Restore after firmware updates

Automatic restoration was verified on a physical Clara Colour during a full reinstall of 6.0.276679. Firmware 5.x remains experimental. The hook accepts only known stock scripts, so this result does not establish compatibility with future update procedures.

This setting is in the System updates section of NickelModManager's page. On firmware 5.x and 6.x it is a switch, on by default, and greyed out when the stock update scripts are not the ones NickelModManager knows. Turning it off writes `.adds/nickel-mod-manager/restore-off`; turning it on again deletes that file. On firmware 4.x the row shows a lock instead of a switch and says the feature is not needed, because system updates there keep installed mods.

While it is on and the stock scripts are known, NickelModManager, a few seconds after Home is up:

- writes `/etc/init.d/nickelmodmanager` and links `/etc/rc6.d/K00nickelmodmanager` and `/etc/rc6.d/S00nickelmodmanager` to it. The script is `src/updates/restore-hook.sh`, embedded in the library; no package carries it.
- keeps `.adds/nickel-mod-manager/restore/Kobo.tgz`, a package with its own library and the kept copies of the mods that are on, with `Kobo.tgz.sha256`, `manager.sha256` and a `manifest` of the inputs. It rebuilds the package a few seconds after Home is up and after each change, and skips the write when the inputs are unchanged. The archive keeps each file's time, so build dates survive.

`rc` runs runlevel 6 differently after a reboot from early boot, where stage 1 reboots: it skips the `K` entries and runs the `S` entries with `stop`. After a normal reboot it runs both. The script acts on `stop` and is safe to run twice. It returns at once when there is no `.kobo/update.tar`, so a normal reboot costs nothing. Otherwise it queues the package only when all of these hold, and logs the first one that fails to `.adds/nickel-mod-manager/restore.log`:

1. The setting is on: `.adds/nickel-mod-manager/restore/` exists. Turning the setting off removes it.
2. `/etc/init.d/ota`, `/etc/init.d/rc`, `/etc/init.d/mount-userdata` and `/usr/libexec/platform/utils.sh` have the SHA-256 of the stock 5.18 or 6.0 scripts. 5.18.270971, 6.0.274403 and 6.0.276679 ship the same first three; `utils.sh` differs on 5.18. A firmware with other scripts may run its update differently, so the hook does nothing there and the switch is greyed out.
3. `/mnt/onboard` is a mounted FAT filesystem.
4. The boot target that `ntx_hwconfig` reports for `BootPartNo` is the number of the recovery partition. The read gets three seconds; the firmware has no `timeout` command, so the script ends a slower read itself. A decimal or `0x` value is accepted; anything else stops the hook.
5. NickelModManager's library has the hash recorded with the package, so a removed or replaced manager is not brought back.
6. There is no `.kobo/Kobo.tgz` yet. One the user put there stays.
7. The package matches its recorded SHA-256 and passes `gunzip -t`.

It then copies the package to a temporary name in `.kobo`, checks the copy's hash, renames it to `Kobo.tgz` and syncs. Nothing else writes `.kobo` during shutdown; the only other writer is the script's own second run, which finds the package and stops.

On the first boot of the new system, the stock `ota` script extracts the package under `/usr/local/Kobo` before Nickel starts. NickelModManager arms its own startup failsafe when the library or firmware identity changed, and writes its shutdown entries again a few seconds after Home is up, because the new system has none. A same-version reinstall also restores mods; the shutdown hook does not require a version change. Mods that were off stay off; they are not in the package.

Turning the switch off removes the shutdown entries and the package. NickelModManager only removes files that are its own: the script must carry its marker line and the links must point at it. It refuses to write over a script or link that is not its own. Marker-based uninstall turns restoration off and removes these entries before deleting the manager library.

The script has no overall deadline. Its file operations are local, and a hung kernel I/O operation would stop a reboot with or without it.

## Mods that fail to load

NickelModManager does not watch other mods. Each NickelHook mod protects itself: its failsafe moves the library aside while the mod starts, and leaves it there when Nickel stops before the mod's delay ends. NickelModManager shows such a mod in the Failed to load section. Limits:

- NickelHook protects a mod only during its own failsafe delay. Kobalt, for example, uses 3 seconds. A crash after that delay is not caught.
- A mod without NickelHook has no such protection.

## Checks

Build and run the store, persistence and shutdown-hook tests for both Qt versions with:

```sh
sh tools/build.sh --test
```

Add `--qt 5` or `--qt 6` to check one target. The tests run under QEMU inside the kobuild images, with each SDK's Qt and the container's ARM runtime. The Qt5 run uses a newer glibc than firmware 4.x. These checks do not verify firmware runtime compatibility or device behavior.

`tests/mods_test.cpp` checks the store rules in temporary folders: first start, turning off and on in one session, a mod removed outside the manager, an installer that replaces or restores a mod, a full update with and without putting mods back, both failsafe locations, a rescan, both dates, a changed copy and a damaged `state.json`. It needs only Qt Core. For a host build without UI, configure with `cmake -S . -B build/host -DNICKELMODMANAGER_PLUGINS=OFF`, build with `cmake --build build/host`, then run `ctest --test-dir build/host --output-on-failure`. For older CTest versions, run `ctest --output-on-failure` from the build directory.

`tests/persistence_test.cpp` checks saved-state round trips, exclusion of session flags, filename validation, damaged-state recovery and failed writes. It calls persistence directly without running mod discovery or changing installed libraries.

`tests/hook_test.cpp` checks the shutdown entries and the package in a temporary system: installation, executable mode, the link targets, the package's hashes and contents with the host `tar`, build dates in the archive, the skipped rewrite, entries put back after an update removed them, removal, refusal of a script or link that is not its own, and refusal on unknown stock scripts. It also checks that the library and `src/updates/restore-hook.sh` list the same stock hashes. Its package also passes `gunzip -t`, `tar -tf` and `tar -xzf` from the BusyBox of 6.0.276679, which the stock `ota` script uses.

`tests/hook_script_test.cpp` runs the embedded shutdown script against temporary fixture systems. By default, it replaces the accepted hashes in the temporary script with hashes of synthetic stock files. The installed script is unchanged. It covers queuing once over two runs, boot target formats, a normal reboot, the setting off, unknown stock scripts, unmounted storage, a changed manager, a changed or damaged package, and an existing package. It needs `sha256sum` and `gunzip`; CTest reports a skip if they are unavailable.

To use real stock scripts on a Linux host, set `FIRMWARE_ROOT` to an extracted 5.18 or 6.0 root filesystem when running `nmm_hook_script_test`. In this mode, the test uses the production hash checks without substitution. The container build uses synthetic fixtures and needs no firmware files.

`tests/ui_test.cpp` checks the fixed icon paths, mirrored chevron, finite coordinates, view-box bounds, embedded licence and font selection for legacy, modern, mixed and unknown firmware font sets. The former converter's malformed-input tests are no longer needed because the project does not parse SVG at build time or runtime.

`tests/lifecycle_test.cpp` checks first-run marker creation, confirmation, arming only for a new library or firmware, damaged or missing confirmation records, both uninstall triggers, settings-folder deletion, shutdown-hook cleanup, preservation of other mods and saved state, reinstall after removal or interrupted startup, refusal to overwrite a newer package, and cleanup failures. These filesystem tests do not establish Qt's plugin-loading behavior on firmware.

A normal `sh tools/build.sh` excludes tests and probes. `--test` enables tests and runs them through CTest under QEMU. `--probes` enables the separate probe packages. Direct CMake builds default to including tests through `BUILD_TESTING`; use `-DBUILD_TESTING=OFF` for a library-only build.

## Continuous integration and releases

`.github/workflows/checks.yml` runs on every push to `main` or `develop` and on pull requests. It runs `sh tools/build.sh --qt 5 --test` and `sh tools/build.sh --qt 6 --test`, the same commands as a local build. It uploads Qt5 `KoboRoot.tgz` as the `NickelModManager` artifact and Qt6 `Kobo.tgz` as `NickelModManager-Qt6`. Each upload fails if its package is missing.

A tag build passes the tag to `tools/build.sh` as the version, so the released library shows it on the details page. To release, rename the `Unreleased` section in `CHANGELOG.md` to the chosen version, such as `## vX.Y.Z`, and push the matching tag. `.github/workflows/release.yml` waits for both build and test jobs, downloads both artifacts, refuses a tag without release notes, and publishes `KoboRoot.tgz` and `Kobo.tgz`. A missing package fails the release. Both packages contain only the manager library, with the paths required by their firmware; they do not include other mods.

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

On 2026-10-09, NickelModManager and NickelHome's Qt6 build were installed on a physical Clara Colour running 6.0.276679. The tester confirmed that both worked and automatic restoration was on. The device then reinstalled the same firmware from its full `update.tar` without reinstalling either mod manually. The tester confirmed that both still worked afterwards.

Inspection over USB confirmed that `update.tar` and the queued `Kobo.tgz` were consumed. The hook log first recorded `0x0A` with recovery partition 11 and skipped queuing. During the update's recovery reboot it recorded `0x0B`, queued the package, and kept it on the second invocation. NickelHome logged a post-update startup with its hooks resolved. The manager's confirmation record was present, and its rebuilt restore archive passed its hash check and contained both libraries. This verifies restoration through the physical recovery process for this same-version reinstall.

Additional hardware checks remain:

- Full updates to a different firmware version and other Qt6 device models.
- Disabled mods staying off after an update.
- Mod toggles, failed-mod retry, interrupted startup and deletion-marker uninstall on Qt6 hardware.
- The manager's restart button and normal device power-off.
- Firmware 5.x installation and restoration.

The successful update does not test interrupted writes or power loss on the device's FAT filesystem. The recovery scripts themselves have not been inspected.

### Additional device coverage

Qt5 has been tested on physical devices. This does not cover every device and firmware combination. Stock-firmware runtime checks on Elipsa 2E 4.38.23697 passed for 227 ppi layout geometry, About navigation, touch controls, mod enable/disable across restarts and deletion-marker removal. The density check uses the screen's shorter edge because Qt initially reports landscape dimensions before Nickel sets portrait orientation. Physical Elipsa 2E touch and e-ink behavior still need a device check.

## History

NickelModManager started as a cut-down sibling of NickelLoader, a larger loader prototype with an SDK, a shared hook engine, compatibility checks and quarantine. That prototype is shelved: hook conflicts belong in NickelHook, and checks before load are covered by NickelHook's failsafe and each mod's own checks. The UI library in `src/ui/` comes from it.

## Licence

NickelModManager is released under the [MIT licence](LICENSE). The Lucide icons in `src/ui/icons/` keep their own [ISC and MIT notice](src/ui/icons/LICENSE), which the library also embeds.
