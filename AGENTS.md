# NickelModManager development

Read `README.md` and `DEVELOPER.md` before working here. The workspace instructions also apply.

## Source conventions

Use upstream [NickelHook](https://github.com/pgaskin/NickelHook) and [NickelMenu](https://github.com/pgaskin/NickelMenu) as references for source readability and comments. Keep the existing C++ and Qt design.

- Use braces for branches and loops. Put separate operations on separate lines and use blank lines between steps. Prefer descriptive names to abbreviations.
- Keep methods short enough to scan. Let a method show the sequence of work and move detailed operations into clearly named helpers when that makes the sequence easier to follow. Do not impose a fixed line limit or split code into trivial wrappers.
- Comment methods whose purpose, reason or gotchas are not obvious. Explain what the method does and, more importantly, why it is needed. Include ordering constraints, side effects and other gotchas where they matter. Simple, self-explanatory methods do not need a comment.
- Put public function contracts in headers when needed. Start the comment with the function name. State inputs, return values, failure behavior and ownership or lifetime rules where they matter. Document private helpers near their implementations. Avoid duplicating the same explanation in both places.
- Document partial changes on failure. Do not imply rollback when completed file operations remain after an error. Say when a function does not return persistence or cleanup errors.
- Use short trailing comments for simple fields. Explain state flags in more detail when their meaning differs from what the name suggests, such as `enabled`, `loaded` and `parked`.
- Put implementation comments beside the code they explain. Describe reasons, ordering constraints and firmware or Qt quirks. Avoid repeating the function contract or narrating obvious statements.
- Use simple `// --- section name` markers for substantial groups of related code in larger implementation files. Do not add a marker for every function.
- Keep comments in plain language. Use the same word for the same concept. Remove stale build history and unsupported claims. Mark device behavior that still needs verification.
- Keep mod management separate from saved-state persistence. Make that separation visible in filenames and interfaces. Persistence reads and writes snapshots; it does not enable, disable or restore installed libraries.
- Use `NickelModManager` as the C++ namespace. Prefer the full project name to shorthand.
- Group related modules in subfolders by responsibility, such as `src/mods/`. Drop filename prefixes that repeat the folder name.
- Keep modules grouped by responsibility. Add a helper or split a file when it makes the code easier to follow; file size alone is not a reason to split it.
- Update comments when behavior changes. Keep return values, lifetime rules and descriptions of state consistent with the implementation.
- Avoid JSON files where a simpler format meets the need. Prefer plain text or INI for settings and saved state. Check build metadata requirements before replacing their format. Preserve state validation, atomic writes and damaged-file recovery when changing persistence.

## Builds and checks

Use the public kobuild SDK images through `tools/build.sh` for public Kobo builds. Keep the shared source compatible with C++14, Qt 5.2.1 for firmware 4.x and Qt 6.5 for firmware 5.x/6.x. Put reusable Qt API compatibility helpers in `src/compat.h`.

Run checks appropriate to the change. `sh tools/build.sh --test` builds and runs the store, persistence, hook, hook-script, lifecycle and icon tests for both Qt targets. These QEMU tests do not establish firmware runtime compatibility or physical-device behavior; the Qt5 test runtime uses a newer glibc than firmware 4.x.

Use Libra Colour 4.42.23033 and Libra 2 4.38.23697 as the Qt5 compatibility baselines. Keep entry-point, uninstall, startup failsafe and mod-management behavior working on both. Prefer Qt APIs over private Nickel symbols so new firmware checks do not require a symbol port first. Hardware checks remain necessary for touch, e-ink and interaction with installed mods.

For comment-only changes, check that executable tokens are unchanged and run `git diff --check`. Run `sh -n src/updates/restore-hook.sh` when editing that script's comments. A runtime test rerun is not needed when executable content is unchanged.

Keep `README.md` short and user-facing. Put build commands, source layout, tests and technical behavior in `DEVELOPER.md`, and update it when relevant code changes. Keep project tooling in CMake, shell or C++; do not add Python dependencies. Keep tests under `tests/` and register them with CTest. Group UI in `src/ui/`, firmware restoration in `src/updates/`, and plugin registration, startup, uninstall and failsafe behavior in `src/entrypoint/`. Keep controller behavior separate from Nickel widget integration. Prefer Qt APIs and widget inspection to private Nickel symbols. Keep deletion-based uninstall and the startup failsafe available independently of the manager UI.

## Maintain these rules

Amend this file as we agree on more source, structure, build or testing conventions during development. Update the relevant rule in the same change that establishes it. Replace rules that no longer apply rather than accumulating contradictory instructions. Record durable conventions here; keep task progress and temporary diagnostics out of this file.
