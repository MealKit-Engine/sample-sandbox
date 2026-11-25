# Effects

Timed or permanent changes to stats: stacking rules, periodic ticks (poison, regeneration) and tags granted while active. Definitions are table rows; active effects are a flat array in slot order, no components attached at run time. Part of the combat set (`tags`, `stats`, `effects`, `health`).

- Version 0.1.1, engine 0.9, requires `tags`, `stats`
- Definition row: `effects::EffectDef` (ids are row index + 1, 0 = none)
- State: `effects::EffectList` (32 `ActiveEffect`, granted `TagCounts`)
- Simulation: `sim/Effects.h`

## Rules

- Same definition **and** same source is "the same effect". `Refresh` keeps one stack and restarts the timer, `Stack` adds a stack up to `maxStacks` (and restarts the timer), `Ignore` keeps the old one.
- `Add` and `PercentAdd` amounts are multiplied by the stack count, `Multiply` is applied once per stack.
- `step()` runs in one order: expire (slot order) → periodic ticks (slot order) → rebuild stats and tags if anything changed. A late call catches up on missed periodic ticks.
- A periodic tick doesn't change health itself: `step()` fills `PeriodicHits` (definition, source, `periodAmount * stacks`) and the game applies them (damage, healing). The module doesn't know `health`.
- Duration 0 = permanent. A full list (32) makes `apply` return false.

## From a table

Keep effects as rows of your own table (`std::string grantTag`, ticks, amounts), then at match start copy each row into an `EffectDef` and call `setGrantTag` after `tags::Registry::build`. Row order is file order, so ids are the same on every peer.

## Adding

```
tools\module.ps1 add -Project <your project> effects -WithRequires
```

```cpp
MEALKIT_FIELD(effects::EffectList, effects, {}, "Active effects");
MEALKIT_FIELD(stats::Stats, stats, {}, "Stats");
MEALKIT_FIELD(tags::TagCounts, tags, {}, "Base tags");

// per tick, for each pawn in id order, after this tick's apply() calls
effects::PeriodicHits hits;
effects::step(pawn.effects, defs, nDefs, registry, tick, pawn.stats, hits);
for (u32 i = 0; i < hits.count; ++i) health::damage(pawn.health, hits.hit[i].amount, hits.hit[i].source, kBurn, tick);
bool stunned = tags::has(pawn.effects.granted, stunnedId);
```

## Reference

| Function | What it does |
|---|---|
| `apply(list, defs, n, id, source, tick)` | Applies or stacks; false if refused |
| `remove(list, id)`, `removeFromSource(list, source)`, `clear(list)` | Cleanse |
| `step(list, defs, n, registry, tick, stats, hits)` | Once per tick |
| `setGrantTag(def, registry, name)` | Resolves a table row's tag name to an id at match start |
| `rebuild(...)` | Recompute after loading a snapshot |
| `stacksOf(list, id)`, `countGranting(list, defs, n, registry, tag)` | Queries |
