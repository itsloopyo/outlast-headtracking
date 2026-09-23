// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>

namespace OutlastHeadTracking {

// Identifies one shipped OLGame.exe. Three independent PE header fields rather than a
// version string, because a version string is not readable from inside the running
// process in any way that survives a store update, and the PE header is. A repacked or
// patched EXE fails the match instead of mis-routing onto RVAs that no longer describe
// it.
struct PeFingerprint {
    std::uint32_t timeDateStamp;
    std::uint32_t sizeOfImage;
    std::uint32_t checkSum;

    bool operator==(const PeFingerprint& other) const {
        return timeDateStamp == other.timeDateStamp && sizeOfImage == other.sizeOfImage &&
               checkSum == other.checkSum;
    }
};

// Everything the mod pins to an address inside one build. RVAs are relative to the
// module base, so the numbers below describe the image wherever Windows loads it.
struct OffsetTable {
    // ULocalPlayer::CalcSceneView. Returns the FSceneView the frame is rendered from,
    // which carries the projection matrix the frame was really drawn with.
    std::uintptr_t rvaCalcSceneView;

    // APlayerController::eventGetFOVAngle. Returns the field of view in degrees that
    // CalcSceneView builds that projection from.
    std::uintptr_t rvaGetFovAngle;

    // The instruction inside CalcSceneView that the call to the accessor above returns
    // to. It is what separates the one caller whose angle reaches the renderer from the
    // three whose answers are game logic.
    std::uintptr_t rvaSceneViewFovReturn;

    // APlayerController::GetPlayerViewPoint. Where the head pose enters the game: the
    // frame's copy of the answer is the only one it is composed into.
    std::uintptr_t rvaGetPlayerViewPoint;

    // The instruction inside CalcSceneView that the call to the accessor above returns
    // to, on the same terms as rvaSceneViewFovReturn.
    std::uintptr_t rvaSceneViewViewPointReturn;

    // The instruction UGameViewportClient::Draw returns to after its call to
    // CalcSceneView. It is what separates the CalcSceneView call that draws the frame
    // from ULocalPlayer::Project and ULocalPlayer::Deproject, which reach the same
    // viewpoint accessor to answer a question script asked.
    std::uintptr_t rvaDrawSceneViewReturn;

    // The GWorld pointer's own address. The gameplay gate walks the chain below off it,
    // the one UWorld::GetWorldInfo walks.
    std::uintptr_t rvaGWorld;
    // The engine's cached UClass* for ULevelStreamingPersistent, which is what decides
    // whether the level streamed into the persistent world REPLACES it. Zero until the
    // engine has resolved the class once - see game_state.cpp.
    std::uintptr_t rvaLevelStreamingPersistentClass;
    // UWorld::PersistentLevel.
    std::size_t offWorldPersistentLevel;
    // ULevel::Actors, a TArray: the element pointer, then the count. Actors(0) is the
    // level's AWorldInfo.
    std::size_t offLevelActorsData;
    std::size_t offLevelActorsCount;
    // AWorldInfo::StreamingLevels, the same TArray shape.
    std::size_t offWorldInfoStreamingData;
    std::size_t offWorldInfoStreamingCount;
    // ULevelStreaming::LoadedLevel.
    std::size_t offLevelStreamingLoadedLevel;
    // UObject::Class, and UStruct::SuperStruct - the two steps of an IsA test.
    std::size_t offObjectClass;
    std::size_t offStructSuperStruct;
    // The dword of AWorldInfo bitfields that carries bIsMenuLevel, and the bit of it
    // that is that flag. Both are the build's, not the engine's: which dword a bool
    // lands in and which bit of it moves whenever a bool is added above it.
    //
    // The mask is a dword in the game and a std::size_t here so that EVERY member of
    // this struct is pointer-width. A narrower member leaves padding, and padding is a
    // place a new member can be inserted without changing sizeof - which is the one
    // thing the assert below has to notice. Keep new members pointer-width too.
    std::size_t offWorldInfoFlags;
    std::size_t maskIsMenuLevel;

    // AOLHUD::DrawCrosshair, the native the HUD's own Draw dispatches to for the dot in
    // the middle of the screen.
    std::uintptr_t rvaDrawCrosshair;
    // AHUD::Canvas, and the UCanvas::ClipX / ClipY pair that function halves to find
    // that middle.
    std::size_t offHudCanvas;
    std::size_t offCanvasClipX;
    std::size_t offCanvasClipY;

    // ULightComponent::SetParentToWorld, which rebuilds a moving light's world transform
    // every frame, and the four fields of the component it reads and answers with.
    std::uintptr_t rvaLightSetParentToWorld;
    std::size_t offLightRotation;
    std::size_t offLightParentToWorld;
    std::size_t offLightOrigin;
    std::size_t offLightFlags;

    std::uintptr_t rvaSingleLineCheck;
    std::size_t offControllerPawn;
    // AOLHero::DefaultFOV: the angle the game draws with while the player is neither
    // running nor holding the camcorder up, which is the unzoomed reference every zoom
    // and any field-of-view override is measured against.
    std::size_t offHeroDefaultFov;
};

// Every profile in the registry is a positional aggregate initialiser - C++17 has no
// designated initialisers - so a member inserted anywhere but the END of OffsetTable
// silently re-routes every RVA after it in EVERY profile, the fingerprint still matches,
// and the mod hooks a wrong address. Nothing else catches that: it is not a type error and
// the compiler has nothing to say about it. This does, and the fix when it fires is to
// append the new member rather than to update the number.
//
// It only does it while every member is pointer-width. A narrower one leaves padding, and
// a member inserted into padding shifts every initialiser after it while sizeof stays put
// - the same silent re-routing, with the one guard against it asleep.
static_assert(sizeof(OffsetTable) == 30 * sizeof(std::uintptr_t),
              "OffsetTable changed size: append new members at the END, then update this "
              "count and add the new value to the end of every profile in the registry.");

struct BuildProfile {
    const char*   name;
    PeFingerprint fingerprint;
    OffsetTable   offsets;
};

}  // namespace OutlastHeadTracking
