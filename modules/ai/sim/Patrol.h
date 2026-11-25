// Vendored copy of module-ai 0.2.0; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "mealkit/core/Math.h"
#include "mealkit/core/Reflect.h"

// Walking along a path placed in the scene (scene kind `path`, mealkit/scene/Scene.h readPath, docs/DECISIONS.md 2026-11-25 "E1 paths").
// Patrol keeps one distance along the path in whole millimeters, the "carrot" the AI walks to: when the AI is close to it the carrot moves on by
// stepMm. A closed path loops; an open one is walked there and back. The path itself is not state (the game reads it from the scene when a
// match starts); `Path` is any type with `valid()`, `length` and the functions `pathPoint(path, mm)` and `pathClosest(path, point)` found by
// argument-dependent lookup, which is what `mealkit::scene::ScenePath` has. Walking to the returned point is `setTarget` + `follow`.
namespace ai {

using mealkit::f32;
using mealkit::u32;
using mealkit::Vec3;

MEALKIT_STRUCT(Patrol) {
    MEALKIT_FIELD(u32, distance, 0, "Carrot distance along the path (mm)");
    MEALKIT_FIELD(bool, backward, false, "Walking back (open paths)");
    MEALKIT_FIELD(bool, started, false, "Started (the carrot was put at the closest point)");
};

struct PatrolParams {
    f32 reach = 0.8f;    // const-ok: default, game tuning sets it; closer than this (XZ, m) to the carrot moves it on
    u32 stepMm = 3000;   // const-ok: default, game tuning sets it; how far the carrot moves on (mm)
};

// Puts the carrot at the point of the path closest to `feet`, walking forward
template <class Path>
void patrolStart(Patrol& p, const Path& path, Vec3 feet) {
    p.distance = path.valid() ? pathClosest(path, feet) : 0;
    p.backward = false;
    p.started = true;
}

// Moves the carrot on when the AI reached it, and returns the point to walk to (the AI's own position when the path is unusable)
template <class Path>
Vec3 patrolTarget(Patrol& p, const Path& path, Vec3 feet, const PatrolParams& params) {
    if (!path.valid()) return feet;
    if (!p.started) patrolStart(p, path, feet);
    Vec3 goal = pathPoint(path, p.distance);
    const f32 dx = goal.x - feet.x, dz = goal.z - feet.z;
    if (dx * dx + dz * dz > params.reach * params.reach) return goal;
    const u32 step = params.stepMm > 0 ? params.stepMm : 1;
    if (path.closed) {
        p.distance = (p.distance + step) % path.length;
    } else if (p.backward) {
        if (p.distance <= step) { p.distance = 0; p.backward = false; }
        else p.distance -= step;
    } else if (p.distance + step >= path.length) {
        p.distance = path.length;
        p.backward = true;
    } else {
        p.distance += step;
    }
    return pathPoint(path, p.distance);
}

} // namespace ai
