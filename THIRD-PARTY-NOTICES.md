# Third-Party Notices

Outlast Head Tracking is MIT licensed - see `LICENSE` at the root of this ZIP
for the full text, which covers the mod's own code and `cameraunlock-core`
below.

The mod deploys one file into the game, `dinput8.dll`. The components below are
compiled into it, ship beside it in the installer ZIP, or are spoken to over the
network at runtime.

## cameraunlock-core

- **Version:** commit `c480d8a8177753966a7d33b857f1db12f5e9fe39`, the submodule pointer this build compiles.
- **License:** `MIT`
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** The shared head-tracking library - the OpenTrack UDP receiver, pose interpolation and smoothing, and the camera maths this mod's Outlast-specific hooks feed.
- **Bundled:** yes. Compiled from the pinned submodule into `dinput8.dll`, and its installer scripts ship as files under `shared/` in the installer ZIP (the per-loader install bodies, `find-game.ps1`, `GamePathDetection.psm1` and `games.json`). Its copyright holder is the same as this mod's, so the `LICENSE` file shipped beside these notices is its licence too; no separate copy is needed.

---

## MinHook

- **Version:** `v1.3.4`, upstream commit `c3fcafdc10146beb5919319d0683e44e3c30d537`, vendored under `extern/minhook/`. One file, `src/hook.c`, carries a local change: MinHook's internal bookkeeping is allocated from the process heap rather than from a private heap of its own. Every other file matches that release. `extern/minhook/README.md` records this and is the authority; this entry is a copy of it.
- **License:** `BSD-2-Clause`
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Installs every inline function hook the mod places: the camera viewpoint accessor, the scene-view build, the field-of-view accessor, the HUD crosshair draw and the light transform update.
- **Bundled:** yes. Compiled from vendored source into `dinput8.dll`; no separate binary ships in the release ZIP.

MinHook carries two copyright holders: Tsuda Kageyu for MinHook itself, and
Vyacheslav Patkov for the Hacker Disassembler Engine that `src/hde/` is built
from. `hde32.c` and `hde64.c` are compiled in alongside the rest, so both
notices appear below exactly as the vendored `extern/minhook/LICENSE.txt`
ships them.

```
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

---

## Hacker Disassembler Engine (HDE32 / HDE64)

- **Version:** distributed as part of the vendored MinHook source (`extern/minhook/src/hde/`), copyright range 2008-2009.
- **License:** `BSD-2-Clause`
- **Upstream:** https://github.com/TsudaKageyu/minhook/tree/master/src/hde
- **Usage:** MinHook's instruction length decoder, used when building hook trampolines.
- **Bundled:** yes. Compiled from vendored source into `dinput8.dll`.

```
Hacker Disassembler Engine 32 C / Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.
```

---

## Unreal Engine 3 (Epic Games)

- **License:** none granted, and none needed. No Epic Games code, header or asset is
  copied, linked or shipped by this mod.
- **Usage:** Outlast runs on Unreal Engine 3, and this mod speaks to it across a
  boundary that has to be auditable, so the provenance of what crosses it is recorded
  here.

  What the mod carries is engine *conventions* and *names*. Some of those are matters of
  public record in the UnrealScript API Epic published with the UDK: that an `FVector`
  is three floats, that an `FRotator` is three signed 32-bit fields with 65536 units to
  a full turn, that world units are centimetres, and the script-visible members
  `APlayerController.GetPlayerViewPoint` and `WorldInfo.bIsMenuLevel`. The rest are
  behaviour of the engine's C++ side, established here by measuring the running game:
  that yaw is the outermost rotation about world Z, which is why adding rotators gives
  horizon-locked yaw; that `FRotationMatrix` composes as `M(P,Y,R) = M(P,0,R) *
  M(0,Y,0)` in the row-vector convention; and the identifiers
  `ULocalPlayer::CalcSceneView`, `ULightComponent::SetParentToWorld` and
  `UWorld::PersistentLevel`, which the source names so a reader can tell what the mod is
  talking to. All of it is implemented in this mod's own code, in `src/ue3_types.h` and
  `src/ue3_rotation.h`.

  The per-build addresses and structure offsets in `src/steam_offsets.cpp` are
  measurements of one shipped executable, taken for interoperability. They are numbers,
  not expression: no engine or game code is reproduced anywhere in this repository, and
  the mod reads the game's memory at runtime rather than shipping any part of it.
- **Bundled:** no.

---

## Outlast (Red Barrels)

- **License:** none granted, and none needed. The mod contains no Outlast code, no
  extracted asset, no text and no audio, and requires a legitimately purchased copy of
  the game to be of any use. Nothing of Red Barrels' is in this repository apart from
  the README's demo clip, which has its own section below.
- **Usage:** The mod is loaded by the game and modifies only what the player sees. It
  does not alter save data, bypass any licence check, or change what the game reports
  to anyone.
- **Bundled:** no.

---

## Outlast footage

- **File:** `assets/readme-clip.gif`, the clip the top of `README.md` embeds from the
  `main` branch.
- **Rights holder:** Red Barrels, together with the rights holders of any third-party
  marks visible in frame.
- **Usage:** recorded from the game running with this mod, captured on a legitimately
  purchased copy, shown so a reader can see what the mod does before installing it.
- **Bundled:** no. The packaging scripts copy the built `dinput8.dll`, the install
  scripts and the documentation at the repo root, and no part of `assets/`, so the clip
  is in neither release ZIP nor anything the launcher deploys. Wherever it is published,
  this section is the statement of terms it is published under.
- **Licence:** none is granted or implied by this repository. This material is not
  covered by the MIT licence in `LICENSE`, and nothing here permits reuse of it. Rights
  holders who would rather it were not published: open an issue or reach us on Discord
  and it comes down.

---

## OpenTrack

- **Version:** none pinned. The datagram layout is implemented in our own code; no OpenTrack release is depended on or shipped.
- **License:** `ISC`
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** The mod receives head pose over OpenTrack's UDP datagram protocol on port 4242. Protocol only. No OpenTrack code is used, linked, or shipped, and OpenTrack itself is not required as long as the sender speaks that protocol.
- **Bundled:** no.

---
