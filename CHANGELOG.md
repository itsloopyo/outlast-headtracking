# Changelog

## [Unreleased]

### Added

- Lean collision: each lean is checked against the game's own line trace and cut to stop `CollisionMargin` (15 cm) short of what the trace hits. It is on by default (`[Position] CollisionEnabled`), and `CollisionChannel` and `CollisionReleaseSmoothing` tune it.
- The tracking mode and yaw mode hotkeys save the mode they switch to in `CameraUnlock.ini`, so the next start begins in it. `End` / `Ctrl+Shift+Y` changes the session only.
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Changed

- Settings move to `Binaries\Win64\CameraUnlock.ini`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A setting that the defaults the README shows set to `default` is written as `default` when you never changed it from the default earlier versions used, because `HeadTracking.ini` does not hold it or holds that default. It then follows `Defaults.ini`, so it takes the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none, which can differ from the default earlier versions used. A setting you changed is written with the value imported for it, or as `default` where that value equals its default at that start.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- A `LimitX`, `LimitY`, `LimitYDown`, `LimitZ` or `LimitZBack` above 10 metres in `HeadTracking.ini`, more than `CameraUnlock.ini` holds, is imported as 10, and the log says so.
- Comments, and keys the mod never read, are not carried over. Nor is a hotkey set to Ctrl, Shift or Alt on its own, where your old file had one. That key goes down before the key of any chord made with it, so the hotkey is left unbound, and it keeps its Ctrl+Shift chord where it has one.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`.
- The development build read no `[Position] CollisionEnabled`, `CollisionMargin`, `CollisionChannel` or `CollisionReleaseSmoothing`, which 2dbcadb added; a `HeadTracking.ini` holding them is now imported with them.
- `uninstall.cmd` leaves `CameraUnlock.ini` and `HeadTracking.ini` in place, so a reinstall keeps your settings. Earlier versions deleted `HeadTracking.ini`.

## [0.0.0] - 2026-09-02

### Added
- Initial release.
- Added head tracking for Outlast over the OpenTrack UDP protocol on port 4242, driven by a webcam, phone, or any OpenTrack compatible tracker.
- Added a tracking toggle on `End` / `Ctrl+Shift+Y`, a three-state mode cycle on `Page Up` / `Ctrl+Shift+G`, and a yaw mode toggle on `Page Down` / `Ctrl+Shift+H`.
- Added world-space and camera-local yaw modes, switchable in game without a restart.
- Added `HeadTracking.ini`, written next to `OLGame.exe` on first run, with any missing key falling back to its default.
- Added a `FieldOfView` override for a game with no field-of-view setting in its menus, accepting 20 to 170 degrees and defaulting to the game's own angle. It is a ratio against the angle you walk around at, so a sprint still widens the view and the camcorder still zooms, both by the proportion they always did.
- Added zoom compensation, so raising the camcorder or running no longer changes how far your head moves the view. A narrower view magnifies everything in the frame, so the same head turn used to sweep further across the screen the moment you lifted the camcorder. The head pose is scaled by the angle the game is drawing at, which is exactly no change in ordinary play and follows a `FieldOfView` of your own. Head tilt is left alone, because a tilt rolls the picture by the same angle at any field of view.
- Added head-following for the camcorder's light. In night vision the lit cone would otherwise point wherever the mouse was, putting most of what you could see off to one side of the view; it turns with the head one for one, while the camcorder still records and aims where your mouse or controller does.
- Added `HeadTracking.log` next to `OLGame.exe`, reporting the matched game build and the frame's real horizontal and vertical angles once per session.
- Added crosshair compensation: the game's own crosshair moves to where the game is actually pointing, so reaching for a door, a locker or a battery is aimed at rather than guessed at once the head has turned the view away. It is hidden on a head turn far enough to put that direction off the screen.
- Added a gameplay gate that holds the head pose off in the front-end menu and while a level is loading, so the menu backdrop stays where the game put it.
- Added window centring on the monitor after the game resizes its window. Outlast centres the window once, at the size the splash movies play at, and then resizes it for the menu without moving it, leaving it off centre for the rest of the session. A window that fills the screen is left where it is, and `[General] CenterWindow=false` leaves the window alone.
- Added a PE fingerprint check that leaves the mod fully dormant on a game build it does not know, so the game runs vanilla rather than crashing on stale offsets.
