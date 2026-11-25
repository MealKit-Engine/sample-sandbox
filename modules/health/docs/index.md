# Health

Hit points, damage, healing, invulnerability and death with a respawn wait. Part of the combat set (`tags`, `stats`, `effects`, `health`). Values are fixed-point integers (1000 = 1.0), the same scale as `stats`, so max health can come from a stat.

- Version 0.2.0, engine 0.9, no required modules
- State: `health::Health`
- Simulation: `sim/Health.h` (state, damage), `sim/Hit.h` (hit queries), screen: `view/HealthView.h`
- UI: `modules/health/ui/health_bar.ui` (bind `healthRatio`, `health::ratio`), `modules/health/ui/damage_number.ui` (bind `damageText`)

## Adding

```
tools\module.ps1 add -Project <your project> health
```

```cpp
MEALKIT_FIELD(health::Health, health, {}, "Health");

health::init(pawn.health, stats::value(pawn.stats, kMaxHealth));
switch (health::damage(pawn.health, amount, attackerId, kBullet, tick)) {
case health::DamageResult::Killed:
    health::startRespawn(pawn.health, tick, respawnTicks); // then remove the Pawn
    break;
default: break;
}
if (health::respawnReady(pawn.health, tick)) health::revive(pawn.health, tick, 0, spawnProtectTicks);
```

## Hit queries

`sim/Hit.h` decides who an attack touches from shapes you pass in (no physics needed, no `sqrt`, so the result is bit-identical on every peer). A shape is a segment with a radius: `sphere(center, r)`, `capsule(bottom, top, r)`, `ray(origin, dir, length)` (radius 0).

```cpp
health::Target targets[kMax];             // fill in Pool id order: {pawn slot id, health::capsule(feet, head, 0.4f)}
health::HitList hits;
health::collect(health::sphere(fistPos, 0.8f), targets, n, attackerId, hits); // melee/area: all touched, in list order
health::HitEntry e;
if (health::first(health::ray(muzzle, dir, 60.0f), targets, n, attackerId, e)) // hitscan: nearest along the ray
    health::damage(pawnOf(e.id).health, 25000, attackerId, kBullet, tick);
```

Walls are not considered: for line-of-sight use `PhysicsWorld::castRay` first and shorten the ray to its hit distance.

## Rules

- Damage is ignored while dead, invulnerable, or `amount <= 0`. `Killed` is returned once, on the hit that brings health to 0.
- Healing never revives and never passes max. `setMax(h, max, keepFraction)` follows a changed max stat.
- Everything counts in ticks, so it behaves the same at any frame rate.

## Reference

| Function | What it does |
|---|---|
| `init`, `revive` | Full health / back to life with spawn protection |
| `damage`, `heal`, `kill` | Changing health |
| `setMax`, `makeInvulnerable` | Max health, protection |
| `startRespawn`, `respawnReady`, `respawnRemaining` | Respawn wait |
| `alive`, `invulnerable`, `fraction` | Queries (`fraction` is for the view) |
| `collect`, `first`, `overlap` (`Hit.h`) | Hit queries |
| `ratio`, `low`, `points` (`HealthView.h`) | View values |
