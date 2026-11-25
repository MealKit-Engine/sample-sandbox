// Vendored copy of module-health 0.2.0; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "mealkit/core/Reflect.h"
#include "mealkit/sim/TickTimer.h"

// Health (docs/index.md): hit points, damage, healing, invulnerability and death with a respawn wait.
// Values are fixed-point integers, 1000 = 1.0, like the stats module (so max health can come from a stat). State goes in a
// Pawn or WorldState field; functions take only that state and the tick number, and count only in ticks.
// Hit queries (who was hit) are the game's job (physics queries, in id order); this module decides what a hit does.
namespace health {

using mealkit::i32;
using mealkit::u32;
using mealkit::u8;

MEALKIT_ENUM(DamageResult, u8, Ignored, Hit, Killed)

MEALKIT_STRUCT(Health) {
    MEALKIT_FIELD(i32, current, 0, "Current health");
    MEALKIT_FIELD(i32, max, 0, "Max health");
    MEALKIT_FIELD(mealkit::sim::TickTimer, invulnerable, {}, "Invulnerable until");
    MEALKIT_FIELD(bool, dead, false, "Dead");
    MEALKIT_FIELD(mealkit::sim::TickTimer, respawn, {}, "Respawn allowed at");
    MEALKIT_FIELD(u32, lastAttacker, 0, "Game id of the last attacker (0 = none)");
    MEALKIT_FIELD(u8, lastKind, 0, "Damage kind of the last hit");
};

// Full health. Use at spawn.
inline void init(Health& h, i32 max) {
    h = {};
    h.max = max < 0 ? 0 : max;
    h.current = h.max;
}

inline bool alive(const Health& h) { return !h.dead; }
inline bool invulnerable(const Health& h, u32 tick) { return !h.invulnerable.ready(tick); }
// 0..1 for a health bar (view)
inline float fraction(const Health& h) { return h.max > 0 ? static_cast<float>(h.current) / static_cast<float>(h.max) : 0.0f; }

// Damage of `amount` (> 0) from `attacker` of game-defined `kind`. Ignored while dead or invulnerable (or amount <= 0).
// Killed is returned once, on the hit that brings health to 0; the game then removes the Pawn / starts the respawn wait.
inline DamageResult damage(Health& h, i32 amount, u32 attacker, u8 kind, u32 tick) {
    if (h.dead || amount <= 0 || invulnerable(h, tick)) return DamageResult::Ignored;
    h.lastAttacker = attacker;
    h.lastKind = kind;
    if (amount >= h.current) {
        h.current = 0;
        h.dead = true;
        return DamageResult::Killed;
    }
    h.current -= amount;
    return DamageResult::Hit;
}

// Healing never revives and never goes over max. Returns the amount actually healed.
inline i32 heal(Health& h, i32 amount) {
    if (h.dead || amount <= 0) return 0;
    const i32 room = h.max - h.current;
    const i32 got = amount < room ? amount : room;
    h.current += got;
    return got;
}

// Changes max health (a stat changed). keepFraction scales current by the same ratio (64-bit, truncating); otherwise current is only clamped.
inline void setMax(Health& h, i32 max, bool keepFraction = false) {
    max = max < 0 ? 0 : max;
    if (keepFraction && h.max > 0 && !h.dead) {
        h.current = static_cast<i32>(static_cast<mealkit::i64>(h.current) * max / h.max);
        if (h.current < 1 && max > 0) h.current = 1; // a scaled-down but living pawn stays alive
    }
    h.max = max;
    if (h.current > h.max) h.current = h.max;
}

inline void makeInvulnerable(Health& h, u32 tick, u32 ticks) { h.invulnerable.start(tick, ticks); }

// Death by the game (out of bounds, etc.). Returns false if already dead.
inline bool kill(Health& h, u32 attacker = 0, u8 kind = 0) {
    if (h.dead) return false;
    h.current = 0;
    h.dead = true;
    h.lastAttacker = attacker;
    h.lastKind = kind;
    return true;
}

// Respawn wait: the game calls startRespawn when it handles Killed, polls respawnReady each tick, then revive() when it makes the new Pawn.
inline void startRespawn(Health& h, u32 tick, u32 delay) { h.respawn.start(tick, delay); }
inline bool respawnReady(const Health& h, u32 tick) { return h.dead && h.respawn.ready(tick); }
inline u32 respawnRemaining(const Health& h, u32 tick) { return h.respawn.remaining(tick); }

// Back to life with `current` health (0 = full), and spawn protection of `protectTicks`.
inline void revive(Health& h, u32 tick, i32 current = 0, u32 protectTicks = 0) {
    h.dead = false;
    h.current = (current <= 0 || current > h.max) ? h.max : current;
    h.respawn = {};
    h.invulnerable = {};
    if (protectTicks) h.invulnerable.start(tick, protectTicks);
}

} // namespace health
