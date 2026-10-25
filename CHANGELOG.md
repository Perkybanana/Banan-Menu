# Changes

## 1.0.0-dev.3 — development candidate, 2026-10-25

- Added World → Open enemy spawner, using the game's existing Blueprint cheat-manager panel entry. The action is disabled without a validated playable world, controller/player ownership and expected native signature, and revalidates after queued dispatch and cheat-manager creation. The overlay closes on the render thread to hand input to the native panel; Insert/F8 still reopens it. Panel visibility and actual enemy spawning are pending gameplay acceptance.
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
