<p align="center"><img src="https://raw.githubusercontent.com/Perkybanana/Banan-Menu/development/assets/banan-hero.svg" alt="Banan Menu — Hellraiser: Revival. Insert / F8." width="100%"></p>
<h1 align="center">Banan Menu</h1>
<p align="center">An in-game menu for <b>Clive Barker’s Hellraiser: Revival</b> on Windows.<br>Player controls, inventory tools, photo tools, chapter selection and enemy spawning.</p>
<p align="center"><b>English</b> · <a href="README.ru.md">Русский</a></p>
<p align="center">
<a href="https://github.com/Perkybanana/Banan-Menu/releases/latest"><img src="https://img.shields.io/github/v/release/Perkybanana/Banan-Menu?label=latest%20stable&amp;color=ac6ee2" alt="Latest published stable release"></a>
<a href="https://github.com/Perkybanana/Banan-Menu/actions/workflows/build.yml"><img src="https://github.com/Perkybanana/Banan-Menu/actions/workflows/build.yml/badge.svg?branch=development" alt="Windows build workflow"></a>
<a href="docs/VERIFICATION.md"><img src="https://img.shields.io/badge/tested%20game-1.1.1.227384-262631" alt="Tested Steam game build 1.1.1.227384"></a>
<img src="https://img.shields.io/badge/platform-Windows%20x64-262631" alt="Windows x64">
<a href="LICENSE"><img src="https://img.shields.io/badge/license-MIT-ac6ee2" alt="MIT license"></a>
</p>
<p align="center"><a href="#installation">Installation</a> · <a href="#features">Features</a> · <a href="#usage">Usage</a> · <a href="#screenshots">Screenshots</a> · <a href="#faq">FAQ</a> · <a href="#uninstall">Uninstall</a> · <a href="#build">Build</a></p>

**Press Insert or F8 to open the menu.** Source version: **1.0.0**. [Downloads and release notes](https://github.com/Perkybanana/Banan-Menu/releases) identify the builds actually published; the badge tracks the latest published stable release.

## Installation

1. Close the game. In Steam, browse the game's installed files and open `Hellraiser/Binaries/Win64`.
2. Back up any existing mod `winmm.dll`. Download a build from [Releases](https://github.com/Perkybanana/Banan-Menu/releases) and copy **only `winmm.dll`** beside the game executable. Keep the archive's documentation and licenses.
3. Start the game normally through Steam and press **Insert** or **F8**.

**Updating:** close the game, back up the old DLL and replace only `winmm.dll`. Keep `BananMenu.ini` and `BananMenu.presets` to preserve settings, profiles, favorites and item kits. Neither file is included in the download. To roll back, restore your backed-up DLL while the game is closed.

## Features

| Area | Controls |
| --- | --- |
| Player & combat | Health, damage, ammunition, durability, aim and Genesis Configuration powers. |
| Inventory | Money, six carried crafting-material totals (0–9999), item browser, favorites and explicit item kits. |
| Movement & world | Fly/noclip, speed/jump, saved positions, live-checkpoint teleport, time scale and AI controls. |
| Photo tools | Free camera, FOV, HUD visibility and three map-associated camera bookmarks. |
| World spawning | Hellghoul, Exploding Hellghoul, Cambion, Cultist and Runner; explicit batches of 1–5 through native enemy initialization. |
| ESP | Enemies, items, containers and save points. |
| Progress & settings | Native chapter selection, current objectives, shop level, Steam achievements, local profiles, language, scaling and hotkeys. |

Controls depend on the current map and player. Camera bookmarks last for the current game session; Restore is blocked on a different map. Item kits use native capacity/drop behavior. Live-checkpoint teleport moves to a loaded checkpoint; it does not prepare story state. Cambion spawning uses the final combat phase, rather than recreating the scripted boss encounter. Count 5, concurrent boss HUDs and every map/encounter have not been exhaustively tested. See the [recorded acceptance scope](docs/VERIFICATION.md).

## Usage

- Open a category and explicitly apply the action you want. Nothing spawns automatically.
- Reopen the native inventory after setting crafting totals to refresh its displayed counts.
- Save a camera view, move/rotate the camera, then use Restore on the same map. These bookmarks do not survive a restart.
- In World, choose an enemy type and count, then Spawn. First loading a class may pause briefly. Readiness is asynchronous and can take up to 15 seconds; the status reports completed initialization and model readiness. Kill nearby skips incomplete targets.
- Save settings profiles and item kits locally. Loading a profile changes its switches/values; one-shot actions still require an explicit click.

## Screenshots

Real, unmodified game captures. The menu shown is the accepted development build; these images do not claim a new 1.0.0 installation or additional runtime coverage.

![Hellraiser: Revival main menu](https://raw.githubusercontent.com/Perkybanana/Banan-Menu/development/assets/screenshots/main-menu.jpg)

![Native chapter selection](https://raw.githubusercontent.com/Perkybanana/Banan-Menu/development/assets/screenshots/episode-selection.jpg)

![Banan Menu dev.9 Player tab at the main menu; no active player](https://raw.githubusercontent.com/Perkybanana/Banan-Menu/development/assets/screenshots/banan-menu-dev9.jpg)

![Banan Menu dev.9 Player tab at the main menu; no active player](https://raw.githubusercontent.com/Perkybanana/Banan-Menu/development/assets/screenshots/banan-menu-dev9.jpg)

## FAQ

**The menu does not open.** Check DLL placement beside the game executable, then try both Insert and F8. A game update or another proxy DLL can affect compatibility. Include your game build and mod version when [reporting a problem](https://github.com/Perkybanana/Banan-Menu/issues).

**Can I start at any checkpoint?** Native chapter selection is retained. Arbitrary checkpoint starts were canceled; live-checkpoint teleport is a separate tool.

**Are there restored cut weapons or extra powers?** None are claimed. The [bounded content investigation](https://github.com/Perkybanana/Banan-Menu/blob/development/docs/CONTENT-RESEARCH.md) found no demonstrated ready-to-enable additional player weapon or power.

**Where are settings stored?** `BananMenu.ini` and `BananMenu.presets` are local files beside the game executable. Keep them when updating.

**Is a download verified?** Compare its DLL SHA-256 with its release manifest. Scan evidence applies only to the exact hash recorded in release notes or [verification](docs/VERIFICATION.md); a rebuilt DLL needs its own scan. Offline checks do not replace in-game testing.

## Uninstall

Close the game and remove the mod's `winmm.dll`, or restore the DLL you backed up. You may keep the settings and presets for a later reinstall, or remove them separately.

## Build

Windows x64, MSVC C++/MASM and CMake 3.25+ are required. Initial configuration downloads pinned, hash-checked Dear ImGui and MinHook sources. No game installation, saves or credentials are needed for the offline tests. The menu makes no web requests at game runtime.

```powershell
cmake -S . -B build -A x64 -DKM_DUMPER=OFF
cmake --build build --config Release --parallel 1 -- /nodeReuse:false
ctest --test-dir build -C Release --output-on-failure
powershell -NoProfile -File scripts/package.ps1 -BuildDirectory build -OutputDirectory packages
```

Commit source and package inputs before packaging. The packager accepts stable `1.0.0` or development `1.0.0-dev.N`, the normal Release DLL with dumper disabled, and an exact eight-file documentation/license allowlist. It verifies every archived entry against its input and writes `BananMenu-1.0.0-win64.zip` plus a JSON manifest containing version, source commit, entry sizes/hashes and archive hash. Existing outputs require explicit `-Force`. The eight-entry package excludes settings, presets, tests and screenshots; view images in the [online README](https://github.com/Perkybanana/Banan-Menu#screenshots).

The [Windows workflow](https://github.com/Perkybanana/Banan-Menu/actions/workflows/build.yml) builds, tests and uploads artifacts; it does not publish releases. Contributions must keep mutations on the game thread, render from copied snapshots and place user-facing RU/EN strings in `src/lang.cpp`.

## Credits & licenses

Based on [Ketamine Menu](https://github.com/literalynn/Hellraiser-Revivals-Ketamine-Menu), upstream commit `3b4ba3efc7ecc8c01f2d59b67ff64e4c3fda642f`. Upstream copyright and the [MIT license](LICENSE) are preserved. See [third-party notices](THIRD-PARTY-NOTICES.txt) and the [Inter font license](assets/fonts/Inter-OFL.txt), all included in the binary archive.

[Changelog](CHANGELOG.md) · [Verification](docs/VERIFICATION.md) · [Issues](https://github.com/Perkybanana/Banan-Menu/issues)
