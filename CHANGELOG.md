# Changelog

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
