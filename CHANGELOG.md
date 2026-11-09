# Changelog

Each release's section becomes the text of its GitHub release (`tools/release_notes.py`, run by
`.github/workflows/build.yml` when a `v*` tag is pushed). Add the section before tagging: a tag
without one does not publish. Keep each entry on one line: GitHub shows a line break in release
text as a break.

## v0.6

- Setup checks your eboot.bin against the unmodified update 1.10 this release was tested with. For a modified one (a DLC unlocker patch, for example) or another version, it warns that the game may be unstable or crash and asks before going on. All add-ons already show as owned without an unlocker.
- Dumps whose eboot.bin was patched for a DLC unlocker (it imports from a `dlcldr` module) now start, instead of stopping at boot with exit code 20.

## v0.5

- Shaders that interpolate a flat-shaded input are handled as on the PS4's GPU instead of stopping the game (issue #1: a stop with exit code 23 when level 3 loaded after the level 2 boss). The game data corruption behind that crash is still being investigated.

## v0.4

- Sharpening: AMD FSR 1 RCAS over the picture, for crisper edges without halos. On by default; a new Sharpening row in Display Settings (F10), and **F9** switches it on or off during play to compare. The game's own HUD and menus are sharpened along with the scene.
- 144 FPS frame rate, between 120 and 180. A 144 or 165 Hz display now runs at 144 instead of 120.

## v0.3

- Aspect ratios: 21:9 and 32:9 show more of the level to the sides, 4:3 more above and below, nothing stretched. The menus stay 16:9.
- 30 FPS cap for PCs that cannot hold 60, at normal game speed.

## v0.2

- HDR setting: the game's own HDR10 output on an HDR display.
- 90 FPS frame rate (smooth on a 120 Hz display with VRR).
- Faster rendering where the game writes next to data the GPU has written.

## v0.1

- First release: Resogun (PS4, CUSA00038 v1.10) running natively on Windows and Linux.
