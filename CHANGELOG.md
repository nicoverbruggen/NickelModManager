# Changelog

## v0.5 (unreleased)

- Restore the original Manage Mods icon in Nickel’s More menu.
- Keep radio indicators transparent during row presses and match restoration text spacing to update policy rows.
- Use plain divided action footers and lighter internal modal dividers, while keeping black modal outlines.
- Fit manager settings on one page, separate unsaved changes, and remove doubled row and tab borders.
- Give grey section headers a consistent height and vertically centered text.
- Use Kobalt’s Phosphor regular icons, grey section headers and borders on tappable rows.
- Show compact mod metadata with circular settings and info buttons. Open manifest settings from a row or button.
- Edit declared boolean settings with Kobo/Tolino conditions, conflict detection and preserved config content. Count saved settings that require a restart in the restart footer.
- Add Settings and About tabs for Mod Manager, with icons for update policies and mod restoration.
- Add Stock, Always ask and No updates policies. Check Nickel’s update interfaces on startup and keep Stock behavior if they do not match.
- Show Kobo or Tolino software on the manager's About page, defaulting to Kobo when detection is unavailable.
- Document the manifest format and OS-specific settings declarations.
- Press and hold a mod row to permanently remove a disabled mod's saved copy while keeping its settings, with one confirmation.
- Use Kobalt-style interface fonts, pressed feedback and full-width popup actions.
- Show a bold empty-state title and the correct installation package name for each firmware.
- Show versions from optional `mod.manifest` metadata when it matches the mod library.

## v0.3

- Added the Qt6 release package, `Kobo.tgz`, for Kobo 5.18 and 6.0, and Tolino Vision Color 5.18.264769. Firmware 4.x uses `KoboRoot.tgz`.
- Added automatic restoration of the manager and enabled mods after supported full firmware updates on Qt6 firmware, with a 30-second deadline.
- Use Tolino's Bariol font for manager pages. Kobo keeps its Rakuten or legacy fonts.

## v0.2

- Fixed a display issue on Kobo Elipsa and Kobo Elipsa 2E devices.
- Fixed issues with fonts not displaying correctly on older devices.

## v0.1

- Initial release, for firmware 4.23 and newer.
