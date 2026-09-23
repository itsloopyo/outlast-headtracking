// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>

namespace OutlastHeadTracking {

// Why the head pose is or is not being applied this frame.
enum class GameplayState {
    Playing,
    // The frame is being drawn from a menu level. Outlast's front end is a real level
    // rendered through the same viewpoint accessor gameplay uses, at the full frame
    // rate, so without this the head pose would swing the menu's backdrop around while
    // the player reads it.
    MenuLevel,
    // The world, its persistent level or its AWorldInfo could not be read, or the
    // engine has not yet resolved the class the streaming test needs. A level load looks
    // like this from the render thread, and so does the first frame of a session.
    NoWorld,
};

const char* Describe(GameplayState state);

// Where the gate reads its answer. Every field is build-specific, so it arrives from the
// matched profile rather than being written down a second time here.
struct GameStateTargets {
    std::uintptr_t gWorld;  // the GWorld pointer's own address, not the world
    // Likewise an address, not an RVA: the address of the global the engine caches its
    // ULevelStreamingPersistent UClass* in.
    std::uintptr_t levelStreamingPersistentClass;
    std::size_t    offWorldPersistentLevel;
    std::size_t    offLevelActorsData;
    std::size_t    offLevelActorsCount;
    std::size_t    offWorldInfoStreamingData;
    std::size_t    offWorldInfoStreamingCount;
    std::size_t    offLevelStreamingLoadedLevel;
    std::size_t    offObjectClass;
    std::size_t    offStructSuperStruct;
    std::size_t    offWorldInfoFlags;
    // Pointer-width to match OffsetTable's, which it is copied from - see the note on
    // padding there. The flag itself is a bit of a dword in the game.
    std::size_t    maskIsMenuLevel;
};

void InitGameState(const GameStateTargets& targets);

// Asks the game's own bIsMenuLevel, off the AWorldInfo that UWorld::GetWorldInfo returns
// when it is told to check for a streaming-persistent level. On Outlast that second part
// IS the test - see the comment in GetGameplayState - and reading the flag off the
// persistent level instead reports a menu for the whole session.
//
// Calling the engine's own accessor is not an option from here: AWorldInfo::IsMenuLevel
// is a script-dispatched native, and entering ProcessEvent from inside the frame's own
// viewpoint call would re-enter the camera hook.
//
// Every dereference is guarded, so a chain that has been torn down mid-load answers
// NoWorld rather than faulting the render thread. A dozen guarded reads a frame - each
// an SEH-wrapped memcpy of a pointer - and no cache, because a cached answer is one that
// is wrong for as long as it is stale, and crossing between a menu and a level is
// exactly when it changes.
GameplayState GetGameplayState();

}  // namespace OutlastHeadTracking
