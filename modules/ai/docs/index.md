# Game AI

Decision making for AI characters: utility scoring, an `enum` state machine, sight and hearing, and walking along the engine's nav grid. No graph editor and no allocation; every number is a plain value in state, so hashes, snapshots and replays include it. Scores are fixed-point integers (1000 = 1.0), like `stats` and `health`.

- Version 0.2.0, engine 0.9, no required modules
- State: `ai::Brain`, `ai::Awareness`, `ai::Route` and `ai::Patrol` (one each per AI Controller), `ai::NoiseList` (one per match), tuning `ai::Tuning`
- Simulation: `sim/Utility.h` (curves, scores, brain), `sim/Perception.h` (sight, hearing, memory), `sim/Route.h` (path following), `sim/Patrol.h` (walking a scene `path` object)
- Debug drawing: `sim/Debug.h`

## Adding

```
tools\module.ps1 add -Project <your project> ai
```

```cpp
// source/sim/World.h
enum class AiState : u32 { Wander, Chase, Flee };
MEALKIT_FIELD(ai::Brain, brain, {}, "Brain");          // in Controller
MEALKIT_FIELD(ai::Awareness, aware, {}, "Awareness");  // in Controller
MEALKIT_FIELD(ai::Route, route, {}, "Route");          // in Controller
MEALKIT_FIELD(ai::NoiseList, noises, {}, "Noises");    // in WorldState
// source/sim/Tuning.h
MEALKIT_FIELD(ai::Tuning, ai, {}, "AI module");        // goes into game.tuning as "ai.thinkTicks 20"
```

One decision per AI Controller each tick, in Controller order (determinism: the same order on every peer):

```cpp
// 1. Senses. Candidates are the Pawns it could see, in Pool id order.
ai::Eye eye{pawn.position, forward, tuning.sightRange, tuning.cosHalfFov};
ai::see(c.aware, eye, world.physics, selfId, pawn.mover.body, candidates, n, tick, tuning.ai);
ai::Noise noise;
if (ai::hear(state.noises, pawn.position, 1.0f, pawnId, tick, tuning.ai, noise)) ai::notice(c.aware, noise, tick);

// 2. Decide. Each option is a score of 0..1000 made of considerations that are also 0..1000.
if (ai::thinkReady(c.brain, tick)) {
    const i32 near = ai::response(ai::Curve::Inverse, ai::normalize(distanceToTarget, 0, 12));   // closer = higher
    const i32 hurt = ai::response(ai::Curve::Inverse, healthFraction);                           // lower health = higher
    ai::Chooser pick(c.brain.state);                                    // offer in a fixed order
    pick.offer(+AiState::Wander, 300);
    pick.offer(+AiState::Chase, c.aware.target ? ai::Score().mul(near).mul(1000 - hurt).done(2) : 0);
    pick.offer(+AiState::Flee, c.aware.target ? ai::Score().mul(hurt).mul(near).done(2) : 0);
    if (ai::think(c.brain, pick, tick, tuning.ai)) { /* entered a new state: pick a path, play a bark */ }
}

// 3. Act by state. Walking goes along the nav grid.
switch (static_cast<AiState>(c.brain.state)) {
case AiState::Chase: ai::setTarget(c.route, c.aware.lastKnown); break;   // each time the target moved enough
...
}
const Vec3 wish = ai::follow(c.route, world.nav, world.navSearch, feet, pathBudget, params);
```

Patrol: a scene object of kind `path` (placed and edited in MkRender) is read once when the match starts (`scene::readPath(entity)` on an object of the flattened scene). Each controller keeps an `ai::Patrol` (a distance in millimeters, the "carrot"); at the moments it picks a new target it asks for the point to walk to and hands it to the route:

```cpp
// when starting the match
world.paths.push_back(scene::readPath(e));          // not state: the scene is the same on every peer
// in the AI's act step, instead of a random wander target
const Vec3 goal = ai::patrolTarget(c.patrol, world.paths[pathIndex], feet, params);   // params: reach, stepMm
ai::setTarget(c.route, goal);
```

A closed path loops, an open one is walked there and back; the first call starts at the point of the path closest to the AI. `Patrol.h` doesn't include the scene module: `patrolTarget` is a template over any path type with `valid()`, `length`, `closed` and `pathPoint` / `pathClosest` functions, so the module's tests use a fake one.

Noises: any system that makes one calls `ai::emit(state.noises, position, radius, sourceId, tick)` (a push, a landing, a shot).

## Rules

- **Fixed order.** Offer options in id order and run Controllers in Pool order: on a tie the first one offered wins, so a state never depends on anything but state.
- **No flapping.** A new option must beat the current one by `switchMargin` and the current state must have lasted `minStayTicks`. Decisions run every `thinkTicks`, not every tick.
- **Sight** is range + a cone (`cosHalfFov`: 0.5 = 120° wide, 0 = 180°, -1 = all around) + one ray to the target; rays are cast only for candidates closer than the nearest seen so far. A target out of sight is remembered for `forgetTicks`.
- **Hearing** ignores walls; a noise carries `radius * sensitivity` meters and is audible for `noiseTicks`. The ring keeps the last 16 noises.
- **Route** needs the engine's nav grid (`mealkit::sim::NavGrid`, baked at match start). The grid is not state; the path is. Searches per tick are limited by the budget you pass in.
- Behavior trees as data are not included: utility + states is enough for most games, and a table-driven tree is the next step if a game needs it.

## Reference

| Function | What it does |
|---|---|
| `normalize`, `response`, `Score`, `Chooser` | Considerations (0..1000), response curves, an option's score, the best option of a tick |
| `think`, `thinkReady`, `enter`, `stateTicks` | The state machine: when to switch, forcing a state |
| `inCone`, `lineClear`, `see` | Sight: cone test, line-of-sight ray, nearest visible candidate (not itself) with memory |
| `emit`, `hear`, `notice` | Hearing: make a noise, the nearest audible one, update the memory |
| `setTarget`, `follow`, `hasPath` | Route to a point along the nav grid |
| `patrolStart`, `patrolTarget` | The point to walk to along a scene path (loops or ping-pongs) |
| `drawEye`, `drawAwareness`, `drawRoute` (`Debug.h`) | Debug drawing |
