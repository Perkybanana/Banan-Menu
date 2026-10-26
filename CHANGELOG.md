# Changes

## 1.0.0-dev.7 — development candidate, 2026-10-26

- Dev.6 passed ownership preflight but failed native-default validation before spawn dispatch. The null-input native builder left the optional hearing-curve override uninitialized. Free-spawn requests now explicitly select no override in the owned seed after reflected type, class, shape and bounds checks; all other native-default and lifecycle guards remain.
- Added production preparation regressions and retained field/stage diagnostics for failed cached defaults. Removed unsupported version/restart advice. Native spawning, visibility, AI and Genesis behavior remain pending gameplay acceptance.

## 1.0.0-dev.6 — development candidate, 2026-10-26

- Corrected reflected array-element metadata decoding after dev.5 rejected enemy-manager ownership before native spawn dispatch. The same fix serves manager enemy arrays, correlation tags and native spawn defaults; existing ownership, readiness and deadline guards remain in place.
- Added production reflection regressions for the supported array layout and preserved object/struct/enum decoding. Actual enemy spawning, visibility, AI and Genesis behavior still require gameplay acceptance.

## 1.0.0-dev.5 — development candidate, 2026-10-26

- Replaced dev.4 bare actor construction after runtime failure: spawned Hellghoul/Cultist were invisible and inert; Genesis after Kill nearby crashed with a null skinned asset. The candidate now requests full native enemy-manager initialization from engine-built defaults, with selected Blueprint settings preflight.
- Added asynchronous request correlation, actual finished-init/model readiness counts, a 15-second timeout, transition/identity guards and duplicate blocking. Kill nearby skips incomplete or model-less enemies and reports skipped counts. No AI/controller, mesh, lifecycle flag or callback is forced.
- Retained one immutable native defaults value and five fixed class conversion values for bounded engine-allocation ownership; dispatch uses the reflected native copy path. Added production state/metadata regressions. All gameplay acceptance remains pending.

## 1.0.0-dev.4 — development candidate, 2026-10-25

- Replaced the nonfunctional native panel with an explicit five-type World spawner, count 1–5, using reflected engine class conversion/loading and deferred actor construction. The menu stays open; nothing preloads/spawns automatically. Obstructed/failed creation stops the batch and reports the completed count. **Failed runtime acceptance; replaced in dev.5.**
- Fixed catalog comes from the game's helper defaults: Hellghoul, Exploding Hellghoul, Cambion, Cultist and Runner. New type/size/signature and operation-local world-ownership checks guard calls without weakening retained generation references. Added production-helper regressions for placement, bounds, signatures and newly created actor lifetimes.
- Corrected native-panel diagnosis: the cheat-manager entry passes a null ObjectRef; the panel requires a configured helper Actor and spawners. Its standalone SpawnChosenEnemies graph also appears to skip count one. Dev.4 uses the verified engine construction primitives directly; it never over-spawns and deletes an extra actor.
- Accepted camera/resources/profiles remain as recorded. Stable 1.0.0, arbitrary checkpoint starts, excluded combat/hand controls and unusual-content research retain their existing status.

## 1.0.0-dev.3 — development candidate, 2026-10-25

- Added World → Open enemy spawner using the existing Blueprint panel entry. **Failed user acceptance on Chapter5: the panel appeared, its type list was empty and Spawn did nothing.** Successful build/CI did not establish gameplay functionality. Dev.4 replaces this path.
- Recorded user acceptance of dev.2 camera Save, move/rotate and Restore on Chapter5 on 2026-10-25, plus separate wrong-map Restore blocking after a menu/chapter transition without a crash. This does not establish all-slot, FOV or death-variant acceptance.
- Arbitrary checkpoint starts remain canceled/outside the current scope; native episode/chapter selection remains. Recoil, attack speed and hand visibility were removed from the current work scope by owner decision. Unusual weapons/possible cut content remain research, without a functionality claim.
- Stable 1.0.0 finalization remains postponed; this is a test candidate.

## 1.0.0-dev.2 — development candidate, 2026-10-25

- Fixed the diagnosed dev.1 camera-bookmark readiness failure: freshly resolved strong camera-component and camera-manager properties can refer to live objects with an unassigned serial. The camera path now validates those targets through their retained positive-generation owner, typed property and live object slot, and re-resolves after native calls. Retained camera/controller/player/world generation guards remain strict.
- Added production-resolver regression tests for zero-serial strong targets and invalid ownership, identity, class and field cases. Source/build tests do not establish gameplay acceptance; dev.2 native camera acceptance remains pending.
- Kept native episode/chapter selection. Arbitrary checkpoint starts and an independent story-state pack/loader are outside the current scope by owner decision. Previously deferred features remain deferred; this is not a verified stable 1.0.0 release.

## 1.0.0-dev.1 — development candidate, 2026-10-24

- Set numeric project version to 1.0.0 and displayed/package version to 1.0.0-dev.1. This is not a verified stable release.
- Added exact carried totals for six crafting materials (0..9999); the native inventory refreshes when reopened.
- Added local settings profiles, favorites and explicit item kits. Kits request delivery under native capacity/drop behavior; they are not atomic equip/loadouts.
- Preserved native chapter selection and corrected return to the main menu. Verification included Chapter5, not every chapter independently.
- Added three guarded session camera bookmarks with explicit Save, Restore view and Delete view. Source/build tests pass; runtime acceptance remains pending.
- Added EN/RU installation/build documentation, an allowlisted development packager with a hash manifest, and a build/test/artifact workflow.
- Live-checkpoint teleport does not establish story state. Recoil, attack speed, hand visibility, enemy spawning and cut-item functionality remain deferred.

Based on the local Banan Menu 0.4.16 baseline and upstream Ketamine Menu commit `3b4ba3efc7ecc8c01f2d59b67ff64e4c3fda642f`; upstream MIT and dependency notices are preserved.
