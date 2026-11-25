// Vendored copy of module-health 0.2.0; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "mealkit/core/Math.h"
#include "mealkit/core/Reflect.h"

// Hit queries (docs/index.md): who does an attack touch? Pure math on shapes the game passes in, so the module needs no physics
// and the result depends only on the numbers. Only + - * / and comparisons are used (no sqrt, no trig), so results are bit-identical.
// A shape is a segment a..b with a radius: a sphere is a == b, a capsule is a..b with its radius, and a ray (hitscan) is a..b with radius 0.
// The caller passes targets in id order (Pool iteration order), and results keep that order, so every peer gets the same list.
namespace health {

using mealkit::f32;
using mealkit::u32;
using mealkit::Vec3;

struct Shape {
    Vec3 a{};
    Vec3 b{};
    f32 radius = 0.0f;
};

inline Shape sphere(Vec3 center, f32 radius) { return {center, center, radius}; }
inline Shape capsule(Vec3 bottom, Vec3 top, f32 radius) { return {bottom, top, radius}; }
inline Shape ray(Vec3 origin, Vec3 direction, f32 length) { return {origin, origin + direction * length, 0.0f}; } // direction should be unit length

struct SegmentPoint {
    f32 s = 0.0f; // parameter on the first segment, 0..1
    f32 distSq = 0.0f;
};

// Closest points between segments p1..q1 and p2..q2 (Ericson, Real-Time Collision Detection 5.1.9): squared distance and the
// parameter on the first segment (how far along a ray the closest point is).
inline SegmentPoint closestSegments(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2) {
    const Vec3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
    const f32 a = mealkit::dot(d1, d1), e = mealkit::dot(d2, d2), f = mealkit::dot(d2, r);
    constexpr f32 kEps = 1e-12f; // const-ok: degenerate segment threshold
    f32 s = 0.0f, t = 0.0f;
    if (a <= kEps && e <= kEps) {
        // both are points
    } else if (a <= kEps) {
        t = f / e;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    } else {
        const f32 c = mealkit::dot(d1, r);
        if (e <= kEps) {
            s = -c / a;
            s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
        } else {
            const f32 b = mealkit::dot(d1, d2);
            const f32 denom = a * e - b * b;
            if (denom > kEps) {
                s = (b * f - c * e) / denom;
                s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
            }
            t = (b * s + f) / e;
            if (t < 0.0f) {
                t = 0.0f;
                s = -c / a;
                s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
            } else if (t > 1.0f) {
                t = 1.0f;
                s = (b - c) / a;
                s = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s);
            }
        }
    }
    const Vec3 c1 = p1 + d1 * s, c2 = p2 + d2 * t;
    return {s, mealkit::lengthSq(c1 - c2)};
}

// Do the shapes touch? (Touching exactly counts.)
inline bool overlap(const Shape& x, const Shape& y) {
    const f32 r = x.radius + y.radius;
    return closestSegments(x.a, x.b, y.a, y.b).distSq <= r * r;
}

struct Target {
    u32 id = 0; // the game's id for the thing (pawn id slot), reported back
    Shape shape;
};

struct HitEntry {
    u32 id = 0;
    f32 along = 0.0f; // 0..1 along the attack segment (for rays: nearest first when sorted)
};

constexpr u32 kMaxHits = 32;

struct HitList {
    HitEntry hit[kMaxHits];
    u32 count = 0;
};

// All targets the attack touches, in the order given (skip the attacker by id). Stops at kMaxHits.
inline void collect(const Shape& attack, const Target* targets, u32 n, u32 skipId, HitList& out) {
    out.count = 0;
    for (u32 i = 0; i < n && out.count < kMaxHits; ++i) {
        const Target& t = targets[i];
        if (t.id == skipId) continue;
        const f32 r = attack.radius + t.shape.radius;
        const SegmentPoint c = closestSegments(attack.a, attack.b, t.shape.a, t.shape.b);
        if (c.distSq <= r * r) out.hit[out.count++] = {t.id, c.s};
    }
}

// Hitscan: the touched target nearest to the start of the ray (ties: the earlier target in the list). Returns false if none.
inline bool first(const Shape& attack, const Target* targets, u32 n, u32 skipId, HitEntry& result) {
    bool found = false;
    for (u32 i = 0; i < n; ++i) {
        const Target& t = targets[i];
        if (t.id == skipId) continue;
        const f32 r = attack.radius + t.shape.radius;
        const SegmentPoint c = closestSegments(attack.a, attack.b, t.shape.a, t.shape.b);
        if (c.distSq <= r * r && (!found || c.s < result.along)) {
            result = {t.id, c.s};
            found = true;
        }
    }
    return found;
}

} // namespace health
