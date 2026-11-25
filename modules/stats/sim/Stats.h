// Vendored copy of module-stats 0.1.0; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "mealkit/core/Reflect.h"

#include <array>

// Stats (docs/index.md): attributes with a base value and modifiers, recomputed in one fixed order.
// All values are fixed-point integers, 1000 = 1.0 (no floats, so results are bit-identical on every platform).
// Order of evaluation is fixed and spelled out at ModOp below.
namespace stats {

using mealkit::i32;
using mealkit::i64;
using mealkit::u32;
using mealkit::u8;

constexpr i32 kOne = 1000; // fixed-point 1.0
constexpr u32 kMaxStats = 16;

// What a modifier does. Applied in this order whatever the order modifiers were added in:
//   1. Add:        flat sum added to the base            (+5 armor)
//   2. PercentAdd: percents are summed, then applied once (+10% and +20% make +30%, in 1/1000: +100 and +200 make +300)
//   3. Multiply:   each factor applied one by one         (x1.5, then x0.5)
//   4. Override:   the last one in list order replaces the result (stun sets speed to 0)
// Then the result is clamped to [min, max].
MEALKIT_ENUM(ModOp, u8, Add, PercentAdd, Multiply, Override)

MEALKIT_STRUCT(Modifier) {
    MEALKIT_FIELD(u8, stat, 0, "Stat index");
    MEALKIT_FIELD(ModOp, op, ModOp::Add, "Operation");
    MEALKIT_FIELD(i32, amount, 0, "Amount (1000 = 1.0)");
};

// Reflected structs aren't aggregates: build a modifier with this.
inline Modifier mod(u32 stat, ModOp op, i32 amount) {
    Modifier m;
    m.stat = static_cast<u8>(stat);
    m.op = op;
    m.amount = amount;
    return m;
}

MEALKIT_STRUCT(Stat) {
    MEALKIT_FIELD(i32, base, 0, "Base value");
    MEALKIT_FIELD(i32, value, 0, "Current value (recomputed)");
    MEALKIT_FIELD(i32, min, -2000000000, "Lowest value");
    MEALKIT_FIELD(i32, max, 2000000000, "Highest value");
};

MEALKIT_STRUCT(Stats) {
    MEALKIT_ARRAY(Stat, kMaxStats, stat, "Stats");
};

// Sets a stat's base and range and makes the value equal to base (before any modifier).
inline void define(Stats& s, u32 index, i32 base, i32 min = -2000000000, i32 max = 2000000000) {
    if (index >= kMaxStats) return;
    s.stat[index].base = base;
    s.stat[index].min = min;
    s.stat[index].max = max;
    s.stat[index].value = base < min ? min : (base > max ? max : base);
}

inline i32 clamp64(i64 v, i32 lo, i32 hi) { return v < lo ? lo : (v > hi ? hi : static_cast<i32>(v)); }

// Recomputes every value from the bases and `mods` (in list order; the order only matters for Override). Call it when a
// modifier is added or removed, or once per tick. Division truncates toward zero. Modifiers with a bad stat index are ignored.
inline void recompute(Stats& s, const Modifier* mods, u32 count) {
    std::array<i64, kMaxStats> add{}, pct{}, factor{}, over{};
    std::array<bool, kMaxStats> hasOver{};
    factor.fill(kOne);
    for (u32 i = 0; i < count; ++i) {
        const Modifier& m = mods[i];
        if (m.stat >= kMaxStats) continue;
        switch (m.op) {
        case ModOp::Add: add[m.stat] += m.amount; break;
        case ModOp::PercentAdd: pct[m.stat] += m.amount; break;
        case ModOp::Multiply: factor[m.stat] = factor[m.stat] * m.amount / kOne; break;
        case ModOp::Override: over[m.stat] = m.amount; hasOver[m.stat] = true; break;
        }
    }
    for (u32 i = 0; i < kMaxStats; ++i) {
        Stat& st = s.stat[i];
        i64 v = static_cast<i64>(st.base) + add[i];
        v = v * (kOne + pct[i]) / kOne;
        v = v * factor[i] / kOne;
        if (hasOver[i]) v = over[i];
        st.value = clamp64(v, st.min, st.max);
    }
}

inline i32 value(const Stats& s, u32 index) { return index < kMaxStats ? s.stat[index].value : 0; }
inline float toFloat(i32 fixed) { return static_cast<float>(fixed) / kOne; } // view only, never in sim/
constexpr i32 fromWhole(i32 whole) { return whole * kOne; }

} // namespace stats
