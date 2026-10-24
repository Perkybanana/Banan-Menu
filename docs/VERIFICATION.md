# Development verification scope

This records the scope of 1.0.0-dev.1 development work as of 2026-10-24. It is not stable-release acceptance.

| Area | Evidence and limits |
| --- | --- |
| Six carried crafting resources | User accepted totals 0..9999. Native inventory refreshes on reopen. |
| Profiles, favorites, explicit item kits | User accepted after returning to the main menu and restarting the game on 2026-10-24. Kits request delivery with existing native capacity/drop behavior; no atomic equip/loadout claim. |
| Native chapter selection and return to main menu | Tested including Chapter5. Every chapter has not been independently tested. |
| Camera bookmarks | Source review and build tests pass. Runtime acceptance is pending, including all three slots, pose/FOV, movement after restore, camera/pawn/map transitions, no player teleport and RU/EN display. Bookmarks last only for the process session. |
| Live-checkpoint teleport | Moves to an existing loaded checkpoint; not a complete story-state start. |
| Independent full checkpoint pack | Not implemented, prepared or verified. |
| Recoil, attack speed, hand visibility, enemy spawning, cut-item functionality | Deferred; no implemented-functionality claim. |
| Frozen candidate binary | Controller scanned SHA-256 `ea63783deb724251c1505faa657a0fcd0a214468d2e8458150cd4212cf750d8f` with Microsoft Defender on 2026-10-24: exit 0, no threats reported. This covers only that exact DLL, not future CI builds. Offline tests and this scoped scan cannot guarantee absence of all malicious code or establish runtime acceptance. |

The build's offline tests cover resource bounds, preset persistence/validation, proxy exports, achievement helpers, reflection-cache handling and camera bookmark/generation guards. They do not replace runtime acceptance. No game installation or saves are needed for these tests; the fake Steam DLL is an offline test fixture and is excluded from packages.

All game mutations belong on the game thread; the render thread consumes copied snapshots. Development packaging uses a fixed allowlist and checks the archive DLL against the selected Release DLL. The JSON manifest contains only relative entry names, sizes, hashes, version and source commit identifiers. It includes the archive hash for transfer verification. Source-only bundles should be made with `git archive` from a reviewed commit, never by recursively zipping a working directory.
