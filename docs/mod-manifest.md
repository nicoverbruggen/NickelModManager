# Mod manifests

`mod.manifest` describes a mod and the settings that it exposes to NickelModManager. It does not store the user's setting values. Keep those values in the mod's own config file.

## Implementation status

The native manager reads version metadata, names, descriptions and creators. It edits the boolean settings described below and evaluates software-family conditions. The declaration must match the observed library hash. These features are under development and need release validation before publication.

## Location and syntax

Ship the file in the mod's own immediate `.adds` subfolder. For example, NickelHome uses `/mnt/onboard/.adds/nickel-home/mod.manifest`. Do not use an `.ini` extension.

Use UTF-8 text with `[section]` headers and `key=value` lines. Surrounding whitespace is ignored. Blank lines and full-line `#` or `;` comments are allowed. Values are literal text, without quoting, escaping, interpolation or inline comments. A value can contain `=` after the first separator. Files are limited to 16 KiB. Do not repeat section names or keys. Declare settings in their display order.

## Version metadata

These fields are implemented:

```text
[mod]
schema=1
library=libnickelhome.so
version=0.7
sha256=<SHA-256 of the final library in this package>
```

| Field | Meaning |
| --- | --- |
| `schema` | Manifest format version. Currently `1`. |
| `library` | The mod's `.so` basename. It must match an already discovered mod. Paths are not accepted. |
| `version` | The author's version string. Use at most 128 printable characters. |
| `sha256` | SHA-256 of the final packaged library, after stripping. Generate it separately for each Qt target. |

The manager compares the hash with the installed, held or saved library that it has observed. A stale manifest cannot supply a version for different library bytes. Missing, invalid or ambiguous version metadata leaves the version undisplayed. They do not prevent enabling, disabling or permanently removing the mod. A manifest alone does not create a mod entry.

Optional `name`, `description` and `creator` fields under `[mod]` provide display metadata. The compact list shows the name, version and description. The About tab shows the creator and library. A missing name uses the library-based display name. Names and creators are limited to 128 characters; descriptions are limited to 512. Overlong display fields are ignored.

## Settings declarations

Declare one config file for this first version:

```text
[settings]
config=config
format=colon-bool-first-v1
apply=restart

[setting.hide_home_row3_enabled]
type=bool
label=Hide the bottom Home row
description=Hide the row that shows notices and promotions.
section=Home screen
fallback=0
```

| Field | Meaning |
| --- | --- |
| `config` | A regular file in the declaring mod’s folder. Accept a basename only. `mod.manifest`, `uninstall` and `uninstall-now` are reserved. |
| `format` | The config reader and writer contract. Initially only `colon-bool-first-v1`. |
| `apply` | Default application timing: `restart` or `live`. `live` means the mod reads the config while running; the manager does not call a reload hook. |
| Setting section suffix | The actual config key. For example, `hide_home_row3_enabled`. |
| `type` | The control type. Initially only `bool`. |
| `label` | The visible setting name, at most 256 characters. |
| `description` | Optional explanation below the label, at most 512 characters. |
| `section` | Optional display group, such as `Home screen` or `Advanced`, at most 128 characters. |
| `fallback` | `0` or `1`, used when the key is absent from an existing config. |
| Setting `apply` | Optional `restart` or `live`, overriding the default from `[settings]`. |

`colon-bool-first-v1` accepts regular config files up to 256 KiB without NUL bytes. It uses one `key:value` entry per line. It strips `#` comments and surrounding whitespace. Keys are case-sensitive. The first occurrence of a key wins. Boolean values accept `1/0`, `true/false`, `yes/no` and `on/off`, with case-insensitive words. Missing or empty values use the declared fallback. Invalid values must be shown as invalid; browsing does not repair them. An explicit user edit can replace an invalid value with `0` or `1`.

Use `apply=restart` for a setting that the mod reads only at startup. The editor marks it with “Applies after restart”. Saving adds it to the manager’s restart footer. The footer counts changed settings separately from mod enable/disable changes. It counts each setting once and removes it when a later save restores its value from before the first edit in this process. A new process clears the count. Opening the page or changing an unsaved switch does not count as a saved change. `apply=live` does not add a restart requirement. A mod must actually reload and apply external saves before declaring this mode. Qt6 NickelHome’s live-reload test build watches atomic config replacements; its Qt5 manifest must keep `apply=restart`.

The fallback is not an installation default. NickelHome's template sets three hide options to `1`, but its reader uses `0` when those keys are missing. Read an existing value before applying the fallback. The mod remains responsible for creating its first config. An absent config makes settings unavailable; the manager must not recreate a deleted mod folder or an uninstall marker.

## Conditional availability

Conditions belong to each setting declaration:

```text
[setting.tolino_home_books_enabled]
type=bool
label=Use Kobo book layout
description=Show current read and My Books on Tolino Home.
section=Home screen
fallback=0
when.os=tolino

[setting.hide_home_row2col1_enabled]
type=bool
label=Hide My Books
description=Hide My Books while keeping the tile beside it.
section=Home screen
fallback=0
```

| Condition | Accepted values | Meaning |
| --- | --- | --- |
| `when.os` | `kobo`, `tolino`, or a comma-separated list | The active software family. It is independent of the device's hardware brand. |

Values within `when.os` are alternatives. For example, `when.os=kobo,tolino` matches either software family. Omit `when.os` to show a setting on either family. A Qt6 package may run Kobo or Tolino software, so this condition remains useful within one build target.

When the running software cannot be identified, assume Kobo. This makes `when.os=kobo` match and keeps `when.os=tolino` hidden. This runtime fallback does not make invalid manifest values valid. An unknown `when.*` name, unknown value or empty condition must hide the affected setting and report a declaration error. Never drop an unsupported condition and expose the setting unconditionally.

### Qt-specific manifests

Each Qt package ships its own manifest for its own library. Include only settings supported by that build. For example, include `hide_home_row2col1_enabled` in NickelHome's Qt6 manifest and omit it from its Qt5 manifest. Generate the version and library hash for each target as usual.

There is no `when.qt` field. It would repeat a choice already made when packaging the mod. An older manager that does not understand a future condition must keep the affected setting unavailable rather than ignore the condition.

Hidden settings are not deleted, reset, normalized or written when saving another setting. Do not treat hidden settings as false. Saving requires a fresh check that the selected setting is still applicable. Unchanged config content, including values for other operating systems, remains intact.

These conditions control availability. They do not prove that the mod supports a firmware build. They do not run scripts, inspect arbitrary paths or provide a general expression language. Conditions between settings, such as disabling individual tiles when their entire row is hidden, need a separate declarative contract before native implementation. The prototype currently demonstrates those relationships directly.

## Software-family detection

The native manager queries Nickel's current `Device` through the optional `getCurrentDevice()` and `isTolino()` methods after the UI is ready. A true result maps to `tolino`; a false result maps to `kobo`. Missing methods or a missing current device default to `kobo`. The manifest condition identifiers are lowercase and stable.

Do not infer the software family from the product name, firmware major version, installed fonts or an affiliate setting. Kobo and Tolino can share Qt and firmware versions. Installed font families can coexist. A device may run software with branding different from its hardware. Font selection remains separate from software-family detection.

## Editing and packaging rules

Keep config editing separate from installed-library management and `state.json`. Opening a settings page must not write config. Save only explicit edits, preserve comments and unrelated entries, and update the first effective occurrence of a key. Append it only when absent. Refuse paths outside the declaring folder and symlinked folders or files. Do not accept commands or callbacks in a declaration.

Before saving, recheck the manifest, config and applicable conditions. If either file changed since opening, report a conflict instead of replacing newer edits. Publish a complete edited config through an atomic replacement. A failed save must not claim success. Track settings that need a restart separately from library enable/disable changes.

Generate the manifest from the same final library that goes into the package. Treat it as mod-owned support data. Install or refresh it during upgrades without replacing the user's config. Include it in the mod's uninstall cleanup. On Qt6, embedding another first-run file is not sufficient for existing installations whose support-file receipt already exists; the mod needs an explicit metadata upgrade path.

Older managers can continue reading `[mod]` metadata while ignoring the new settings sections. They must not claim to edit settings that they do not understand. Unsupported manifest schemas remain unavailable. Changes to this contract must be documented here before release.
