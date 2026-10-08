# NickelModManager

NickelModManager adds Manage Mods to Nickel's More menu. It lists installed mods and lets you turn them off and on. Changes apply after a restart. It keeps copies of your mods on user storage and can restore enabled mods after supported firmware updates.

It supports firmware 4.x, 5.x and 6.x. It has not been tested on a physical device yet. Firmware restoration and e-ink behavior still need device verification.

## Install

Copy the package for your firmware to `.kobo/` on the eReader, eject it and restart.

| Firmware | Package |
| --- | --- |
| 4.x | `KoboRoot.tgz` |
| 5.x and 6.x | `Kobo.tgz` |

Open More, Manage Mods to change which mods load at the next start. NickelModManager itself cannot be turned off from this screen. Its card opens the Restore after firmware updates setting.

Ordinary Nickel plugin mods need no changes to work with the manager. Each mod remains responsible for its own compatibility checks and hooks.

## Remove

Turn on any mods you want to keep using first. Connect by USB, delete `.adds/nickel-mod-manager/uninstall`, eject and restart. You can instead create an empty `.adds/nickel-mod-manager/uninstall-now` file and restart. NickelModManager removes its library and update-restoration hook. Other installed mods stay installed. Saved settings and mod copies stay on user storage; mods you turned off stay off.

To reinstall, copy the package again. The uninstall marker is created again on the first start. If the first start after installing, updating or a firmware update fails before Home has been up for three seconds, the manager stays disabled by its failsafe. Reinstalling retries startup.

## Development

See [DEVELOPER.md](DEVELOPER.md) for builds, source layout, tests, recovery behavior and device verification.

## Licence

NickelModManager uses the [MIT licence](LICENSE). Its Lucide icons retain their [ISC and MIT notice](src/ui/icons/LICENSE).
