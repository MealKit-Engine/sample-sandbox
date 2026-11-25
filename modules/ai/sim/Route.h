// Vendored copy of module-ai 0.2.0; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "mealkit/core/Math.h"
#include "mealkit/core/Reflect.h"
#include "mealkit/sim/NavGrid.h"

#include <cmath>

// Walking to a point along the engine's nav grid (mealkit/sim/NavGrid.h, docs/CODING_MODEL.md "Pathfinding"): a stored path of waypoints
// that is searched when the target changes and re-searched when it ran out before the target. The grid and NavSearch are the game's
// (not state); the path is. Searches per tick are limited by a budget the game passes in and spends in controller order.
namespace ai {

using mealkit::f32;
using mealkit::u32;
using mealkit::Vec3;

inline constexpr u32 kRoutePoints = 16; // waypoints kept (a longer path is re-searched from its end)

MEALKIT_STRUCT(Route) {
    MEALKIT_FIELD(Vec3, target, {}, "Target (feet)");
    MEALKIT_FIELD(bool, repath, false, "Waiting for a search");
    MEALKIT_ARRAY(Vec3, kRoutePoints, points, "Waypoints");
    MEALKIT_FIELD(u32, count, 0, "Waypoints");
    MEALKIT_FIELD(u32, next, 0, "Next waypoint");
};

// Walk to `target` (feet position). Clears the old path.
inline void setTarget(Route& r, Vec3 target) {
    r.target = target;
    r.repath = true;
    r.count = r.next = 0;
}

inline bool hasPath(const Route& r) { return r.next < r.count; }

// Distances and limits of one follower (game tuning).
struct FollowParams {
    f32 reach = 0.5f;        // a waypoint closer than this counts as reached (m)
    f32 stopDistance = 1.0f; // without a path, stop this close to the target (m)
    f32 waypointStop = 0.1f; // const-ok: default, with a path stop steering within this of the last waypoint (m)
    f32 speed = 1.0f;        // length of the returned direction
    u32 maxExpand = 4096;    // nav cells one search may look at
};

// One tick of following: runs a pending search when the budget allows (budget--), then returns the direction to walk, a unit vector
// on the XZ plane scaled by `speed`, or zero when it has arrived (or is waiting for a search). Without a grid it heads straight
// for the target.
inline Vec3 follow(Route& r, const mealkit::sim::NavGrid& grid, mealkit::sim::NavSearch& search, Vec3 feet, u32& budget,
                   const FollowParams& p) {
    if (r.repath && budget > 0 && !grid.empty()) {
        --budget;
        r.repath = false;
        u32 n = 0;
        mealkit::sim::findPath(grid, search, feet, r.target, r.points.data(), kRoutePoints, n, p.maxExpand);
        r.count = n;
        r.next = 0;
    }
    Vec3 goal = r.target;
    while (r.next < r.count) {
        const Vec3 w = r.points[r.next];
        const f32 dx = w.x - feet.x, dz = w.z - feet.z;
        if (dx * dx + dz * dz > p.reach * p.reach) {
            goal = w;
            break;
        }
        if (++r.next == r.count) { // end of a path cut short at kRoutePoints: search again from here
            const f32 tx = r.target.x - feet.x, tz = r.target.z - feet.z;
            if (r.count == kRoutePoints && tx * tx + tz * tz > p.stopDistance * p.stopDistance) r.repath = true;
            goal = feet; // stand still for now
        }
    }
    const Vec3 to{goal.x - feet.x, 0.0f, goal.z - feet.z};
    const f32 len2 = mealkit::lengthSq(to);
    const f32 stop = hasPath(r) ? p.waypointStop * p.waypointStop : p.stopDistance * p.stopDistance;
    if (len2 > stop && len2 > 0.0f) return to / std::sqrt(len2) * p.speed;
    return {};
}

} // namespace ai
