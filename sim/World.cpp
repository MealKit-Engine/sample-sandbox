#include "sim/World.h"

#include "sim/Systems.h"

#include "mealkit/core/Hash.h"
#include "mealkit/core/Log.h"
#include "mealkit/core/Profile.h"

#include <cstring>

namespace sandbox {
namespace {

using mealkit::physics::BodyDesc;
using mealkit::physics::Motion;
namespace scene = mealkit::scene;

// Links for the scene's `joint` objects, in scene order. A name that isn't a solid (or empty) is the world: a small static body at the anchor
void createJoints(World& world, const std::vector<scene::SceneJoint>& joints,
                  const std::vector<std::pair<std::string, mealkit::physics::BodyId>>& named) {
    namespace physics = mealkit::physics;
    auto find = [&](const std::string& name) {
        for (const auto& [n, body] : named)
            if (!name.empty() && n == name) return body;
        return physics::kInvalidBody;
    };
    for (const scene::SceneJoint& j : joints) {
        const scene::JointFields& f = j.fields;
        physics::BodyId a = find(f.bodyA), b = find(f.bodyB);
        if (a == physics::kInvalidBody && b == physics::kInvalidBody) {
            MEALKIT_WARN("joint '%s' / '%s': neither body is a solid of the scene - skipped", f.bodyA.c_str(), f.bodyB.c_str());
            continue;
        }
        BodyDesc anchor;
        anchor.motion = Motion::Static;
        anchor.position = j.pivot;
        if (a == physics::kInvalidBody) a = world.physics.createBox({0.01f, 0.01f, 0.01f}, anchor); // const-ok: tiny world anchor
        if (b == physics::kInvalidBody) b = world.physics.createBox({0.01f, 0.01f, 0.01f}, anchor); // const-ok: tiny world anchor
        physics::LinkDesc d;
        d.kind = static_cast<physics::LinkKind>(static_cast<u8>(f.kind)); // same order as scene::JointKind
        d.pivot = j.pivot;
        d.pivot2 = world.physics.position(b);
        d.axis = j.axis;
        d.limitMin = j.limitMin;
        d.limitMax = j.limitMax;
        d.length = f.length;
        d.frequency = f.frequency;
        d.damping = f.damping;
        d.breakForce = f.breakForce;
        world.physics.createLink(a, b, d);
    }
}

// The scene's collision boxes (static), spawn points, jump pads, match area, box spawn areas
void loadScene(World& world, const scene::Scene& scene, mealkit::asset::ModelCache& models) {
    u32 padCount = 0;
    for (const scene::Entity& e : scene.entities) padCount += e.is<JumpPad>() ? 1u : 0u;
    world.state.pads.init(padCount);
    bool arena = false;

    // `joint` objects name their solids: a solid a joint marks dynamic is created as a moving body (docs/DECISIONS.md 2026-11-25 "E7 joints")
    std::vector<scene::SceneJoint> joints;
    for (const scene::Entity& e : scene.entities)
        if (e.is(scene::kTypeJoint)) joints.push_back(scene::readJoint(e));
    auto movesByJoint = [&](const std::string& name) {
        if (name.empty()) return false;
        for (const scene::SceneJoint& j : joints)
            if ((j.fields.bodyA == name && j.fields.dynamicA) || (j.fields.bodyB == name && j.fields.dynamicB)) return true;
        return false;
    };
    std::vector<std::pair<std::string, mealkit::physics::BodyId>> named; // solids a joint names, for the links below

    for (const scene::Entity& e : scene.entities) {
        if (e.is(scene::kTypeSolid)) {
            const bool moves = movesByJoint(e.name);
            BodyDesc d;
            d.motion = moves ? Motion::Dynamic : Motion::Static;
            d.position = e.position;
            d.rotation = e.rotation;
            d.friction = world.tuning.solidFriction;
            const mealkit::asset::Model* model = e.model.empty() ? nullptr : models.get(e.model);
            mealkit::physics::BodyId body = mealkit::physics::kInvalidBody;
            if (model) {
                std::vector<Vec3> points;
                points.reserve(model->hullPoints.size());
                for (const Vec3& p : model->hullPoints)
                    points.push_back({p.x * e.scale.x, p.y * e.scale.y, p.z * e.scale.z});
                body = world.physics.createConvexHull(points.data(), static_cast<u32>(points.size()), d);
            } else {
                body = world.physics.createBox(e.scale * 0.5f, d);
            }
            if (!e.name.empty()) named.push_back({e.name, body});
            if (moves) world.linkedSolids.push_back({e.id, body});
        } else if (e.is(scene::kTypeTerrain)) { // heightmap (rotation ignored: terrain is axis-aligned)
            const mealkit::asset::Terrain* terrain = models.terrain(scene::terrainFile(e));
            if (!terrain) continue;
            const mealkit::asset::Heightmap& map = terrain->map;
            std::vector<f32> heights;
            mealkit::asset::terrainHeights(map, e.scale.y, heights);
            BodyDesc d;
            d.position = e.position;
            d.friction = world.tuning.solidFriction;
            const f32 cells = static_cast<f32>(map.size - 1);
            world.physics.createHeightField(heights.data(), map.size, e.scale * -0.5f, e.scale.x / cells, e.scale.z / cells, d);
        } else if (e.is(scene::kTypePath)) {
            world.paths.push_back(scene::readPath(e));
        } else if (e.is(scene::kTypeSpawn)) {
            world.spawns.push_back(e.position);
        } else if (e.is<JumpPad>()) {
            const JumpPad def = scene::readObject<JumpPad>(e);
            Pad pad;
            pad.center = e.position;
            pad.halfExtents = e.scale * 0.5f;
            pad.power = def.power;
            pad.cooldownTicks = def.cooldown;
            world.state.pads.create(pad);
        } else if (e.is<MatchArea>() && !arena) {
            const scene::Bounds b = scene::areaBounds(e);
            world.arenaMin = b.min;
            world.arenaMax = b.max;
            arena = true;
        } else if (e.is<PropSpawnArea>()) {
            const scene::Bounds b = scene::areaBounds(e);
            const PropSpawnArea def = scene::readObject<PropSpawnArea>(e);
            u32 kind = world.propKinds.find(def.kind.id); // row name → index (once at load)
            if (kind == mealkit::kNoRow) {
                MEALKIT_WARN("%s: box kind '%s' is not in tables/props.table - using the first row", e.name.c_str(),
                             def.kind.id.c_str());
                kind = 0;
            }
            if (world.propKinds.size() == 0) continue; // no box kinds means no boxes
            world.propSpawns.push_back({b.min, b.max, def.count, kind});
        }
    }
    if (!arena) { // scenes without a match area (engine default arena etc.): run with the old sandbox values
        MEALKIT_WARN("Scene has no MatchArea - using a default 80 m arena");
        world.arenaMin = {-40.0f, -30.0f, -40.0f}; // const-ok: safe value for scenes without an area
        world.arenaMax = {40.0f, 30.0f, 40.0f};    // const-ok: safe value for scenes without an area
    }
    world.state.pads.endTick();
    createJoints(world, joints, named);
    world.physics.optimize();
}

// Sets the box count to override: proportionally per area, the remainder to the last area (integer math, so the same on all peers)
void overridePropCount(std::vector<World::PropSpawn>& spawns, u32 total) {
    u64 declared = 0;
    for (const World::PropSpawn& s : spawns) declared += s.count;
    if (spawns.empty() || declared == 0) return;
    u32 given = 0;
    for (World::PropSpawn& s : spawns) {
        s.count = static_cast<u32>(static_cast<u64>(s.count) * total / declared);
        given += s.count;
    }
    spawns.back().count += total - given;
}

} // namespace

void World::reset(u64 seed, u32 playerCount, u32 propOverride, const Tuning& tuningValue,
                  const mealkit::sim::Table<PropKind>& propKindTable, const mealkit::sim::Table<BodyKind>& bodyKindTable,
                  const std::vector<u32>& playerBodies, const scene::Scene& scene, mealkit::asset::ModelCache& models,
                  u32 tickRateValue, u32 physicsThreads) {
    physics.init(16384, physicsThreads);
    tickRate = tickRateValue ? tickRateValue : 30u;
    tickDt = 1.0f / static_cast<f32>(tickRate);
    tuning = tuningValue;
    rules = matchRules(tuning);
    buildCombat(*this);
    propKinds = propKindTable;
    bodyKinds = bodyKindTable;
    physics.setContactEvents(tuning.impactSpeed, tuning.impactsPerTick); // collision sounds (view notifications)
    physics.setGravity(tuning.gravity);
    spawns.clear();
    linkedSolids.clear();
    propSpawns.clear();
    state = WorldState{};
    events.clear();

    // Fixed body creation order: scene → Pawns → boxes. The scene must be read first to know the box count (= Pool capacity, physics body count).
    loadScene(*this, scene, models);
    // Only static scene bodies exist now, so the nav grid sees just the level (from the death height up to the area top)
    nav = mealkit::sim::bakeNavGrid(physics, arenaMin, arenaMax, tuning.nav);
    if (propOverride) overridePropCount(propSpawns, propOverride);
    u32 propCount = 0;
    for (const PropSpawn& s : propSpawns) propCount += s.count;
    state.playerCount = playerCount;
    for (u32 i = 0; i < playerCount; ++i) // match start data: first row if the row is missing
        state.slots[i].body = i < playerBodies.size() && playerBodies[i] < bodyKinds.size() ? playerBodies[i] : 0u;
    const u32 controllers = playerCount + tuning.aiPawns;
    state.controllers.init(controllers);
    state.pawns.init(controllers * 2); // with a 0-tick respawn wait, the deleted Pawn and the new Pawn coexist within one tick
    state.props.init(propCount);
    state.aiRng = mealkit::Pcg32(seed ^ 0x9E3779B97F4A7C15ull, 7);

    createControllers(*this);
    state.controllers.endTick();
    respawnPawns(*this); // controllers with a 0 respawn wait = all of them: first Pawns
    state.pawns.endTick();
    spawnProps(*this, seed);
    state.props.endTick(); // boxes created at start also appear in iteration from tick 0
    sensePawns(*this);
    senseProps(*this);
    startMatch(*this);
    events.clear(); // spawn events of Pawns created at start aren't sent
}

void World::setTuning(const Tuning& value) {
    tuning = value;
    rules = matchRules(tuning);
    buildCombat(*this);
    physics.setGravity(tuning.gravity);
}

void World::step(const mealkit::session::TickInputs& tickInputs) {
    MEALKIT_ZONE("World::step");
    for (u32 i = 0; i < state.playerCount; ++i) std::memcpy(&inputs[i], tickInputs.players[i].bytes, sizeof(GameInput));
    events.clear();

    applyCommands(*this, tickInputs); // confirmed commands (debug cheats etc.): player index → send order
    updateMatch(*this);               // waiting → countdown → playing → round end → results
    respawnPawns(*this);
    updateEffects(*this);
    controlPawns(*this);
    usePads(*this);
    physics.step(tickDt, 2); // splits a tick into 2 physics substeps for stability
    for (const mealkit::physics::ContactEvent& c : physics.contacts()) // strongest first (order independent of thread count)
        events.push(makeEvent(EventKind::Impact, kNoPlayer, c.point, c.speed));
    removeFallenPawns(*this);
    recoverFallenProps(*this);
    sensePawns(*this);
    senseProps(*this);
    // End of tick: delete bodies of objects deleted this tick in slot order, and put objects created this tick into iteration from the next tick
    state.controllers.endTick();
    state.pawns.endTick([this](Pawn& pawn) { mealkit::sim::destroyMover(physics, pawn.mover); });
    state.props.endTick([this](Prop& prop) { physics.destroy(prop.body); });
    ++state.tick;
}

u64 World::hash() const {
    mealkit::Hasher h;
    mealkit::reflect::hashStruct(mealkit::reflect::typeOf<WorldState>(), &state, h); // all declared state fields
    physics.hashState(h);
    return h.value;
}

} // namespace sandbox
