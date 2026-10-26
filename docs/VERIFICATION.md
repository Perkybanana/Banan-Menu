# Verification scope

Source version **1.0.0**, prepared on 2026-10-26, preserves the gameplay implementation reviewed and accepted in **1.0.0-dev.9**, source commit `3619796053f5194b12dbf0885648e8ac9311687d`. The stable source changes concern version metadata, packaging, CI and documentation. They do not establish a new installation or scan result. Final archive freeze, stable DLL scan/checksum, publication and installation are separate release gates.

The tested installation is the **Windows x64 Steam game, build 1.1.1.227384, DX12**. The genuine dev.9 interface capture confirms that build and renderer; it is taken at the main menu without an active player. DX11 support exists in the overlay source and host test utility, but this game acceptance record is DX12.

## Current gameplay acceptance

| Area | Evidence and limits |
| --- | --- |
| Six carried crafting resources | User accepted totals 0..9999. Native inventory refreshes on reopen. |
| Profiles, favorites, explicit item kits | User accepted persistence across return to main menu and game restart. Kits request delivery under native capacity/drop behavior; they are not atomic equipment loadouts. |
| Native chapter selection and return to main menu | User accepted the persistence/fix, including Chapter5. Not every chapter was tested independently. Arbitrary checkpoint starts were canceled. |
| Camera bookmarks | User accepted dev.2 Save, move/rotate and Restore on Chapter5, then wrong-map Restore blocking after a chapter/menu transition without a crash. Three session-only slots are implemented; not all slots, FOV, death/pawn variants or every RU/EN presentation were exercised. |
| Enemy spawning — ordinary and exploding Hellghoul | Dev.7 visible spawning and Genesis finisher were accepted, while offensive AI was incomplete. Following the dev.8 authored subtype correction, the user reported ordinary Hellghoul and exploding-variant behavior working. This establishes those reported paths, without an exhaustive all-type/all-map combat claim. |
| Fresh Cambion — dev.9 | After dev.8's encounter-phase stall, the user explicitly accepted the requested Chapter5 fresh-Cambion normal health depletion/Genesis check and the separate Kill nearby/finisher check: “everything works, excellent.” Dev.9 source `3619796` prepares only newly correlated, uniquely alive manager-owned Cambions from initialized Phase_1 to verified final Phase_2 once; it does not reproduce the complete scripted boss fight or repair existing stuck/story actors. Native starting effects and stagger/Banish behavior remain intact. |
| Full spawner bounds | Five explicit types (Hellghoul, Exploding Hellghoul, Cambion, Cultist, Runner) and counts 1–5 are implemented through native MakeSpawnInfo/SpawnThreat. Only the selected class loads on click. Correlated finished initialization and a usable model determine readiness; pending status lasts up to 15 seconds. Transition, stale identity, ambiguous ownership, incomplete model and timeout guards remain. Kill nearby skips incomplete/model-less targets. Offline guards do not prove every native encounter context. Count 5, simultaneous boss HUDs, all five combat paths and all navigation/maps have not been separately exhaustively accepted. |
| Live-checkpoint teleport | Moves to an existing loaded checkpoint; it does not prepare a complete story-state start. |
| Excluded work | Recoil, attack speed and hand visibility were removed by owner decision. Arbitrary checkpoint starts are canceled and are not a release gate. |
| Residual weapons and powers | Research of the named candidates is complete with a bounded negative finding: no verified ready-to-enable additional player weapon or Genesis power. No speculative restoration controls were added. See [content research](https://github.com/Perkybanana/Banan-Menu/blob/development/docs/CONTENT-RESEARCH.md). |

## Development history

Dev.3's native helper panel opened with an empty type list and did not spawn. Dev.4's bare actor construction produced invisible/inert enemies; a Genesis execution after Kill nearby crashed with a missing skinned asset. Dev.5 replaced that path with full native manager initialization, owned engine defaults, completion/model checks and private request correlation. Dev.6 corrected reflected array metadata. Dev.7 explicitly selected no optional hearing-curve override after typed metadata checks. Dev.8 supplied each request's fixed authored subtype so native configured abilities could load/grant. Dev.9 prepared the fresh Cambion final combat phase. The historical changelog records each candidate's status at its date; the table above records later user acceptance.

## Offline checks and package contract

The CMake/CTest suite covers resource bounds, preset persistence/validation, proxy exports, achievement helpers, reflection-cache handling, camera bookmark/generation guards, spawn placement/bounds, completion/timeouts/correlation, typed signatures, actor ownership, authored subtypes and Cambion phase protocol. The package version test executes the production parser with stable/development versions and invalid/ambiguous declarations. Offline tests do not execute Unreal combat or replace gameplay acceptance. They require no game installation or saves. The fake Steam DLL and overlay host are test utilities and never enter release packages.

All game mutations remain on the game thread; the render thread consumes copied snapshots. Packaging requires committed source/package inputs, a normal Release DLL containing the exact declared stable/development version, the matching configured source directory, and `KM_DUMPER=OFF`. It rejects unsafe output paths and unintended overwrites. The ZIP contains exactly these eight entries:

1. `winmm.dll`
2. `README.md`
3. `README.ru.md`
4. `CHANGELOG.md`
5. `LICENSE`
6. `THIRD-PARTY-NOTICES.txt`
7. `assets/fonts/Inter-OFL.txt`
8. `docs/VERIFICATION.md`

The packager checks every archived entry's size and SHA-256 against its input. The separate JSON manifest contains relative paths, sizes, hashes, version and source commit, plus archive hash for transfer verification. Settings/presets, screenshots, captures, private dumps, test binaries and proprietary game assets are excluded. Source bundles should use `git archive` from a reviewed commit, never a recursive working-directory ZIP. The build workflow uploads artifacts but never automatically publishes a release.

## Scan evidence

Historical scans remain associated with their exact candidates; they are not relabeled stable scans:

| Candidate | Exact DLL SHA-256 | Recorded result |
| --- | --- | --- |
| Historical 2026-10-24 candidate | `ea63783deb724251c1505faa657a0fcd0a214468d2e8458150cd4212cf750d8f` | Microsoft Defender exit 0, no threats reported on 2026-10-24. |
| 1.0.0-dev.9 / source `3619796` | `54e178dfaa24b48482ddee111ab3a7975fa0b2574df47beace4fb70902f4d5ff` | Exact packaged DLL scanned with Microsoft Defender on 2026-10-26: exit 0, no threats reported; installed DLL matched that hash. |
| 1.0.0 stable | Not recorded at source preparation | Root must scan the final Release DLL, freeze/verify the archive and record exact hashes before publication/installation. |

A different hash or future CI build needs its own evidence. A scoped scan cannot guarantee absence of all malicious code, and neither scanning nor offline tests establish additional in-game acceptance.
