// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "lean_trace.h"

#include "camera_trace.h"
#include "ue3_types.h"

#include <cmath>

namespace OutlastHeadTracking {
namespace lean_trace {

namespace {
unsigned int g_flags = kTraceAllFlags;
}  // namespace

void SetTraceFlags(unsigned int flags) {
    g_flags = flags;
}

cameraunlock::camera::LeanObstruction Query(void* context,
                                            const cameraunlock::math::Vec3& start,
                                            const cameraunlock::math::Vec3& direction,
                                            float maxDistance) {
    cameraunlock::camera::LeanObstruction out;

    // The clamp already asks for the lean plus its standoff, so the ray reaches past
    // where the eye is allowed to stop and can see the surface it is coming to rest
    // against. A ray that ended where the lean ends could not: the eye would travel the
    // whole way, arrive against the wall, and only come back once the head pushed far
    // enough for the ray itself to cross it.
    const UE3Vector from{start.x, start.y, start.z};
    const UE3Vector to{start.x + direction.x * maxDistance,
                       start.y + direction.y * maxDistance,
                       start.z + direction.z * maxDistance};

    UE3Vector impact{};
    bool hit = false;
    if (!TraceWorldLine(context, from, to, g_flags, impact, hit)) {
        // Left with queried = false. The clamp then passes the lean through unclamped and
        // says so through LastQueryFailed(), because a clamp that has quietly stopped
        // clamping looks exactly like one that never engaged.
        return out;
    }

    out.queried = true;
    out.blocked = hit;
    if (hit) {
        const float dx = impact.X - start.x;
        const float dy = impact.Y - start.y;
        const float dz = impact.Z - start.z;
        out.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return out;
}

}  // namespace lean_trace
}  // namespace OutlastHeadTracking
