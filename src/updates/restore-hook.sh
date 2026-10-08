#!/bin/sh
# NickelModManager: put the manager and the mods that are on back after a
# full firmware update on 5.x and 6.x.
#
# rc runs this in runlevel 6 as K00 and S00, both with "stop". Stage 1 of a
# full update sets the boot target to recovery and reboots. That reboot runs
# this after the stock ota script has used up .kobo/Kobo.tgz for this boot.
# A package queued here waits through recovery stage 2, and the stock ota
# script applies it on the first boot of the new system.
# A full same-version reinstall passed on Clara Colour 6.0.276679.
#
# Rejected checks exit 0 so rc can continue the reboot. File I/O has no
# overall deadline. The library writes this file; it is not part of a package.
#
# Tests pass --fixture=ROOT: every path is under ROOT, the boot target comes
# from ROOT/boot-target with recovery as 7, and ROOT/mounted stands for a
# mounted FAT user storage.

[ "$1" = stop ] || exit 0
root=
# Fixture paths affect tests only; device runs use the real absolute paths.
case "$2" in --fixture=/*) root=${2#--fixture=} ;; esac
kobo=$root/mnt/onboard/.kobo
state=$root/mnt/onboard/.adds/nickel-mod-manager
restore=$state/restore

# A normal reboot has no staged update. Say nothing then.
[ -f "$kobo/update.tar" ] && [ ! -L "$kobo/update.tar" ] || exit 0

log() {
    [ -d "$state" ] && [ ! -L "$state/restore.log" ] || return 0
    echo "$(date -u +%Y-%m-%dT%H:%M:%SZ) hook: $*" >> "$state/restore.log"
}
hash() { sha256sum "$1" 2>/dev/null | cut -d ' ' -f 1; }
skip() { log "SKIP $*"; exit 0; }

# The preference marker wins even if a previous cleanup left a package behind.
[ ! -e "$state/restore-off" ] && [ -d "$restore" ] && [ ! -L "$restore" ] || skip "restore after updates is off"

# The stock scripts this relies on, as shipped by 5.18 and 6.0. Unfamiliar
# scripts may run the update differently, so they disable the hook.
[ "$(hash "$root/etc/init.d/ota")" = 60ce6320db4a065be8f37e5c3fa3bccb5a54948c261dd980fde7aba0611a8448 ] &&
[ "$(hash "$root/etc/init.d/rc")" = dc914f7bac4ec44ed39fc95b34d6250d6a8573633b5a7e16f74f0ff533d34312 ] &&
[ "$(hash "$root/etc/init.d/mount-userdata")" = b2ad669790d17cbd8aa244b321e43b96dac9f33a91d294f8c65ed050cc626f5f ] ||
    skip "unfamiliar stock scripts"
case "$(hash "$root/usr/libexec/platform/utils.sh")" in
    2f3df68977747b2e2f6a4f00097db1a84a172d47926fb59bf9ebfcd1f13fe8ad|91d260ec9fa0b6453f0ed8511e90e85a8cfae94040e00f416e206b6842262bf1) ;;
    *) skip "unfamiliar stock scripts" ;;
esac

# User storage must be the mounted FAT partition, not an empty mount point.
if [ -n "$root" ]; then
    [ -e "$root/mounted" ] || skip "user storage is not mounted"
else
    grep -q '^[^ ]* /mnt/onboard vfat ' /proc/mounts || skip "user storage is not mounted FAT"
fi

# Only the reboot into recovery that stage 1 asks for qualifies.
if [ -n "$root" ]; then
    recovery=7
    selected=$(cat "$root/boot-target" 2>/dev/null)
else
    recovery=$(readlink -f /dev/disk/by-partlabel/recovery)
    # Compare partition numbers, not device names such as mmcblk0p7.
    recovery=${recovery##*[!0-9]}
    output=$state/.boot-target
    rm -f "$output"
    /usr/bin/ntx_hwconfig -S 1 -p /dev/disk/by-partlabel/hwcfg BootPartNo > "$output" 2>/dev/null &
    reader=$!
    # The firmware has no timeout command. Three seconds bound the read.
    for wait in 1 2 3; do
        kill -0 "$reader" 2>/dev/null || break
        sleep 1
    done
    kill "$reader" 2>/dev/null
    selected=$(cat "$output" 2>/dev/null)
    rm -f "$output"
    log "boot target raw=$selected recovery=$recovery"
fi
case "$recovery" in ''|*[!0-9]*) skip "no recovery partition number" ;; esac
# Validate before arithmetic so unexpected command output cannot become a
# shell expression. Accept decimal and hexadecimal boot targets.
case "$selected" in
    0x*) case "${selected#0x}" in ''|*[!0-9a-fA-F]*) skip "unfamiliar boot target" ;; esac ;;
    ''|*[!0-9]*) skip "unfamiliar boot target" ;;
    # Shell arithmetic reads a leading zero as octal.
    *) selected=${selected#"${selected%%[!0]*}"}; selected=${selected:-0} ;;
esac
[ $((selected)) -eq $((recovery)) ] || skip "boot target is not recovery"

# The manager must still be installed as it was when it made the package.
[ "$(hash "$root/usr/local/Kobo/imageformats/libnickelmm.so")" = "$(cat "$restore/manager.sha256" 2>/dev/null)" ] ||
    skip "NickelModManager is missing, changed or parked"
# Preserve user-staged packages, including a dangling symlink at that path.
[ -e "$kobo/Kobo.tgz" ] || [ -L "$kobo/Kobo.tgz" ] && skip "an existing Kobo.tgz is kept"
package=$restore/Kobo.tgz
[ -f "$package" ] && [ ! -L "$package" ] || skip "no restore package"
[ "$(hash "$package")" = "$(cat "$restore/Kobo.tgz.sha256" 2>/dev/null)" ] || skip "restore package changed"
gunzip -t "$package" 2>/dev/null || skip "restore package is damaged"

# Nothing else writes .kobo during shutdown, so the check before the rename
# only races with this script's own second run, which finds the package.
part=$kobo/.Kobo.tgz.nickelmodmanager
# ota recognizes Kobo.tgz. Keep the incomplete copy under another name until
# its bytes match the prepared package and the filesystem has been synced.
rm -f "$part"
if cp "$package" "$part" && sync && [ "$(hash "$part")" = "$(cat "$restore/Kobo.tgz.sha256")" ] &&
    [ ! -e "$kobo/Kobo.tgz" ] && mv "$part" "$kobo/Kobo.tgz" && sync; then
    log "QUEUED Kobo.tgz with NickelModManager and the mods that are on"
else
    rm -f "$part"
    log "FAILED to queue Kobo.tgz"
fi
exit 0
