// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include "logging.h"

#include "cameraunlock/memory/safe_memory.h"

namespace OutlastHeadTracking {

namespace {

GameStateTargets g_targets{};

// How far an IsA test may walk before it gives up. A UClass chain is a handful of links
// and the one this reads is two, so a walk that runs past this is not a class chain - a
// rebuilt object graph, or a layout this profile no longer describes. It is answered as
// unreadable rather than as a result, because a cycle here would hang the render thread
// and a guess would decide whether the player has head tracking at all.
constexpr int kMaxClassDepth = 16;

bool ReadPointer(std::uintptr_t addr, std::uintptr_t& out) {
    return cameraunlock::memory::SafeRead(addr, out) && out != 0;
}

// Actors(0) of a level, which for any UE3 level is its AWorldInfo.
bool LevelWorldInfo(std::uintptr_t level, std::uintptr_t& out) {
    // The count is read before the element pointer for the same reason the engine's own
    // accessor reads it: an empty TArray leaves the pointer holding whatever it last
    // held, and Actors(0) of an empty array is not the AWorldInfo.
    std::int32_t actorCount = 0;
    if (!cameraunlock::memory::SafeRead(level + g_targets.offLevelActorsCount, actorCount) ||
        actorCount <= 0) {
        return false;
    }
    std::uintptr_t actors = 0;
    if (!ReadPointer(level + g_targets.offLevelActorsData, actors)) {
        return false;
    }
    return ReadPointer(actors, out);
}

// Whether `object` is a ULevelStreamingPersistent, by the engine's own test: walk the
// object's class and its supers looking for the one the engine cached.
//
// `outDecided` is false when the walk could not run - the cached class pointer is not
// there yet, or the chain did not terminate. Separate from the answer, because "not a
// persistent streaming level" and "could not tell" lead to opposite views of what the
// player is looking at.
bool IsLevelStreamingPersistent(std::uintptr_t object, bool& outDecided) {
    outDecided = false;
    // Zero until the engine itself has resolved the class once, which it does through
    // UWorld::GetWorldInfo constantly - but not necessarily before the mod's first
    // frame. Undecided rather than false: a false here would call the front end
    // gameplay for as long as it lasted.
    std::uintptr_t wanted = 0;
    if (!ReadPointer(g_targets.levelStreamingPersistentClass, wanted)) {
        return false;
    }
    std::uintptr_t cls = 0;
    if (!ReadPointer(object + g_targets.offObjectClass, cls)) {
        return false;
    }
    for (int depth = 0; depth < kMaxClassDepth; ++depth) {
        if (cls == wanted) {
            outDecided = true;
            return true;
        }
        std::uintptr_t super = 0;
        if (!cameraunlock::memory::SafeRead(cls + g_targets.offStructSuperStruct, super)) {
            return false;
        }
        if (super == 0) {
            outDecided = true;
            return false;
        }
        cls = super;
    }
    return false;
}

// The AWorldInfo the gate should read, once the streaming-persistent level - if there is
// one - has replaced the persistent level's.
//
// `worldInfo` is the persistent level's on the way in, and is left as it is whenever
// there is nothing to replace it with: no streaming levels, a streamed level that has not
// finished arriving, or one that is an ordinary ULevelStreaming.
//
// False when the walk could not be completed, which is not the same as leaving the flag
// where it was: an undecided IsA test, or a persistent level whose own AWorldInfo cannot
// be reached, means the gate does not know what is being drawn.
bool ResolveStreamedWorldInfo(std::uintptr_t& worldInfo) {
    // Each step separates a read that FAILED from a read that succeeded and answered
    // nothing. They are not the same: the second is a level that has not arrived yet and
    // correctly leaves the flag on the front end's own AWorldInfo, while the first means
    // the gate does not know what is being drawn. Both suppress tracking, but they are
    // different lines in the log, and the log is read on exactly the transition where
    // this array is being reallocated from two entries to forty-one.
    std::int32_t streamingCount = 0;
    if (!cameraunlock::memory::SafeRead(worldInfo + g_targets.offWorldInfoStreamingCount,
                                        streamingCount)) {
        return false;
    }
    if (streamingCount <= 0) {
        return true;
    }
    std::uintptr_t streamingData = 0;
    if (!ReadPointer(worldInfo + g_targets.offWorldInfoStreamingData, streamingData)) {
        return false;
    }
    std::uintptr_t streaming = 0;
    if (!ReadPointer(streamingData, streaming)) {
        return false;
    }
    std::uintptr_t loaded = 0;
    if (!cameraunlock::memory::SafeRead(streaming + g_targets.offLevelStreamingLoadedLevel,
                                        loaded)) {
        return false;
    }
    // A streamed level that has not finished arriving has no LoadedLevel, which leaves
    // the flag on the front end's own AWorldInfo - which is what a level load should look
    // like from here.
    if (loaded == 0) {
        return true;
    }

    bool decided = false;
    const bool persistent = IsLevelStreamingPersistent(streaming, decided);
    if (!decided) {
        return false;
    }
    if (!persistent) {
        return true;
    }
    return LevelWorldInfo(loaded, worldInfo);
}

}  // namespace

const char* Describe(GameplayState state) {
    switch (state) {
        case GameplayState::Playing:   return "playing";
        case GameplayState::MenuLevel: return "a menu level (the front end)";
        case GameplayState::NoWorld:   return "the world could not be read (a level load)";
    }
    return "unknown";
}

void InitGameState(const GameStateTargets& targets) {
    g_targets = targets;
    Log::Line("Gameplay gate armed: GWorld @ 0x%p, bIsMenuLevel at AWorldInfo+0x%zX bit "
              "0x%zX, read off the streaming-persistent level wherever there is one.",
              reinterpret_cast<void*>(targets.gWorld), targets.offWorldInfoFlags,
              targets.maskIsMenuLevel);
}

GameplayState GetGameplayState() {
    std::uintptr_t world = 0;
    if (!ReadPointer(g_targets.gWorld, world)) {
        return GameplayState::NoWorld;
    }
    std::uintptr_t level = 0;
    if (!ReadPointer(world + g_targets.offWorldPersistentLevel, level)) {
        return GameplayState::NoWorld;
    }
    std::uintptr_t worldInfo = 0;
    if (!LevelWorldInfo(level, worldInfo)) {
        return GameplayState::NoWorld;
    }

    // The step that matters on this game, and the one UWorld::GetWorldInfo takes when it
    // is asked to check for a streaming-persistent level.
    //
    // Outlast never leaves the front end's world. Picking Continue streams the chapter in
    // as a ULevelStreamingPersistent inside MainMenuGame's own UWorld, so GWorld and its
    // PersistentLevel are the SAME objects in the asylum as they are on the title screen,
    // and that level's AWorldInfo carries bIsMenuLevel for the whole session. Reading the
    // flag straight off it - which is what AWorldInfo::IsMenuLevel itself does, because
    // script only ever asks it from the front end - reports a menu for as long as the
    // player plays, and the head pose never reaches the camera.
    //
    // Measured on steam-win64-20140429: on the title screen StreamingLevels(0) is an
    // ordinary ULevelStreaming and the flag reads 1; in the asylum it is a
    // ULevelStreamingPersistent whose loaded level's own AWorldInfo reads 0.
    if (!ResolveStreamedWorldInfo(worldInfo)) {
        return GameplayState::NoWorld;
    }

    std::uint32_t flags = 0;
    if (!cameraunlock::memory::SafeRead(worldInfo + g_targets.offWorldInfoFlags, flags)) {
        return GameplayState::NoWorld;
    }
    return (flags & g_targets.maskIsMenuLevel) != 0 ? GameplayState::MenuLevel
                                                    : GameplayState::Playing;
}

}  // namespace OutlastHeadTracking
