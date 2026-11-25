# Stats

Attributes with a base value and modifiers (speed, armor, max health), recomputed in one fixed order. All values are **fixed-point integers, 1000 = 1.0**, so results are bit-identical on every platform. Part of the combat set (`tags`, `stats`, `effects`, `health`).

- Version 0.1.0, engine 0.9, no required modules
- State: `stats::Stats` (16 `Stat`: `base`, `value`, `min`, `max`)
- Simulation: `sim/Stats.h`

## Evaluation order

Whatever order modifiers are listed in:

1. `Add`: flat values are summed and added to the base
2. `PercentAdd`: percents are summed and applied once (`+100` and `+200` = +30%)
3. `Multiply`: each factor is applied in turn (64-bit intermediates, division truncates toward zero)
4. `Override`: the last one in list order replaces the result

Then the value is clamped to `[min, max]`.

## Adding

```
tools\module.ps1 add -Project <your project> stats
```

```cpp
// source/sim/World.h
MEALKIT_FIELD(stats::Stats, stats, {}, "Stats");

stats::define(pawn.stats, kSpeed, stats::fromWhole(7), 0, stats::fromWhole(20));
const stats::Modifier mods[] = {stats::mod(kSpeed, stats::ModOp::PercentAdd, 250)};
stats::recompute(pawn.stats, mods, 1);
float speed = stats::toFloat(stats::value(pawn.stats, kSpeed)); // convert only on the view side
```

Normally the `effects` module collects the modifiers for you.

## Reference

| Function | What it does |
|---|---|
| `define(stats, index, base, min, max)` | Sets base and range |
| `mod(stat, op, amount)` | Builds a `Modifier` (reflected structs aren't aggregates) |
| `recompute(stats, mods, n)` | Recomputes every value |
| `value(stats, index)`, `fromWhole(n)`, `toFloat(v)` | Reading and conversion |
