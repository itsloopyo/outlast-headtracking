// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "build_profile.h"

// Every Steam build of Outlast the mod knows about, one profile each, append-only.
// A patch that moves an address gets a NEW profile at the top of kKnownProfiles
// (build_registry.cpp); the entries below are never edited, because a player who has
// not taken that patch still matches one of them by fingerprint and would otherwise be
// left with a mod that hooks the wrong addresses.

namespace OutlastHeadTracking {

// Steam, x64, TimeDateStamp 2014-04-29. Every address and offset below is a measurement
// of that one shipped OLGame.exe, recorded here with what identifies it, so a reader can
// tell what the mod is talking to before trusting a number:
//
//   0x62A020  ULocalPlayer::CalcSceneView. Recognised by the projection it builds -
//             FPerspectiveMatrix(halfFOV, halfFOV, MultX, MultY, 10, 10) at 0x60E9C0 -
//             and by its read of NAME_GetPlayerViewPoint.
//   0x60F4F0  APlayerController::eventGetFOVAngle: FindFunction(NAME_GetFOVAngle)
//             (the FName global written at 0x42A550) then ProcessEvent, returning the
//             angle in xmm0. Four callers; only CalcSceneView's reaches the renderer.
//   0x62A1C0  the instruction after CalcSceneView's `call 0x60F4F0` at 0x62A1BB.
//   0x168E50  APlayerController::GetPlayerViewPoint: FindFunction(NAME_GetPlayerViewPoint)
//             (the FName global written at 0x42A7F0) then ProcessEvent through the
//             object's vtable at +0x218, with the 24-byte parameter block copied back
//             into the caller's FVector and FRotator. Four callers; CalcSceneView's is
//             the frame.
//   0x62A226  the instruction after CalcSceneView's `call 0x168E50` at 0x62A221.
//   0x63062D  the instruction after UGameViewportClient::Draw's `call 0x62A020` at
//             0x630628. The other two callers of CalcSceneView are ULocalPlayer::Project
//             (0x62B420) and ULocalPlayer::Deproject (0x62B280), which script calls to
//             convert between a world point and a screen point.
//
//   +0x3798   AOLHero::DefaultFOV, reached through APlayerController::Pawn at +0x248.
//             Identified by the block OLGame.ini's [OLGame.OLHero] section authors,
//             which lies out in declaration order around it: DefaultFOV 90 and
//             RunningFOV 100 at +0x3798 and +0x379C, the four FOVApproachCoeff values
//             0.97 / 0.97 / 0.4 / 0.97 next, and CamcorderMinFOV 15 with the two 83s
//             at +0x37BC. Measured in game, GetFOVAngle returns exactly 90.000 while
//             the player stands still and climbs toward 100 in a run, so this field is
//             the unzoomed angle in the same measure the live one is quoted in.
//
//             NOT the camera's DefaultFOV at +0x250, nor the controller's at +0x49C.
//             CalcSceneView normalises its own level-of-detail factor against those, so
//             they look like the reference and read as perfectly plausible angles - but
//             both hold 80 on this build while the game renders 90 walking around, and
//             measuring the zoom against one of them scaled every pose by 1.19 through
//             the whole of ordinary play.
//
//   0x200B5A8 GWorld. Read off AWorldInfo::execIsMenuLevel (0x397B30, registered under
//             that name in the native table at 0x1E163B0) and confirmed against
//             UWorld::GetWorldInfo (0x3964D0), which walks the same chain: GWorld,
//             +0x80 the persistent level, +0x60 the Actors array's element pointer,
//             +0x68 its count, element 0 the AWorldInfo. An empty level name is answered
//             from bIsMenuLevel, which is bit 0x20000 of the bitfield dword at +0x3BC.
//
//             GetWorldInfo's second branch is the one the gameplay gate needs, and the
//             rest of the chain comes from it: `+0x454` / `+0x44C` are
//             AWorldInfo::StreamingLevels' count and element pointer, `+0x68` on
//             StreamingLevels(0) is ULevelStreaming::LoadedLevel, and the loop that
//             follows walks `+0x50` (UObject::Class) then `+0x78` (UStruct::SuperStruct)
//             looking for the ULevelStreamingPersistent class the getter at 0x2F1D60
//             resolves by name and caches in the global at 0x200B5D0.
//   0x4F3D90  ULevel::GetWorldInfo, which is Actors(0) through the same +0x60 / +0x68 -
//             so a streamed level's AWorldInfo is reached exactly like the persistent
//             level's.
//
//   0xD1D450  AOLHUD::DrawCrosshair. Reached from AOLHUD::Draw (0xD8BA70), which is the
//             virtual at vtable slot +0x740 that AOLHUD::execDraw (0xCA4940, registered
//             at 0x1EAC420) dispatches to. Its whole body is: advance the fade, then
//             draw two tiles - a soft outer one and the dot - both at
//             `ClipX * 0.5 - size * 0.5` by `ClipY * 0.5 - size * 0.5`, with size the
//             fade blended between 6 and 20 pixels. It reads the canvas for nothing
//             else, which is what makes biasing that pair around the call move the dot
//             and nothing besides.
//   +0x518    AHUD::Canvas, and +0x70 / +0x74 that canvas's ClipX / ClipY.
//
//   0x2CEA90  ULightComponent::SetParentToWorld. Found by arming a data watchpoint on the
//             direction of the camcorder's own light while night vision was on: this is
//             the function that writes it, every frame the light moves. Its whole body is
//             a rotation-translation matrix built from +0x26C and +0x210, multiplied by
//             the parent transform at +0x1D0, with the products written back across the
//             component. Turning +0x26C for the duration of the call is therefore the one
//             field that moves the cone and nothing else.
//   +0x100    where the light says it is - ULightComponent::execGetOrigin (0x1DACC0)
//             answers with exactly this vector, and execGetDirection (0x1DAD40) answers
//             with the first element of each of the rows at +0x98, +0xA8 and +0xB8.
//   +0x140    the bitfield ULightComponent::execSetEnabled (0x1DAA40) flips bit 0 of,
//             which is bEnabled.
extern const BuildProfile kSteamProfile_20140429 = {
    "steam-win64-20140429",
    { 0x535FECFFu, 0x0222C000u, 0x020B0C50u },
    {
        0x62A020,
        0x60F4F0,
        0x62A1C0,
        0x168E50,
        0x62A226,
        0x63062D,
        0x200B5A8,
        0x200B5D0,
        0x80,
        0x60,
        0x68,
        0x44C,
        0x454,
        0x68,
        0x50,
        0x78,
        0x3BC,
        0x20000u,
        0xD1D450,
        0x518,
        0x70,
        0x74,
        0x2CEA90,
        0x26C,
        0x1D0,
        0x100,
        0x140,
        0x506920,
        0x248,
        0x3798,
    },
};

}  // namespace OutlastHeadTracking
