# Gameplay tags

Hierarchical names such as `State.Stunned` turned into small integer ids when a match starts, plus counted grants. Use them for "can this pawn act?" checks and for finding effects ("everything under `State`"). Part of the combat set (`tags`, `stats`, `effects`, `health`).

- Version 0.1.0, engine 0.9, no required modules
- Match setup data: `tags::Registry` (not a field; rebuild it from the same name list at match start)
- State: `tags::TagCounts` (one `u8` per tag id)
- Simulation: `sim/Tags.h`

## How it works

- `Registry::build(names, n)` interns every name **and its parents** in sorted order, so a parent has a smaller id than its children and every peer gets the same ids whatever order the list had. Limit: 63 tags, 47 characters per name. No allocation.
- A grant counts for the tag and all its parents: while `State.Stunned` is granted, `has(State)` is true. Two sources need two `remove` calls.
- Id 0 means "no tag".

## Adding

```
tools\module.ps1 add -Project <your project> tags
```

```cpp
// match start (same list on every peer: a table, a constexpr array)
static const char* kTags[] = {"State.Stunned", "State.Dead", "Ability.Fire"};
tags::Registry registry;                 // lives in the match setup data, not in WorldState
registry.build(kTags, 3);

// source/sim/World.h
MEALKIT_FIELD(tags::TagCounts, tags, {}, "Tags on this pawn");

// systems
tags::add(pawn.tags, registry, registry.find("State.Stunned"));
if (tags::allows(pawn.tags, required, nRequired, blocked, nBlocked)) { /* can act */ }
```

## Reference

| Function | What it does |
|---|---|
| `Registry::build(names, n)` | Interns names and parents; false if a name is empty, too long, or there are too many |
| `find(name)`, `name(id)`, `parent(id)`, `isUnder(id, ancestor)` | Lookups |
| `add` / `remove(TagCounts&, registry, id)` | Counted grant, with parents |
| `has`, `hasAny`, `allows(required, blocked)` | Queries |
