// Player slots, controllers, Pawns: possession, movement, jumping, pushing, jump pads, respawn wait after falling, AI.
#include "sim/Systems.h"

#include "sim/Loadout.h"

#include "mealkit/core/DetMath.h"
#include "ai/sim/Debug.h"
#include "mealkit/debug/DebugDraw.h"

#include <algorithm>
#include <cmath>

namespace sandbox {

const BodyKind* bodyOf(const World& world, u8 player) {
    if (player >= world.state.playerCount || world.bodyKinds.size() == 0) return nullptr;
    return &world.bodyKinds.rows[world.state.slots[player].body];
}

namespace {

// What a Pawn wants to do this tick (humans from input, AI from decisions)
struct Intent {
    mealkit::sim::MoveIntent move;
    bool push = false;
};

Intent intentFromInput(const GameInput& in) {
    Intent i;
    constexpr f32 kAxis = 127.0f; // const-ok: input quantization (i8 -127..127 → -1..1)
    i.move.wish = {static_cast<f32>(in.moveX) / kAxis, 0.0f, static_cast<f32>(in.moveZ) / kAxis};
    i.move.jump = (in.buttons & kButtonJump) != 0;
    i.push = (in.buttons & kButtonPush) != 0;
    return i;
}

// AI (module ai): senses (sight cone with a line-of-sight ray, hearing pushes), utility scores for Wander / Chase / Flee, a brain that
// switches state without flapping, and a path along the nav grid. Controllers run in order; paths are searched at most
// aiPathsPerTick per tick. Wander picks a new nearby point every aiThink and sometimes jumps or pushes then.
constexpr u32 kMaxCandidates = 32; // Pawns an AI can look at (more are ignored, in slot order)

mealkit::i32 healthFixed(const Pawn& pawn) { // 0..1000
    return pawn.health.max > 0 ? static_cast<mealkit::i32>(static_cast<mealkit::i64>(pawn.health.current) * ai::kOne / pawn.health.max) : ai::kOne;
}

Intent thinkAI(World& world, Controller& c, const Pawn& pawn, u32 selfId, const ai::Candidate* candidates, u32 candidateCount,
               u32& pathBudget) {
    const Tuning& t = world.tuning;
    const u32 tick = world.state.tick;
    mealkit::Pcg32& rng = world.state.aiRng;
    Intent i;
    const Vec3 feet{pawn.position.x, pawn.position.y - pawn.mover.halfHeight, pawn.position.z};
    if (mealkit::lengthSq(c.facing) <= 0.0f) c.facing = {0.0f, 0.0f, 1.0f};

    // Senses: what it sees now, and the nearest push it hears when it sees nothing
    constexpr f32 kHalf = mealkit::kDetPi / 360.0f; // const-ok: degrees → radians, halved
    const ai::Eye eye{pawn.position, c.facing, t.aiSightRange, mealkit::detCos(t.aiSightAngle * kHalf)};
    ai::see(c.aware, eye, world.physics, selfId, pawn.mover.body, candidates, candidateCount, tick, t.ai, t.pawnRadius);
    ai::Noise noise;
    if (ai::hear(world.state.noises, pawn.position, 1.0f, selfId, tick, t.ai, noise)) ai::notice(c.aware, noise, tick);
    const bool known = ai::hasTarget(c.aware);
    const f32 dist = known ? std::sqrt(mealkit::lengthSq(c.aware.lastKnown - pawn.position)) : 0.0f;

    // Decision: each option is a score (0..1000) built from considerations (0..1000). Offered in enum order.
    if (ai::thinkReady(c.brain, tick)) {
        mealkit::i32 chase = 0, flee = 0;
        if (known) {
            const mealkit::i32 near = ai::response(ai::Curve::Inverse, ai::normalize(static_cast<mealkit::i32>(dist * 1000.0f), 0, // const-ok: m → mm
                                                                                      static_cast<mealkit::i32>(t.aiChaseRange * 1000.0f))); // const-ok: m → mm
            const mealkit::i32 healthy = healthFixed(pawn);
            chase = ai::Score().mul(near).mul(healthy).done(2);
            flee = ai::Score().mul(near).mul(ai::kOne - healthy).done(2);
        }
        ai::Chooser pick(c.brain.state);
        pick.offer(static_cast<u32>(AiState::Wander), static_cast<mealkit::i32>(t.aiWanderScore));
        pick.offer(static_cast<u32>(AiState::Chase), chase);
        pick.offer(static_cast<u32>(AiState::Flee), flee);
        if (ai::think(c.brain, pick, tick, t.ai)) c.retarget = {}; // new state: choose its target right away
    }

    // Act. Chase and Flee need a target; without one the AI wanders until the decision changes.
    AiState act = static_cast<AiState>(c.brain.state);
    if (act != AiState::Wander && !known) act = AiState::Wander;
    if (act == AiState::Wander) {
        if (c.retarget.ready(tick)) {
            c.retarget.start(tick, t.aiThink + rng.below(t.aiThink + 1));
            const f32 angle = rng.range(-mealkit::kDetPi, mealkit::kDetPi);
            const f32 distance = rng.range(t.aiTargetMin, t.aiTargetMax);
            const Vec3 target = pawn.position + Vec3{mealkit::detSin(angle), 0.0f, mealkit::detCos(angle)} * distance;
            ai::setTarget(c.route, {std::clamp(target.x, world.arenaMin.x, world.arenaMax.x), feet.y, std::clamp(target.z, world.arenaMin.z, world.arenaMax.z)});
            i.move.jump = rng.below(t.aiJumpOneIn) == 0;
            i.push = rng.below(t.aiPushOneIn) == 0;
        }
    } else if (c.retarget.ready(tick)) {
        c.retarget.start(tick, t.ai.thinkTicks);
        const Vec3 there{c.aware.lastKnown.x, c.aware.lastKnown.y - pawn.mover.halfHeight, c.aware.lastKnown.z};
        if (act == AiState::Chase) {
            ai::setTarget(c.route, there);
        } else { // Flee: a point away from where the target was
            Vec3 away{feet.x - there.x, 0.0f, feet.z - there.z};
            const f32 len2 = mealkit::lengthSq(away);
            away = len2 > 0.0f ? away / std::sqrt(len2) : c.facing * -1.0f;
            const Vec3 goal = feet + away * t.aiFleeDistance;
            ai::setTarget(c.route, {std::clamp(goal.x, world.arenaMin.x, world.arenaMax.x), feet.y, std::clamp(goal.z, world.arenaMin.z, world.arenaMax.z)});
        }
    }
    if (act == AiState::Chase && c.aware.visible && dist < t.pushRadius * 0.8f) i.push = true; // const-ok: pushes when this close to it

    ai::FollowParams follow;
    follow.speed = t.aiSpeedScale;
    follow.maxExpand = t.aiPathExpand;
    const Vec3 wish = ai::follow(c.route, world.nav, world.navSearch, feet, pathBudget, follow);
    i.move.wish = wish;
    const f32 speed2 = mealkit::lengthSq(wish);
    if (speed2 > 0.0f) c.facing = wish / std::sqrt(speed2);
    if (t.navDebug) {
        ai::drawRoute(c.route, feet);
        ai::drawEye(eye, t.aiSightAngle * kHalf);
        ai::drawAwareness(eye, c.aware);
    }
    return i;
}

// Nav grid debug drawing: walkable cells near each AI as short crosses (tuning navDebug)
void drawNav(const World& world) {
    const mealkit::sim::NavGrid& g = world.nav;
    if (!world.tuning.navDebug || g.empty()) return;
    constexpr mealkit::i32 kRings = 12; // const-ok: debug draw radius in cells
    const f32 half = g.cellSize * 0.3f; // const-ok: debug cross size
    for (auto [id, c] : world.state.controllers) {
        const Pawn* p = c.kind == ControllerKind::AI ? world.state.pawns.get(c.pawn) : nullptr;
        if (!p) continue;
        const mealkit::i32 cx = static_cast<mealkit::i32>((p->position.x - g.origin.x) / g.cellSize);
        const mealkit::i32 cz = static_cast<mealkit::i32>((p->position.z - g.origin.z) / g.cellSize);
        for (mealkit::i32 z = std::max(0, cz - kRings); z <= std::min(static_cast<mealkit::i32>(g.depth) - 1, cz + kRings); ++z)
            for (mealkit::i32 x = std::max(0, cx - kRings); x <= std::min(static_cast<mealkit::i32>(g.width) - 1, cx + kRings); ++x)
                for (u32 l = 0; l < g.layers; ++l) {
                    const u32 n = g.node(static_cast<u32>(x), static_cast<u32>(z), l);
                    if (!g.walkable(n)) continue;
                    const Vec3 q = g.position(n) + Vec3{0.0f, 0.03f, 0.0f}; // const-ok: lift debug lines off the floor
                    mealkit::debug::line(q - Vec3{half, 0.0f, 0.0f}, q + Vec3{half, 0.0f, 0.0f}, mealkit::debug::kBlue);
                    mealkit::debug::line(q - Vec3{0.0f, 0.0f, half}, q + Vec3{0.0f, 0.0f, half}, mealkit::debug::kBlue);
                }
    }
}

// Pushing knocks nearby ragdoll Pawns down (other movement modes aren't pushed, as before). The pusher itself is at distance 0.
// Every other Pawn in range also takes combat effects: push damage (health module) and daze (effects module: slower, can't push).
// Attacker = player slot + 1 (0 = AI)
void knockRagdolls(World& world, const Vec3& center, u32 attacker) {
    const Tuning& t = world.tuning;
    for (auto [id, other] : world.state.pawns) { // Pawn slot order
        const Vec3 d = other.position - center;
        const f32 dist2 = mealkit::lengthSq(d);
        if (dist2 <= 1e-4f || dist2 >= t.pushRadius * t.pushRadius) continue; // const-ok: the pusher itself
        if (other.mover.mode == mealkit::sim::MoveMode::Ragdoll) {
            const f32 dist = std::sqrt(dist2);
            const f32 strength = 1.0f - dist / t.pushRadius;
            const Vec3 dir = d / dist;
            mealkit::sim::knockRagdoll(world.physics, other.mover, t.knockdown.value,
                                       Vec3{dir.x, 0.0f, dir.z} * (t.pushSpeed * strength) + Vec3{0.0f, t.pushLift * strength, 0.0f});
        }
        if (t.maxHealth > 0 && t.pushDamage > 0)
            health::damage(other.health, static_cast<mealkit::i32>(t.pushDamage) * stats::kOne, attacker, 0, world.state.tick);
        effects::apply(other.effects, world.effectDefs.data(), static_cast<u32>(world.effectDefs.size()), 1, attacker, world.state.tick);
    }
}

void movePawn(World& world, Pawn& p, const Intent& intent, u8 player, u32 selfId) {
    const Tuning& t = world.tuning;
    const BodyKind* body = bodyOf(world, player);
    // body kind (loadout) times the speed stat (daze lowers it)
    const f32 speedScale = (body ? body->speedScale : 1.0f) * (static_cast<f32>(stats::value(p.stats, kStatSpeed)) / static_cast<f32>(stats::kOne));
    if (mealkit::sim::move(world.physics, p.mover, intent.move, t.move, speedScale, t.gravity, world.tickDt))
        world.events.push(makeEvent(EventKind::Jump, player, p.position, 0.0f));

    const bool dazed = tags::has(p.effects.granted, world.dazedTag); // "can act" check by tag
    if (intent.push && !dazed && cooldown::tryUse(p.push, world.state.tick, t.pushCooldown.value)) {
        pushProps(world, p.position);
        ai::emit(world.state.noises, p.position, t.pushNoise, selfId, world.state.tick); // AI hears it
        knockRagdolls(world, p.position, player == kNoPlayer ? 0u : player + 1u);
        world.events.push(makeEvent(EventKind::Push, player, p.position, t.pushRadius));
    }
}

} // namespace

// Uses the scene's spawn points in turn, stepping aside if there are more than points. Scenes without spawn points line up sideways from the origin.
Vec3 spawnPosition(const World& world, u32 index) {
    const Tuning& t = world.tuning;
    if (world.spawns.empty()) return {static_cast<f32>(index) * t.spawnSpacing, t.pawnHalfHeight, 0.0f};
    const u32 n = static_cast<u32>(world.spawns.size());
    const f32 offset = static_cast<f32>(index / n) * t.spawnSpacing;
    return world.spawns[index % n] + Vec3{offset, 0.0f, 0.0f};
}

const Pawn* playerPawn(const WorldState& state, u32 slot) {
    if (slot >= state.playerCount) return nullptr;
    const Controller* c = state.controllers.get(state.slots[slot].controller);
    return c ? state.pawns.get(c->pawn) : nullptr;
}

u8 pawnPlayer(const WorldState& state, const Pawn& pawn) {
    const Controller* c = state.controllers.get(pawn.owner);
    return c && c->kind == ControllerKind::Player ? static_cast<u8>(c->slot) : kNoPlayer;
}

// One controller per player slot, then AI controllers (creation order = processing order)
void createControllers(World& world) {
    WorldState& s = world.state;
    for (u32 slot = 0; slot < s.playerCount; ++slot) {
        Controller c;
        c.kind = ControllerKind::Player;
        c.slot = slot;
        c.spawnIndex = slot;
        s.slots[slot].connected = true;
        s.slots[slot].controller = s.controllers.create(c);
    }
    for (u32 i = 0; i < world.tuning.aiPawns; ++i) {
        Controller c;
        c.kind = ControllerKind::AI;
        c.spawnIndex = s.playerCount + i;
        s.controllers.create(c);
    }
}

// A capsule (or an active ragdoll) moved by the body kind's movement mode (engine mealkit/sim/Movement.h). Every mode pushes boxes away.
Id<Pawn> spawnPawn(World& world, const Vec3& position, const BodyKind* body) {
    Pawn p;
    p.position = position;
    health::init(p.health, static_cast<mealkit::i32>(world.tuning.maxHealth) * stats::kOne);
    stats::define(p.stats, kStatSpeed, stats::kOne, 0, 4 * stats::kOne);
    const Id<Pawn> id = world.state.pawns.create(p);
    mealkit::sim::MoverShape shape;
    shape.position = position;
    shape.halfHeight = world.tuning.pawnHalfHeight;
    shape.radius = world.tuning.pawnRadius;
    shape.mass = world.tuning.pawnMass * (body ? body->massScale : 1.0f);
    shape.group = id.slot + 1; // ragdoll parts: unique among living Pawns
    world.state.pawns.get(id)->mover = mealkit::sim::createMover(
        world.physics, body ? body->movement : mealkit::sim::MoveMode::Capsule, shape, world.tuning.move);
    return id;
}

void possess(World& world, Id<Controller> controller, Id<Pawn> pawn) {
    unpossess(world, controller);
    Controller* c = world.state.controllers.get(controller);
    Pawn* p = world.state.pawns.get(pawn);
    if (!c || !p) return;
    if (Controller* previous = world.state.controllers.get(p->owner)) previous->pawn = {};
    c->pawn = pawn;
    p->owner = controller;
}

void unpossess(World& world, Id<Controller> controller) {
    Controller* c = world.state.controllers.get(controller);
    if (!c) return;
    if (Pawn* p = world.state.pawns.get(c->pawn)) p->owner = {};
    c->pawn = {};
}

// Creates a new Pawn and possesses it for controllers whose respawn wait has ended (the first Pawns at match start are made this way too)
void respawnPawns(World& world) {
    WorldState& s = world.state;
    for (auto [id, c] : s.controllers) {
        if (s.pawns.alive(c.pawn) || !c.respawn.ready(s.tick)) continue;
        if (c.kind == ControllerKind::Player && !s.slots[c.slot].connected) continue; // disconnected players wait until they rejoin
        if (c.kind == ControllerKind::Player && slotOut(world, c.slot)) continue;     // out of lives: spectate until the next round
        const Vec3 at = spawnPosition(world, c.spawnIndex);
        const u8 player = c.kind == ControllerKind::Player ? static_cast<u8>(c.slot) : kNoPlayer;
        const BodyKind* body = bodyOf(world, player);
        const Id<Pawn> pawn = spawnPawn(world, at, body);
        possess(world, id, pawn);
        world.events.push(makeEvent(EventKind::Spawn, player, at, 0.0f));
    }
}

void controlPawns(World& world) {
    WorldState& s = world.state;
    const bool playing = matchPlaying(world); // during countdown and after the end, stand and wait (AI decisions stop too)
    u32 pathBudget = world.tuning.aiPathsPerTick; // per-tick search limit, used up in controller order
    drawNav(world);
    ai::Candidate candidates[kMaxCandidates]; // living Pawns in slot order: what an AI can look at
    u32 candidateCount = 0;
    for (auto [pawnId, other] : s.pawns) {
        if (candidateCount == kMaxCandidates) break;
        if (!other.health.dead) candidates[candidateCount++] = {pawnId.slot + 1, other.position, other.mover.body};
    }
    for (auto [id, c] : s.controllers) { // controller order = player slot order, then AI
        Pawn* p = s.pawns.get(c.pawn);
        if (!p) continue;
        if (c.kind == ControllerKind::Player)
            movePawn(world, *p, playing ? intentFromInput(world.inputs[c.slot]) : Intent{}, static_cast<u8>(c.slot), c.pawn.slot + 1);
        else if (c.kind == ControllerKind::AI) movePawn(world, *p,
                     playing ? thinkAI(world, c, *p, c.pawn.slot + 1, candidates, candidateCount, pathBudget) : Intent{}, kNoPlayer, c.pawn.slot + 1);
    }
}

void usePads(World& world) {
    WorldState& s = world.state;
    const u32 tick = s.tick;
    for (auto [padId, pad] : s.pads) {
        if (!pad.ready.ready(tick)) continue;
        const f32 top = pad.center.y + pad.halfExtents.y;
        for (auto [pawnId, p] : s.pawns) { // Pawn slot order
            const f32 feet = p.position.y - world.tuning.pawnHalfHeight;
            const Vec3 d = p.position - pad.center;
            const bool over = d.x <= pad.halfExtents.x && -d.x <= pad.halfExtents.x && d.z <= pad.halfExtents.z &&
                              -d.z <= pad.halfExtents.z;
            if (!over || feet < top - world.tuning.padBelow || feet > top + world.tuning.padAbove) continue;
            Vec3 v = mealkit::sim::moverVelocity(world.physics, p.mover);
            v.y = pad.power;
            mealkit::sim::setMoverVelocity(world.physics, p.mover, v);
            pad.ready.start(tick, pad.cooldownTicks);
            world.events.push(makeEvent(EventKind::Bounce, pawnPlayer(s, p), {pad.center.x, top, pad.center.z}, 0.0f));
            break;
        }
    }
}

// Pawns that fell below the match area floor are removed, and controllers wait to respawn (spectating meanwhile)
void removeFallenPawns(World& world) {
    WorldState& s = world.state;
    for (auto [pawnId, p] : s.pawns) {
        const bool hurtToDeath = world.tuning.maxHealth > 0 && p.health.dead; // health reached 0 (push damage)
        if (p.position.y >= world.arenaMin.y && !hurtToDeath) continue;
        const u8 player = pawnPlayer(s, p);
        if (Controller* c = s.controllers.get(p.owner)) {
            c->respawn.start(s.tick, world.tuning.respawn);
            unpossess(world, p.owner);
        }
        if (player != kNoPlayer && matchPlaying(world)) onPlayerFell(world, player); // score only during play
        world.events.push(makeEvent(EventKind::Fall, player, p.position, 0.0f));
        s.pawns.destroy(pawnId); // the body is deleted at the end of the tick (World::step)
    }
}

// Someone joined mid-match: for a new slot, create a controller (Pawn on the next tick); for a rejoin, re-enable that slot.
// A disconnected player's Pawn is deleted (score/slot remain)
void setPresence(World& world, const mealkit::sim::Presence& presence) {
    WorldState& s = world.state;
    if (presence.player >= mealkit::session::kMaxPlayers) return;
    PlayerSlot& slot = s.slots[presence.player];
    if (!presence.connected) {
        slot.connected = false;
        if (Controller* c = s.controllers.get(slot.controller)) {
            if (Pawn* p = s.pawns.get(c->pawn)) {
                const Id<Pawn> pawn = c->pawn;
                world.events.push(makeEvent(EventKind::Fall, presence.player, p->position, 0.0f));
                unpossess(world, slot.controller);
                s.pawns.destroy(pawn);
            }
        }
        return;
    }
    slot.connected = true;
    if (presence.player < s.playerCount && s.controllers.alive(slot.controller)) return; // rejoined
    s.playerCount = std::max(s.playerCount, presence.player + 1u);
    Loadout l;
    slot.body = presence.readLoadout(l) && l.body < world.bodyKinds.size() ? l.body : 0u;
    if (s.controllers.count() >= s.controllers.capacity()) s.controllers.grow(s.controllers.capacity() + 1);
    s.pawns.grow(s.controllers.capacity() * 2); // same ratio as World::reset
    Controller c;
    c.kind = ControllerKind::Player;
    c.slot = presence.player;
    c.spawnIndex = s.controllers.count(); // a spawn spot that doesn't overlap existing controllers
    slot.controller = s.controllers.create(c);
}

void buildCombat(World& world) {
    const char* const names[] = {"State.Dazed"};
    world.tagRegistry.build(names, 1);
    world.dazedTag = world.tagRegistry.find("State.Dazed");
    effects::EffectDef d;
    d.duration = world.tuning.dazeTime.value;
    d.stacking = effects::Stacking::Refresh;
    d.stat = static_cast<u8>(kStatSpeed);
    d.op = stats::ModOp::PercentAdd;
    d.amount = -static_cast<mealkit::i32>(world.tuning.dazeSlow) * 10; // percent → 1/1000
    effects::setGrantTag(d, world.tagRegistry, "State.Dazed");
    world.effectDefs[0] = d;
}

void updateEffects(World& world) {
    effects::PeriodicHits hits; // dazed has no periodic part
    for (auto [id, p] : world.state.pawns) // Pawn slot order
        effects::step(p.effects, world.effectDefs.data(), static_cast<u32>(world.effectDefs.size()), world.tagRegistry, world.state.tick,
                      p.stats, hits);
}

void sensePawns(World& world) {
    for (auto [id, p] : world.state.pawns) {
        mealkit::sim::senseMover(world.physics, p.mover, world.tuning.move, p.position, p.velocity);
    }
}

} // namespace sandbox
