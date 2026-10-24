# Banan Menu for Hellraiser: Revival

**1.0.0-dev.1 is a development build for the Windows Steam game, not a verified stable 1.0.0 release. The independent full checkpoint library is unavailable: it has not been implemented, prepared or verified.** Camera bookmarks have passed source/build tests; runtime acceptance is pending.

[Русская версия](README.ru.md) · [Changes](CHANGELOG.md) · [Verification scope](docs/VERIFICATION.md)

## Features in this source

- Player and combat controls: health, damage, ammunition, durability, aim and Genesis Configuration powers.
- Movement and world controls: fly/noclip, speed/jump, saved positions, teleport to live level checkpoints, time scale, AI controls, free camera, FOV and HUD visibility.
- ESP for enemies, items, containers and save points.
- Inventory: money, six carried crafting-material totals (0..9999), item browser, favorites and explicit item kits; settings profiles with local persistence.
- Progress controls for current objectives, shop level and Steam achievements; native chapter selection remains available. Language, scaling and hotkeys are configurable.
- Three guarded, session-only camera bookmarks are present in the source. Their in-game behavior still needs acceptance.

Available controls depend on the current player/map and supported native game interfaces. Kits request item delivery and may encounter the existing native capacity/drop behavior; they are not atomic equip/loadouts. Teleporting to a live checkpoint does not start a complete story state. Recoil, attack speed, hand visibility, enemy spawning and cut-item functionality remain deferred.

## Install, update and remove

1. Close the game. In Steam, browse the game's installed files and open `Hellraiser/Binaries/Win64`.
2. Back up any existing mod `winmm.dll`. From the development archive, copy **only `winmm.dll`** into that directory, beside the game executable. Keep the archive's documentation and licenses for reference.
3. Start the game normally through Steam. Press **Insert** or **F8** to toggle the menu.

For updates, close the game, back up the existing DLL, then replace only `winmm.dll`. Preserve `BananMenu.ini` (settings) and `BananMenu.presets` (profiles, favorites and kits). The archive includes neither file. Reopen the native inventory after changing crafting totals to refresh its display.

To uninstall, close the game and remove the mod's `winmm.dll`, or restore the DLL you backed up. Settings and presets may be kept for a later reinstall; removing them is optional. To roll back an update, restore your backed-up mod DLL while the game is closed.

## Build and package

Requires Windows x64, an MSVC C++/MASM toolchain and CMake 3.25 or newer. No credentials, user saves or game installation are required to build or run the offline tests. CMake downloads pinned, hash-checked Dear ImGui and MinHook archives during initial configuration; no new dependencies are required. The menu makes no web requests at game runtime.

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel 1 -- /nodeReuse:false
ctest --test-dir build -C Release --output-on-failure
powershell -NoProfile -File scripts/package.ps1 -BuildDirectory build -OutputDirectory packages
```

The packager accepts only the normal Release DLL and the documentation/license allowlist. It produces `BananMenu-1.0.0-dev.1-win64.zip` and a JSON hash/size manifest with the source commit and candidate version. Existing targets cause a failure unless `-Force` is explicitly supplied. Compare the extracted DLL's SHA-256 with the manifest before installation. Build/test success is not in-game acceptance or proof of absence of all malicious code; the new candidate binary has not yet been scanned.

The Windows workflow builds, tests and uploads a development artifact; it does not publish a GitHub Release. For contributions, keep game mutations on the game thread, render from copied snapshots, and add user-facing RU/EN text to `src/lang.cpp`. Run the build and tests above and describe the actual runtime verification performed.

## Source and licenses

Based on [Ketamine Menu](https://github.com/literalynn/Hellraiser-Revivals-Ketamine-Menu), commit `3b4ba3efc7ecc8c01f2d59b67ff64e4c3fda642f`. Upstream copyright and [MIT license](LICENSE) are preserved. See [dependency notices](THIRD-PARTY-NOTICES.txt) and the [Inter font license](assets/fonts/Inter-OFL.txt). These notices are included in the binary archive.

Game binaries/assets, saves, logs, dumps, personal paths and credentials are not distribution inputs.
