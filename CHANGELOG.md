# Changes

## 1.0.0-dev.1 — development candidate, 2026-10-24

- Set numeric project version to 1.0.0 and displayed/package version to 1.0.0-dev.1. This is not a verified stable release.
- Added exact carried totals for six crafting materials (0..9999); the native inventory refreshes when reopened.
- Added local settings profiles, favorites and explicit item kits. Kits request delivery under native capacity/drop behavior; they are not atomic equip/loadouts.
- Preserved native chapter selection and corrected return to the main menu. Verification included Chapter5, not every chapter independently.
- Added three guarded session camera bookmarks with explicit Save, Restore view and Delete view. Source/build tests pass; runtime acceptance remains pending.
- Added EN/RU installation/build documentation, an allowlisted development packager with a hash manifest, and a build/test/artifact workflow.
- Independent full checkpoint pack remains unimplemented, unprepared and unverified. Live-checkpoint teleport does not establish story state. Recoil, attack speed, hand visibility, enemy spawning and cut-item functionality remain deferred.

Based on the local Banan Menu 0.4.16 baseline and upstream Ketamine Menu commit `3b4ba3efc7ecc8c01f2d59b67ff64e4c3fda642f`; upstream MIT and dependency notices are preserved.
