# Outlast Head Tracking

![Outlast running with this mod](https://raw.githubusercontent.com/itsloopyo/outlast-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Outlast that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the view; your mouse or controller keeps the aim
- **6DOF positional tracking** - lean and peek around corners with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Outlast](https://store.steampowered.com/app/238320/Outlast/) on Steam, the
  2014-04-29 build. Steam is the only store the mod supports: it ships one build
  profile, for that executable, and a copy from any other store is a different
  binary that the mod leaves alone.
- A tracking source that sends the OpenTrack UDP protocol:
  [OpenTrack](https://github.com/opentrack/opentrack) with a webcam or a VR
  headset, or a phone app that speaks it.
- 64-bit Windows 10 or 11. The mod is a 64-bit DLL and loads into `Binaries\Win64\OLGame.exe`,
  which is the executable the launcher picks on a 64-bit system; a 32-bit install is not
  supported.

The mod identifies the game from its own executable and does nothing at all on a build it
does not recognise: no hooks, no changes, the game runs exactly as it does without the
DLL. That is deliberate, because attaching to a patched build at addresses that have moved
crashes the game within seconds. To see which side you are on, start the game once and read
the first few lines of `HeadTracking.log` in `Binaries\Win64\`: it either names the build
it matched, or says the build was not recognised and why.

## Installation

### Lopari

Once this mod is available in [Lopari](https://lopari.app), download Lopari,
choose **Outlast**, and click **Play with head tracking**. Until then, install it
with the standalone installer below.

### Standalone Installer

1. Download the installer ZIP from the
   [Releases](https://github.com/itsloopyo/outlast-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1:4242`.
5. Launch the game.

If the installer cannot find your copy of the game, point it at the folder
yourself. Either pass the path as an argument:

```powershell
install.cmd "D:\Games\Outlast"
```

or set the `OUTLAST_PATH` environment variable before running it:

```powershell
$env:OUTLAST_PATH = 'D:\Games\Outlast'
```

Give it the folder that contains `OutlastLauncher.exe`, not the
`Binaries\Win64` folder inside it.

### Manual Installation

The mod is a single `dinput8.dll` that the game imports and loads itself, so
there is no mod loader to install first.

1. Open the Nexus ZIP, or the `plugins\` folder inside the installer ZIP.
2. Copy `dinput8.dll` into `Binaries\Win64\` next to `OLGame.exe`, which is
   `<game folder>\Binaries\Win64\` for a Steam install.

The Nexus ZIP already has that layout, so it can be extracted straight over the
game folder. To remove a manual install, delete `dinput8.dll`,
`HeadTracking.log` and `HeadTracking.prev.log` from that same folder, and
`CameraUnlock.ini` too if you do not want to keep your settings.

## Setting Up OpenTrack

In OpenTrack, set **Output** to `UDP over network`, open its options, and enter
address `127.0.0.1` and port `4242`. Pick an **Input** to match your hardware
using one of the sections below, then press **Start**.

### VR Headset Setup

1. Connect the headset to the PC over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR and let the headset finish tracking setup.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on UDP `127.0.0.1:4242`.

### Webcam Setup

Set OpenTrack's **Input** to `neuralnet tracker`. It tracks your face from an
ordinary webcam, so it needs no markers, no clip and no IR hardware.

### Phone App Setup

A phone app is usable here if it sends the OpenTrack UDP protocol, either
itself or through a PC-side companion. Check your app against that before
anything else, because plenty of phone trackers speak something different.

For an app that does send it, what decides the wiring is how much filtering the
app does before the packet leaves the phone. An app that filters on-device can
point straight at this PC's LAN address on port `4242`. A raw or lightly
filtered feed sent direct will jitter, because the mod's smoothing is sized to
take the edge off a clean signal rather than to rescue a noisy one, and that app
should go through OpenTrack instead so its filters and curves can clean the feed
up first. Route it through OpenTrack anyway if you want OpenTrack's curve
mapping. The test is quicker than the theory: try direct, hold your head still,
and if the view drifts or shakes, put OpenTrack in the middle.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody
with a phone already in their pocket. It filters on-device, so it can send
direct. Any app that filters enough noise works exactly the same way.

A phone on WiFi is a remote connection and gets `RemoteSmoothing`. So does a
tracker running on this same PC if it sends to the machine's LAN address
instead of `127.0.0.1`, because the mod classifies the connection by the
address the packet came from rather than by which machine it came from.

## Controls

Two equivalent binding sets by default - use whichever your keyboard has. Each
action's keys are a list in `CameraUnlock.ini` (see Configuration), so either
can be rebound:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` / `Ctrl+Shift+H` switches yaw between world-space (horizon-locked)
and camera-local.

Center the view in your tracker: opentrack's Center bind, SteamVR's reset, or
the CENTER button in your phone app. The mod applies whatever pose the tracker
sends, so centring in one place is the whole of it.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `Binaries\Win64\CameraUnlock.ini` in the game folder, and creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `DataFreshnessMs=500`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `CollisionEnabled=true`
- `CollisionReleaseSmoothing=0.9`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; Outlast head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default
; Milliseconds a tracker packet stays current. Once the tracker has sent nothing
; for this long, the mod stops following it until data arrives again.
DataFreshnessMs=default
; Keep the game window centred on its monitor. Outlast centres it once, while the
; splash movies play, then resizes it for the menu without moving it. A window
; that fills the screen is left alone.
CenterWindow=true

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default
; true: leaning stops at walls instead of moving the view through them.
CollisionEnabled=default
; How far, in centimetres, the view is held off a wall when you lean into it.
; Below about 10 the wall stops being drawn before the view stops moving.
CollisionMargin=15.0
; The game's own trace mask the wall check uses. 8383 is 0x20BF, the mask
; the crosshair trace uses, so the check also stops on characters.
; CollisionChannel=8383
; How gently the view eases back out after a wall stopped a lean.
; 0 is the quickest, 1 the slowest.
CollisionReleaseSmoothing=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[View]
; Field of view in degrees, 0 or 20 to 170. 0 keeps the game's own. Outlast has no
; field-of-view setting; this is the mod rendering the frame at a different angle,
; as a ratio against the camera's unzoomed angle, so raising the camcorder and
; running still change the view by the same proportion.
FieldOfView=0.0

[Diagnostics]
; Find the live camera record by scanning memory and report what writes and reads
; it to HeadTracking.log. A diagnostic tool that sets a CPU watchpoint in every
; game thread. Leave it false.
CameraProbe=false
; Report the lights near the player to HeadTracking.log. A diagnostic tool; leave
; it false.
LightProbe=false
; Log the reticle target, camera position and screen offset once a second.
; A diagnostic tool; leave it false.
AimProbe=false
```
<!-- /cameraunlock:config -->

`Page Up` / `Ctrl+Shift+G` and `Page Down` / `Ctrl+Shift+H` save the mode they
switch to in `CameraUnlock.ini`, so the next start begins in it. `End` /
`Ctrl+Shift+Y` changes the session only; `EnableOnStartup` decides whether
tracking is on at the next start.

### The camcorder's light

The camcorder lights what it is pointed at, and the game points it where you are
aiming, which would leave the lit cone off to one side whenever you look away
from where you are pointing. In night vision that cone is most of what you can
see. The mod turns the light by the same amount it turns the view, so the cone
follows your head while the camcorder still records, and still points, where
your mouse or controller is aiming.

### The crosshair

Outlast's own crosshair, the small dot switched on under Options, marks where
you are pointing only while the view and the aim are the same thing. Head
tracking separates them, so the mod moves the game's own dot to where the game
is actually pointing. Switch the dot on and off the way you always could, in the
game's Options.

The dot follows the direction the game is pointing rather than the point it is
pointing at, so leaning your head sideways leaves it a little off what you are
about to grab, most at arm's reach and less across a room.

### Field of view

Outlast has no field-of-view setting in its menus, but it does have one in its
own config file. In `Documents\My Games\Outlast\OLGame\Config\OLGame.ini`,
under `[OLGame.OLHero]`, `DefaultFOV=90.0` is the angle you walk around at and
`RunningFOV=100.0` the one a sprint widens to. Editing `DefaultFOV` there does
change what the game draws: on a 16:9 display 90 renders a 96.0 by 64.0 degree
frame and 100 renders 105.9 by 73.4. The mod reads whatever you set, so head
tracking stays the right size either way.

`FieldOfView` in `CameraUnlock.ini` is the mod's own, and it works differently.
It is applied as a ratio against the game's unzoomed angle, so a sprint still
widens the view and raising the camcorder still zooms it, both by the proportion
they always did; `RunningFOV` is a separate line in the game's file and does not
follow an edit to `DefaultFOV`. `0`, the default, leaves the game's own angle
alone. The mod accepts 20 to 170; outside that it says so in `HeadTracking.log`
and renders the game's field of view.

It changes the frame only: what the game reaches for when you press use, and
everything else that asks the game the same question, keep the game's own
answer. On a 16:9 display, `FieldOfView=110` draws a 115.6 by 83.5 degree frame,
and a wider monitor buys width rather than costing height. `HeadTracking.log`
reports the frame's real angles once per session, so you never have to work
them out.

### Zoom

Raising the camcorder narrows the view and running widens it, and a narrower
view magnifies everything in the frame. The mod scales the pose by whatever the
game is drawing at, so a head turn moves the view by the same amount on screen
zoomed in as it does walking around. Head tilt is left alone, because a tilt
rolls the picture by the same angle at any field of view. The scaling follows a
`FieldOfView` of your own as well, so an override is not a permanent sensitivity
change. `HeadTracking.log` prints the numbers it works from once per session.

## Troubleshooting

**Mod not loading**

- Check that `dinput8.dll` sits in `Binaries\Win64\` next to `OLGame.exe`, not
  in the game's root folder.
- Look for `HeadTracking.log` in that same folder. If it is missing, the game
  never loaded the DLL; run `install.cmd` again and let it find the game itself.
- If the log says the game build was not recognized, the mod has left itself
  dormant on a build it does not know and the game is running vanilla. Check
  the [Releases](https://github.com/itsloopyo/outlast-headtracking/releases)
  page for a newer version.

**No tracking response**

- In OpenTrack, confirm **Output** is UDP to `127.0.0.1` port `4242` and that
  you have pressed **Start**.
- Confirm `UdpPort` in `CameraUnlock.ini` matches the port your tracker sends to.
- If the log says the port could not be opened, another program already had it
  when Outlast started - usually a head tracking mod in a game you left running.
  Close that game and tracking starts about half a second later on its own; you
  do not need to restart Outlast or touch the config. The line above it in the log
  is the reason Windows gave, verbatim.
- Press `End` or `Ctrl+Shift+Y`; tracking may have been toggled off.
- The front-end menu is not the game. The mod holds the pose off there and
  while a level loads, so the menu's backdrop never moves with your head. Load
  a save or start a new game to see tracking.
- For a phone on WiFi, allow the game through the Windows firewall on private
  networks, and send to this PC's LAN address rather than `127.0.0.1`.

**Jittery or unstable tracking**

- On a phone or other network tracker, raise `RemoteSmoothing` a little at a
  time; on a tracker running on this PC, raise `LocalSmoothing` from its
  default of `0.0`.
- If a phone app sends a raw feed, route it through OpenTrack and use
  OpenTrack's own filters instead of leaning on the mod's smoothing.

**Wrong rotation axis, or the view drifts when you look up and down**

- Press `Page Down` or `Ctrl+Shift+H` to switch yaw mode. World-space is
  horizon-locked; camera-local follows the camera's current up-axis.
- If the view sits off-center, center it in your tracker rather than in the
  game: opentrack's Center bind, SteamVR's reset, or your phone app's CENTER
  button.

## Updating

Download the new release and run `install.cmd` again. Your settings are kept.

## Uninstalling

Run `uninstall.cmd`. It removes `dinput8.dll`, `HeadTracking.log` and
`HeadTracking.prev.log` from `Binaries\Win64\`, and restores any `dinput8.dll`
that was already there before the mod was installed. It leaves
`CameraUnlock.ini` and any `HeadTracking.ini` an older version left in place, so
a reinstall keeps your settings. This mod ships no separate mod
loader, so `uninstall.cmd /force`, which exists to remove a loader the
installer did not put there, has nothing extra to do here.

## Building from Source

Needs Visual Studio 2022 with the Desktop development with C++ workload, and
[pixi](https://pixi.sh). No copy of the game is required.

```powershell
git clone --recurse-submodules https://github.com/itsloopyo/outlast-headtracking
cd outlast-headtracking
pixi run test
pixi run package
```

`pixi run package` builds `dinput8.dll` and writes both release ZIPs into
`release\`.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details. The same copyright covers
`cameraunlock-core`, the shared submodule this mod compiles in.

MIT does not cover the clip embedded at the top of this page: that is footage
of Outlast and stays Red Barrels' copyright.
Components that are not ours are listed with their notices in
[THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).

## Credits

- Outlast is developed and published by
  [Red Barrels](https://redbarrelsgames.com).
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause) - the inline
  hooks the mod installs, including the Hacker Disassembler Engine it bundles.
- [OpenTrack](https://github.com/opentrack/opentrack) (ISC) - the UDP pose
  protocol the mod listens for.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Red Barrels. Use
at your own risk.
