// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace OutlastHeadTracking {

// Whether the ULocalPlayer::CalcSceneView call running on this thread right now is the
// one building the frame on screen.
//
// CalcSceneView has three callers in OLGame.exe: UGameViewportClient::Draw, which draws
// the frame, and ULocalPlayer::Project and ULocalPlayer::Deproject, which script calls to
// convert between a world point and a screen point. All three reach the same viewpoint
// accessor, and only the first is a frame the player sees, so the head pose is composed
// into that one and no other. Two things go wrong without the distinction, and the second
// is the worse of them: a Project() answer the game asked for would come back displaced
// by the player's head, and - because advancing the pipeline is what measures the frame
// interval - every extra call would divide the delta that all the smoothing is decided
// against.
//
// Set by the scene-view hook around the original call and read by the camera hook from
// inside it. thread_local because it describes the call on this thread's stack, not a
// state of the mod.
inline thread_local bool t_drawingFrame = false;

// Marks the enclosing CalcSceneView call as the one drawing the frame, for as long as it
// runs.
//
// The previous value is restored rather than cleared, because the calls nest: the
// UnrealScript behind the frame's own GetPlayerViewPoint can reach ULocalPlayer::Project,
// which enters this detour again from inside the drawing one. Forcing the flag to false
// on the way out of that inner call would leave the outer frame's viewpoint - the one
// still to be composed, further up the same stack - looking like a Project, and the pose
// would silently not reach the camera on any frame where script did that.
class DrawingFrameScope {
public:
    explicit DrawingFrameScope(bool drawingFrame) : m_previous(t_drawingFrame) {
        t_drawingFrame = drawingFrame;
    }
    ~DrawingFrameScope() { t_drawingFrame = m_previous; }

    DrawingFrameScope(const DrawingFrameScope&) = delete;
    DrawingFrameScope& operator=(const DrawingFrameScope&) = delete;

private:
    bool m_previous;
};

inline bool IsDrawingFrame() {
    return t_drawingFrame;
}

}  // namespace OutlastHeadTracking
