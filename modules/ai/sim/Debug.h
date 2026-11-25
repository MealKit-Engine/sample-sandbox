// Vendored copy of module-ai 0.1.0; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "ai/sim/Perception.h"
#include "ai/sim/Route.h"

#include "mealkit/core/DetMath.h"
#include "mealkit/debug/DebugDraw.h"

// Debug drawing for AI (lines through mealkit/debug/DebugDraw.h, nothing in a build without debug drawing; never reads or changes state):
// the sight cone, what an AI knows, and its path. Call it from the AI's own system when the game's debug tuning is on.
namespace ai {

// Cone edges (left and right of forward on the XZ plane) and the far arc's ends, drawn at eye height. `halfAngle` in radians.
inline void drawEye(const Eye& eye, mealkit::f32 halfAngle, mealkit::u32 color = mealkit::debug::kYellow) {
    const mealkit::f32 s = mealkit::detSin(halfAngle), c = mealkit::detCos(halfAngle);
    const Vec3 f = eye.forward;
    const Vec3 left{f.x * c - f.z * s, f.y, f.x * s + f.z * c};
    const Vec3 right{f.x * c + f.z * s, f.y, -f.x * s + f.z * c};
    mealkit::debug::line(eye.position, eye.position + left * eye.range, color);
    mealkit::debug::line(eye.position, eye.position + right * eye.range, color);
    mealkit::debug::line(eye.position + left * eye.range, eye.position + right * eye.range, color);
}

// Line to the target (green while seen, orange when only remembered) and a cross at the last known position.
inline void drawAwareness(const Eye& eye, const Awareness& a) {
    if (!hasTarget(a)) return;
    const mealkit::u32 color = a.visible ? mealkit::debug::kGreen : mealkit::debug::kYellow;
    const mealkit::f32 h = 0.3f; // const-ok: size of the cross
    mealkit::debug::line(eye.position, a.lastKnown, color);
    mealkit::debug::line(a.lastKnown - Vec3{h, 0.0f, 0.0f}, a.lastKnown + Vec3{h, 0.0f, 0.0f}, color);
    mealkit::debug::line(a.lastKnown - Vec3{0.0f, 0.0f, h}, a.lastKnown + Vec3{0.0f, 0.0f, h}, color);
}

// The path still to walk, from the feet through the remaining waypoints.
inline void drawRoute(const Route& r, Vec3 feet, mealkit::u32 color = mealkit::debug::kGreen) {
    Vec3 from = feet;
    for (mealkit::u32 k = r.next; k < r.count; ++k) {
        mealkit::debug::line(from, r.points[k], color);
        from = r.points[k];
    }
}

} // namespace ai
