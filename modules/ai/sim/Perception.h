// Vendored copy of module-ai 0.2.0; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "ai/sim/Utility.h"

#include "mealkit/core/Math.h"
#include "mealkit/core/Reflect.h"
#include "mealkit/physics/PhysicsWorld.h"

#include <cmath>

// Perception (docs/index.md): a sight cone with a line-of-sight ray, noises that carry a distance, and a memory of the last place a
// target was known. Uses only adds, multiplies, compares and one `sqrt` (exact in IEEE), so every peer decides the same.
// Candidates must be passed in Pool id order; ties go to the first.
namespace ai {

using mealkit::f32;
using mealkit::Vec3;

// Where one AI looks from. forward must be unit length. cosHalfFov = cos(half the field of view), e.g. 0.5 for 120°
// (compute it once with mealkit::detCos, or keep it in tuning); -1 sees all around.
struct Eye {
    Vec3 position;
    Vec3 forward;
    f32 range = 0.0f;
    f32 cosHalfFov = 0.0f;
};

// Inside the cone: within range and within the field of view. No walls considered.
inline bool inCone(const Eye& eye, Vec3 target) {
    const Vec3 d = target - eye.position;
    const f32 dist2 = mealkit::lengthSq(d);
    if (dist2 > eye.range * eye.range) return false;
    if (dist2 <= 0.0f) return true; // on top of the eye
    const f32 along = mealkit::dot(d, eye.forward);
    const f32 c = eye.cosHalfFov;
    if (c <= -1.0f) return true;
    // along / |d| >= c without a sqrt: both sides squared, with the sign of each side kept
    if (c >= 0.0f) return along >= 0.0f && along * along >= c * c * dist2;
    return along >= 0.0f || along * along <= c * c * dist2;
}

// True when no static or other body stands between the two points. `selfBody` (and a nonzero `selfGroup`, a ragdoll's parts) are
// ignored; the last `endMargin` meters before the target are ignored too, so the target's own body doesn't block its own view.
inline bool lineClear(const mealkit::physics::PhysicsWorld& world, Vec3 from, Vec3 to, mealkit::physics::BodyId selfBody,
                      f32 endMargin = 0.0f, u32 selfGroup = 0) {
    const Vec3 d = to - from;
    const f32 len = std::sqrt(mealkit::lengthSq(d));
    if (len <= endMargin) return true;
    mealkit::physics::RayHit hit;
    if (!world.castRay(from, d, hit, selfBody, selfGroup)) return true;
    return hit.fraction * len >= len - endMargin;
}

struct Candidate {
    u32 id = 0; // the game's id for it (nonzero)
    Vec3 position;
    mealkit::physics::BodyId body = mealkit::physics::kInvalidBody;
};

// What one AI knows about its target.
MEALKIT_STRUCT(Awareness) {
    MEALKIT_FIELD(u32, target, 0, "Target id (0 = none)");
    MEALKIT_FIELD(Vec3, lastKnown, {}, "Last known position");
    MEALKIT_FIELD(u32, knownTick, 0, "Tick the target was last seen or heard");
    MEALKIT_FIELD(bool, visible, false, "Seen this tick");
};

inline bool hasTarget(const Awareness& a) { return a.target != 0; }
inline void forget(Awareness& a) { a = {}; }

// Looks at the candidates and keeps the nearest one in the cone with a clear line of sight. `self` is the AI's own candidate id
// (skipped; 0 = it isn't in the list), `selfBody`/`selfGroup` its own bodies (the ray ignores them). Rays are cast only for
// candidates closer than the best so far. A target that has been out of sight for `forgetTicks` is forgotten.
// Returns whether something is in view now.
inline bool see(Awareness& a, const Eye& eye, const mealkit::physics::PhysicsWorld& world, u32 self, mealkit::physics::BodyId selfBody,
                const Candidate* candidates, u32 count, u32 tick, const Tuning& t, f32 targetRadius = 0.0f, u32 selfGroup = 0) {
    u32 best = Chooser::kNone;
    f32 bestDist2 = 0.0f;
    for (u32 i = 0; i < count; ++i) {
        const Candidate& c = candidates[i];
        if (c.id == 0 || c.id == self || !inCone(eye, c.position)) continue;
        const f32 d2 = mealkit::lengthSq(c.position - eye.position);
        if (best != Chooser::kNone && d2 >= bestDist2) continue;
        if (!lineClear(world, eye.position, c.position, selfBody, targetRadius, selfGroup)) continue;
        best = i;
        bestDist2 = d2;
    }
    a.visible = best != Chooser::kNone;
    if (a.visible) {
        a.target = candidates[best].id;
        a.lastKnown = candidates[best].position;
        a.knownTick = tick;
    } else if (a.target != 0 && tick - a.knownTick >= t.forgetTicks) {
        forget(a);
    }
    return a.visible;
}

// ---- Hearing

inline constexpr u32 kMaxNoises = 16;

MEALKIT_STRUCT(Noise) {
    MEALKIT_FIELD(Vec3, position, {}, "Where");
    MEALKIT_FIELD(f32, radius, 0.0f, "How far it carries (m)");
    MEALKIT_FIELD(u32, source, 0, "Who made it (game id, 0 = none)");
    MEALKIT_FIELD(u32, tick, 0, "Tick it was made");
};

// The recent noises of the world: a ring that overwrites the oldest. Put one in WorldState.
MEALKIT_STRUCT(NoiseList) {
    MEALKIT_ARRAY(Noise, kMaxNoises, items, "Noises");
    MEALKIT_FIELD(u32, next, 0, "Next slot to write");
};

inline void emit(NoiseList& list, Vec3 position, f32 radius, u32 source, u32 tick) {
    Noise& n = list.items[list.next];
    n.position = position;
    n.radius = radius;
    n.source = source;
    n.tick = tick;
    list.next = (list.next + 1) % kMaxNoises;
}

// The nearest noise this listener can hear: within radius * sensitivity (1.0 = normal ears), not older than `noiseTicks`, not its own
// (`self` = the listener's game id). Walls don't muffle it. Returns false when there is none.
inline bool hear(const NoiseList& list, Vec3 listener, f32 sensitivity, u32 self, u32 tick, const Tuning& t, Noise& out) {
    bool found = false;
    f32 bestDist2 = 0.0f;
    for (u32 i = 0; i < kMaxNoises; ++i) {
        const Noise& n = list.items[i];
        if (n.radius <= 0.0f || tick - n.tick > t.noiseTicks || (self != 0 && n.source == self)) continue;
        const f32 reach = n.radius * sensitivity;
        const f32 d2 = mealkit::lengthSq(n.position - listener);
        if (d2 > reach * reach || (found && d2 >= bestDist2)) continue;
        out = n;
        bestDist2 = d2;
        found = true;
    }
    return found;
}

// A heard noise updates the memory of where something is, unless the target is in view (sight is better).
inline void notice(Awareness& a, const Noise& n, u32 tick) {
    if (a.visible) return;
    a.target = n.source;
    a.lastKnown = n.position;
    a.knownTick = tick;
}

} // namespace ai
