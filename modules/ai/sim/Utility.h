// Vendored copy of module-ai 0.2.0; change it in that repo (tools\module.ps1 update), not here.
#pragma once

#include "mealkit/core/Reflect.h"
#include "mealkit/sim/TickTimer.h"

// Utility scoring and an enum state machine (docs/index.md). Scores are fixed-point integers, 1000 = 1.0 (like the stats and health
// modules), so a decision is bit-identical on every peer. The game keeps its own `enum` of states (any u8 values) and one `Brain`
// per AI Controller; this header only decides when to switch.
//
//   ai::Chooser pick(brain.state);                          // scores of this tick's options, offered in a fixed (id) order
//   pick.offer(kWander, 300);
//   pick.offer(kChase, ai::Score().mul(visible).mul(near).done(2));
//   if (ai::think(brain, pick, tick, tuning)) onEnter(brain.state);
namespace ai {

using mealkit::i32;
using mealkit::i64;
using mealkit::u32;
using mealkit::u8;

inline constexpr i32 kOne = 1000; // 1.0

MEALKIT_STRUCT(Tuning) {
    MEALKIT_FIELD(u32, thinkTicks, 20, "Ticks between decisions");
    MEALKIT_FIELD(u32, minStayTicks, 30, "Least ticks in a state before another can take over");
    MEALKIT_FIELD(i32, switchMargin, 100, "A new option must beat the current one by this much (1000 = 1.0)");
    MEALKIT_FIELD(u32, forgetTicks, 120, "Ticks until a target that is no longer seen is forgotten");
    MEALKIT_FIELD(u32, noiseTicks, 30, "Ticks a noise can still be heard");
};

// Response curves: input and output are 0..1000
enum class Curve : u8 {
    Linear,         // x
    Quadratic,      // x²: grows slowly at first
    Inverse,        // 1 - x: high when the input is low (distance, health)
    InverseQuadratic, // (1 - x)²
    Step            // 1 once x >= param, else 0
};

inline i32 clamp01(i32 x) { return x < 0 ? 0 : (x > kOne ? kOne : x); }

// value mapped from [lo, hi] to 0..1000 (clamped). hi <= lo gives 0.
inline i32 normalize(i32 value, i32 lo, i32 hi) {
    if (hi <= lo) return 0;
    return clamp01(static_cast<i32>(static_cast<i64>(value - lo) * kOne / (hi - lo)));
}

inline i32 response(Curve curve, i32 x, i32 param = 0) {
    x = clamp01(x);
    switch (curve) {
    case Curve::Linear: return x;
    case Curve::Quadratic: return static_cast<i32>(static_cast<i64>(x) * x / kOne);
    case Curve::Inverse: return kOne - x;
    case Curve::InverseQuadratic: return static_cast<i32>(static_cast<i64>(kOne - x) * (kOne - x) / kOne);
    case Curve::Step: return x >= param ? kOne : 0;
    }
    return 0;
}

// Score of one option: considerations (each 0..1000) multiplied, so any zero rules the option out. done(n) adds back what the
// product loses as n grows (an option with 4 good considerations isn't punished against one with 1).
struct Score {
    i32 value = kOne;
    Score& mul(i32 consideration) {
        value = static_cast<i32>(static_cast<i64>(value) * clamp01(consideration) / kOne);
        return *this;
    }
    // `count` = how many considerations were multiplied
    i32 done(u32 count) {
        if (count > 1) {
            const i64 make = static_cast<i64>(kOne - value) * (kOne - kOne / static_cast<i32>(count)) / kOne; // 1 - 1/n of the loss
            value += static_cast<i32>(make * value / kOne);
        }
        return value;
    }
};

// This tick's options. Offer them in a fixed order (their id order): on a tie the first one offered wins.
struct Chooser {
    static constexpr u32 kNone = 0xFFFFFFFFu;
    u32 current;          // the state the brain is in
    u32 best = kNone;
    i32 bestScore = -1;
    i32 currentScore = 0; // score offered for `current` (0 when not offered)

    explicit Chooser(u32 currentState) : current(currentState) {}
    void offer(u32 id, i32 score) {
        if (id == current) currentScore = score;
        if (score > bestScore) {
            best = id;
            bestScore = score;
        }
    }
};

// One per AI Controller. `state` is the game's own enum value.
MEALKIT_STRUCT(Brain) {
    MEALKIT_FIELD(u32, state, 0, "State");
    MEALKIT_FIELD(u32, previous, 0, "Previous state");
    MEALKIT_FIELD(u32, enteredTick, 0, "Tick the state was entered");
    MEALKIT_FIELD(mealkit::sim::TickTimer, nextThink, {}, "Next decision");
};

inline bool thinkReady(const Brain& b, u32 tick) { return b.nextThink.ready(tick); }
inline u32 stateTicks(const Brain& b, u32 tick) { return tick - b.enteredTick; }

// Forces a state (match start, a scripted change). Resets the stay time.
inline void enter(Brain& b, u32 state, u32 tick) {
    b.previous = b.state;
    b.state = state;
    b.enteredTick = tick;
}

// A decision every `thinkTicks`: switches to the best option when it beats the current one by `switchMargin` and the current state has
// lasted `minStayTicks` (no flapping). Offer the scores every tick the brain is ready (see thinkReady) and skip the work otherwise.
// Returns true when the state changed.
inline bool think(Brain& b, const Chooser& pick, u32 tick, const Tuning& t) {
    if (!b.nextThink.ready(tick)) return false;
    b.nextThink.start(tick, t.thinkTicks);
    if (pick.best == Chooser::kNone || pick.best == b.state) return false;
    if (stateTicks(b, tick) < t.minStayTicks) return false;
    if (pick.bestScore < pick.currentScore + t.switchMargin) return false;
    enter(b, pick.best, tick);
    return true;
}

} // namespace ai
