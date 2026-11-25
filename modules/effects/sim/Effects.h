// Vendored copy of module-effects 0.1.1; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "mealkit/core/Reflect.h"
#include "stats/sim/Stats.h"
#include "tags/sim/Tags.h"

#include <array>

// Effects (docs/index.md): timed or permanent modifiers on stats, stacking rules, periodic ticks and granted tags.
// Definitions are table rows (EffectDef, ids are row index + 1, 0 = none). Active effects are a flat array in slot order:
// no components attached at run time. Everything counts in ticks. step() does the same work in the same order on every peer:
//   1. expire effects whose time is up (slot order)  2. fire periodic ticks (slot order)  3. if anything changed, rebuild stats and tags.
namespace effects {

using mealkit::i32;
using mealkit::u16;
using mealkit::u32;
using mealkit::u8;

constexpr u32 kMaxActive = 32;

// What happens when an effect is applied while one of the same kind (and same source) is already on the target.
//   Refresh: keep one stack, restart the timer.   Stack: add a stack (up to maxStacks) and restart the timer.   Ignore: keep the old one untouched.
MEALKIT_ENUM(Stacking, u8, Refresh, Stack, Ignore)

MEALKIT_STRUCT(EffectDef) {
    MEALKIT_FIELD(u32, duration, 0, "Duration (ticks, 0 = permanent)");
    MEALKIT_FIELD(Stacking, stacking, Stacking::Refresh, "Stacking rule");
    MEALKIT_FIELD(u8, maxStacks, 1, "Max stacks");
    MEALKIT_FIELD(u8, stat, 0, "Modified stat index");
    MEALKIT_FIELD(stats::ModOp, op, stats::ModOp::Add, "Modifier operation");
    MEALKIT_FIELD(i32, amount, 0, "Modifier amount per stack (1000 = 1.0); Add/PercentAdd 0 = no modifier");
    MEALKIT_FIELD(u16, grantTag, 0, "Tag id granted while active (0 = none)");
    MEALKIT_FIELD(u32, period, 0, "Periodic tick every N ticks (0 = none)");
    MEALKIT_FIELD(i32, periodAmount, 0, "Amount reported per periodic tick, per stack");
};

MEALKIT_STRUCT(ActiveEffect) {
    MEALKIT_FIELD(u16, def, 0, "Definition id (0 = free slot)");
    MEALKIT_FIELD(u8, stacks, 0, "Stacks");
    MEALKIT_FIELD(u32, source, 0, "Who applied it (game id, 0 = none)");
    MEALKIT_FIELD(u32, expiresAt, 0, "Tick it ends (0 = permanent)");
    MEALKIT_FIELD(u32, nextPeriod, 0, "Tick of the next periodic tick (0 = none)");
};

MEALKIT_STRUCT(EffectList) {
    MEALKIT_ARRAY(ActiveEffect, kMaxActive, active, "Active effects");
    MEALKIT_FIELD(tags::TagCounts, granted, {}, "Tags granted by effects");
    MEALKIT_FIELD(bool, dirty, false, "Stats and tags need rebuilding");
};

// What a periodic tick reports; the game turns it into damage, healing, etc. (this module doesn't know health).
struct PeriodicHit {
    u16 def = 0;
    u32 source = 0;
    i32 amount = 0; // periodAmount * stacks
};
struct PeriodicHits {
    std::array<PeriodicHit, kMaxActive> hit{};
    u32 count = 0;
};

// Table rows hold tag names (text); resolve them to ids once at match start, after tags::Registry::build:
//   EffectDef d = rowToDef(row); effects::setGrantTag(d, registry, row.grantTag.c_str());
// Returns false (and sets no tag) if the name isn't in the registry; an empty name means "no tag" and is fine.
inline bool setGrantTag(EffectDef& d, const tags::Registry& reg, const char* name) {
    d.grantTag = 0;
    if (!name || !*name) return true;
    const u16 id = reg.find(name);
    d.grantTag = id;
    return id != tags::kNone;
}

inline const EffectDef* defOf(const EffectDef* defs, u32 nDefs, u16 id) { return id >= 1 && id <= nDefs ? &defs[id - 1] : nullptr; }

// Applies definition `id` at `tick`. Same definition and same source = "the same effect" for stacking. Returns false if the id is
// bad, the rule is Ignore and it is already there, or all slots are used.
inline bool apply(EffectList& list, const EffectDef* defs, u32 nDefs, u16 id, u32 source, u32 tick) {
    const EffectDef* d = defOf(defs, nDefs, id);
    if (!d) return false;
    const u32 expires = d->duration ? tick + d->duration : 0;
    const u32 next = d->period ? tick + d->period : 0;
    ActiveEffect* freeSlot = nullptr;
    for (ActiveEffect& a : list.active) {
        if (a.def == 0) {
            if (!freeSlot) freeSlot = &a;
            continue;
        }
        if (a.def != id || a.source != source) continue;
        if (d->stacking == Stacking::Ignore) return false;
        if (d->stacking == Stacking::Stack && a.stacks < (d->maxStacks ? d->maxStacks : 1)) ++a.stacks;
        a.expiresAt = expires; // a periodic effect keeps its own rhythm
        list.dirty = true;
        return true;
    }
    if (!freeSlot) return false;
    ActiveEffect& a = *freeSlot;
    a = {};
    a.def = id;
    a.stacks = 1;
    a.source = source;
    a.expiresAt = expires;
    a.nextPeriod = next;
    list.dirty = true;
    return true;
}

// Removes every active effect of definition `id` (cleanse). Returns how many slots were cleared.
inline u32 remove(EffectList& list, u16 id) {
    u32 n = 0;
    for (ActiveEffect& a : list.active)
        if (a.def == id) {
            a = {};
            ++n;
        }
    if (n) list.dirty = true;
    return n;
}
// Removes everything an owner applied (the caster died).
inline u32 removeFromSource(EffectList& list, u32 source) {
    u32 n = 0;
    for (ActiveEffect& a : list.active)
        if (a.def != 0 && a.source == source) {
            a = {};
            ++n;
        }
    if (n) list.dirty = true;
    return n;
}
inline void clear(EffectList& list) { list = {}; }

inline u32 stacksOf(const EffectList& list, u16 id) {
    u32 n = 0;
    for (const ActiveEffect& a : list.active)
        if (a.def == id) n += a.stacks;
    return n;
}
// Effect query by tag: how many active effects grant `tag` or a tag under it.
inline u32 countGranting(const EffectList& list, const EffectDef* defs, u32 nDefs, const tags::Registry& reg, u16 tag) {
    u32 n = 0;
    for (const ActiveEffect& a : list.active) {
        const EffectDef* d = defOf(defs, nDefs, a.def);
        if (d && d->grantTag && reg.isUnder(d->grantTag, tag)) ++n;
    }
    return n;
}

// Rebuilds stat values and granted tags from the active effects (done by step() when dirty; exposed for loading snapshots).
inline void rebuild(EffectList& list, const EffectDef* defs, u32 nDefs, const tags::Registry& reg, stats::Stats& s) {
    std::array<stats::Modifier, kMaxActive> mods;
    u32 n = 0;
    tags::clear(list.granted);
    for (const ActiveEffect& a : list.active) {
        const EffectDef* d = defOf(defs, nDefs, a.def);
        if (!d) continue;
        if ((d->amount != 0 || d->op == stats::ModOp::Override || d->op == stats::ModOp::Multiply) && n < mods.size()) { // Add/PercentAdd of 0 = no modifier
            i32 amount = d->amount;
            if (d->op == stats::ModOp::Add || d->op == stats::ModOp::PercentAdd) {
                amount = d->amount * a.stacks;
            } else if (d->op == stats::ModOp::Multiply) {
                mealkit::i64 f = stats::kOne;
                for (u32 i = 0; i < a.stacks; ++i) f = f * d->amount / stats::kOne;
                amount = static_cast<i32>(f);
            }
            mods[n++] = stats::mod(d->stat, d->op, amount);
        }
        if (d->grantTag) tags::add(list.granted, reg, d->grantTag);
    }
    stats::recompute(s, mods.data(), n);
    list.dirty = false;
}

// Once per tick, after the game applied this tick's new effects. `out` is cleared first.
inline void step(EffectList& list, const EffectDef* defs, u32 nDefs, const tags::Registry& reg, u32 tick, stats::Stats& s,
                 PeriodicHits& out) {
    out.count = 0;
    for (ActiveEffect& a : list.active) {
        if (a.def == 0) continue;
        if (a.expiresAt != 0 && tick >= a.expiresAt) {
            a = {};
            list.dirty = true;
            continue;
        }
        const EffectDef* d = defOf(defs, nDefs, a.def);
        if (!d) {
            a = {};
            list.dirty = true;
            continue;
        }
        if (a.nextPeriod != 0 && d->period != 0) {
            while (tick >= a.nextPeriod && out.count < out.hit.size()) { // catches up if called late, never loops forever
                PeriodicHit& h = out.hit[out.count++];
                h.def = a.def;
                h.source = a.source;
                h.amount = d->periodAmount * a.stacks;
                a.nextPeriod += d->period;
            }
        }
    }
    if (list.dirty) rebuild(list, defs, nDefs, reg, s);
}

} // namespace effects
