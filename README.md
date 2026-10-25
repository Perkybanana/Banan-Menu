# Banan Menu for Hellraiser: Revival

**1.0.0-dev.4 is a development candidate for the Windows Steam game, not a verified stable 1.0.0 release.** Dev.3's native spawner panel failed gameplay acceptance: its list was empty and Spawn did nothing. Dev.4 replaces that entry with a fixed five-type spawner; actual spawning and enemy behavior remain pending gameplay acceptance. Native episode/chapter selection is supported; arbitrary checkpoint starts are outside the current scope.

[Русская версия](README.ru.md) · [Changes](CHANGELOG.md) · [Verification scope](docs/VERIFICATION.md)

## Features in this source

- Player and combat controls: health, damage, ammunition, durability, aim and Genesis Configuration powers.
- Movement and world controls: fly/noclip, speed/jump, saved positions, teleport to live level checkpoints, time scale, AI controls, free camera, FOV and HUD visibility.
- ESP for enemies, items, containers and save points.
- Inventory: money, six carried crafting-material totals (0..9999), item browser, favorites and explicit item kits; settings profiles with local persistence.
- Progress controls for current objectives, shop level and Steam achievements; native chapter selection remains available. Language, scaling and hotkeys are configurable.
- Three guarded, session-only camera bookmarks. On 2026-10-25 the user accepted dev.2 Save, move/rotate and Restore on Chapter5, and separately accepted wrong-map Restore blocking after a menu/chapter transition without a crash. Other slots, FOV and death variants have not all been tested.
- World → Enemy spawner offers Hellghoul, Exploding Hellghoul, Cambion, Cultist and Runner, with an explicit count of 1–5. The menu stays open. Only the selected class loads on click; the first load may pause briefly. Placement is in front of the pawn with spacing for multiple actors, and collision checks can block creation. Status counts completed actors, including partial failures; it does not confirm enemy AI/combat behavior.

Available controls depend on the current player/map and supported native game interfaces. Kits request item delivery and may encounter the existing native capacity/drop behavior; they are not atomic equip/loadouts. Teleporting to a live checkpoint does not start a complete story state. Recoil, attack speed and hand visibility were removed from the current work scope by owner decision. Unusual weapons and possible cut content remain research; their functionality is not confirmed.

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

The packager accepts only the normal Release DLL and the documentation/license allowlist. It produces `BananMenu-1.0.0-dev.4-win64.zip` and a JSON hash/size manifest with the source commit and candidate version. Existing targets cause a failure unless `-Force` is explicitly supplied. Compare the extracted DLL's SHA-256 with the manifest before installation. Consult the release notes for the candidate you install and the [recorded scan evidence](docs/VERIFICATION.md). A scan result applies only to its exact recorded DLL hash; a different build needs its own evidence. Scanning does not guarantee absence of all malicious code or establish in-game acceptance.

The Windows workflow builds, tests and uploads a development artifact; it does not publish a GitHub Release. For contributions, keep game mutations on the game thread, render from copied snapshots, and add user-facing RU/EN text to `src/lang.cpp`. Run the build and tests above and describe the actual runtime verification performed.

## Source and licenses

Based on [Ketamine Menu](https://github.com/literalynn/Hellraiser-Revivals-Ketamine-Menu), commit `3b4ba3efc7ecc8c01f2d59b67ff64e4c3fda642f`. Upstream copyright and [MIT license](LICENSE) are preserved. See [dependency notices](THIRD-PARTY-NOTICES.txt) and the [Inter font license](assets/fonts/Inter-OFL.txt). These notices are included in the binary archive.

Game binaries/assets, saves, logs, dumps, personal paths and credentials are not distribution inputs.
