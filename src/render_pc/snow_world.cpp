// NOT ORIGINAL - the surface trails' world: the snow's sky map and pressers, the paint's step (snow_world.h).
#include "render_pc/snow_world.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ratio>
#include <span>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/enum_ops.h"
#include "base/maths/mat4.h"
#include "base/maths/vec4.h"
#include "engine/config/settings_state.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/paint_trails.h"
#include "engine/gfx/rain.h"
#include "engine/gfx/rig.h"
#include "render_pc/mesh_list.h"
#include "engine/gfx/snow.h"
#include "engine/gfx/surface_trail.h"
#include "engine/gfx/weather.h"
#include "engine/lists/key_map.h"
#include "engine/memory/arena_alloc.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/sys/stopwatch.h"
#include "platform/frame_profile.h"
#include "platform/run_log.h"
#include "render_pc/air_world.h"
#include "render_pc/geom.h"

namespace render::snow_world {

namespace {

constexpr int kMostScenes = 256;        // a scene list longer than this is taken as broken (the editor's guard)
constexpr uint32_t kMostTableBits = 20; // the largest sector table walked
constexpr uint32_t kSwitchedOff = 4U;   // a collision object's flag: switched off (not collided with)
constexpr uint64_t kFnvBasis = 0xCBF29CE484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001B3ULL;
constexpr std::size_t kRow = 4;            // (a Mat4's floats a row)
constexpr std::size_t kTranslationRow = 3;
constexpr int kLeastWheelBones = 2; // fewer than surface_trail::kSkeletonBones (but some): a vehicle - its wheels the bones off its sides
constexpr float kWheelSide = 15.0F; // a bone this far off the middle (its x) and ...
constexpr float kWheelLow = 40.0F;  // ... no higher than this over the origin: a wheel's hub
constexpr float kSquareInchesPerSquareMetre = 1550.0031F;

struct State {
    uint64_t world_key = 0;   // the loaded scenes'
    uint64_t sectors_key = 0; // ... and their sectors' (the sky map's)
    bool keyed = false;   // world_key is the loaded world's
    bool outdoor = false; // ... and it has a sky
    bool made = false;    // its sky map handed to the snow
    // (the step's report: since the last)
    uint32_t report_steps = 0;
    double report_ms = 0.0;
    double report_most = 0.0;
    uint64_t report_pressers = 0;
    uint64_t report_stamps = 0;
    uint64_t report_body = 0;
};
constexpr uint32_t kReportSteps = 1200; // (twenty seconds at 60 steps a second)

State& state()
{
    static State instance;
    return instance;
}

// Each loaded scene (the scene list: render::WorldScene nodes until the head).
template <typename Visit>
void for_each_scene(Visit visit)
{
    const memory::ListNode* node = engine_parts().scene_list().next();
    for (int guard = 0; node != nullptr && !node->is_head() && guard < kMostScenes; ++guard) {
        const auto* scene = static_cast<const WorldScene*>(node->data());
        node = node->next();
        if (scene != nullptr) {
            visit(*scene);
        }
    }
}

template <typename Visit>
void for_each_sector(const WorldScene& scene, Visit visit)
{
    const lists::KeyMap* table = scene.sectors();
    if (table == nullptr || std::cmp_greater(table->bits(), kMostTableBits) || table->buckets() == nullptr) {
        return;
    }
    const std::span<const lists::KeyMap::Item> buckets(table->buckets(), std::size_t{1} << table->bits());
    for (const lists::KeyMap::Item& bucket : buckets) {
        for (const lists::KeyMap::Item* entry = &bucket; entry != nullptr; entry = entry->next) {
            if (entry->value != nullptr) {
                visit(*static_cast<const WorldCell*>(entry->value));
            }
        }
    }
}

// The loaded world's key: which scenes, and how many sectors each has.
uint64_t world_key()
{
    uint64_t key = kFnvBasis;
    for_each_scene([&key](const WorldScene& scene) {
        uint64_t sectors = 0;
        for_each_sector(scene, [&sectors](const WorldCell& /*sector*/) { ++sectors; });
        for (const uint64_t part : {static_cast<uint64_t>(std::bit_cast<uintptr_t>(&scene)), sectors}) {
            key = (key ^ part) * kFnvPrime;
        }
    });
    return key;
}

// The loaded scenes alone (cheap: a few list nodes - each step, snow or not).
uint64_t scenes_key()
{
    uint64_t key = kFnvBasis;
    for_each_scene([&key](const WorldScene& scene) {
        key = (key ^ static_cast<uint64_t>(std::bit_cast<uintptr_t>(&scene))) * kFnvPrime;
    });
    return key;
}

} // namespace

std::vector<snow::Face> collision_faces(bool* has_sky)
{
    const uint32_t indoor = checksums::hash_from_text("Indoor");
    std::vector<snow::Face> faces;
    *has_sky = false;
    for_each_scene([&](const WorldScene& scene) {
        if (scene.is_sky()) {
            *has_sky = true;
            return;
        }
        for_each_sector(scene, [&](const WorldCell& sector) {
            const HitMesh* body = sector.hit_body();
            if (!is_static_collider(body) || (body->flag_bits() & kSwitchedOff) != 0U || body->geometry() == nullptr) {
                return;
            }
            const TriMeshSource& geometry = *body->geometry();
            if (geometry.faces() == nullptr || geometry.vertices() == nullptr) {
                return;
            }
            const bool settles = sector.light_group() != indoor;
            const int vertices = geometry.num_vertices();
            for (int face = 0; std::cmp_less(face, geometry.tri_count()); ++face) {
                if (core::any(geometry.face_flags(face) & kTriggerVolumeFaceFlags)) {
                    continue; // (a trigger or not collidable: nothing settles on it, nothing is under it)
                }
                constexpr uint32_t kTerrainByte = 0xFFU; // (a face's terrain: its low byte, as the game reads it)
                snow::Face out{.settles = settles, .terrain = static_cast<uint8_t>(geometry.face_terrain(face) & kTerrainByte)};
                bool inside = true;
                for (int corner = 0; corner < 3 && inside; ++corner) {
                    const uint32_t index = geometry.face_vertex(face, corner);
                    inside = std::cmp_less(index, vertices);
                    if (inside) {
                        maths::Vec4 at;
                        geometry.read_stored_vertex(static_cast<int>(index), &at);
                        core::at(out.corners, static_cast<std::size_t>(corner)) = {at.x, at.y, at.z};
                    }
                }
                if (inside) {
                    faces.push_back(out);
                }
            }
        });
    });
    return faces;
}

namespace {

void make_map()
{
    const auto start = std::chrono::steady_clock::now();
    bool has_sky = false;
    const std::vector<snow::Face> faces = collision_faces(&has_sky);
    if (!has_sky) {
        snow::set_sky_map(nullptr);
        get_log("snow: this level has no sky (indoors) - no snow here");
        return;
    }
    auto map = std::make_shared<snow::SkyMap>(snow::make_sky_map(faces));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    get_log("snow: the sky map of %u faces (%u indoor): %ux%u cells of %.1f units, %.0f square metres open to the snow; "
            "made in %.0f ms",
            map->faces, map->indoor_faces, map->columns, map->rows, static_cast<double>(map->cell),
            map->snowy_area / static_cast<double>(kSquareInchesPerSquareMetre), ms);
    snow::set_sky_map(map->cells.empty() ? nullptr : std::move(map));
}

// Whether a sky scene is loaded: the level is outdoors (an indoor one - the skate shop, a warehouse - has none).
bool world_has_sky()
{
    bool found = false;
    for_each_scene([&found](const WorldScene& scene) { found = found || scene.is_sky(); });
    return found;
}

std::array<float, 3> row_of(std::span<const float, maths::Mat4::kFloatCount> values, std::size_t row)
{
    return {core::at(values, row * kRow), core::at(values, (row * kRow) + 1), core::at(values, (row * kRow) + 2)};
}

bool finite(const std::array<float, 3>& point)
{
    return std::isfinite(core::at(point, 0)) && std::isfinite(core::at(point, 1)) && std::isfinite(core::at(point, 2));
}

// A point of the model's space in the world (row vectors: x along_x + y along_y + z along_z + origin).
std::array<float, 3> to_world(const std::array<float, 3>& local, const std::array<std::array<float, 3>, 4>& basis)
{
    std::array<float, 3> world = core::at(basis, kTranslationRow);
    for (std::size_t axis = 0; axis < 3; ++axis) {
        core::at(world, axis) += (core::at(local, 0) * core::at(core::at(basis, 0), axis)) +
                                 (core::at(local, 1) * core::at(core::at(basis, 1), axis)) +
                                 (core::at(local, 2) * core::at(core::at(basis, 2), axis));
    }
    return world;
}

// A character's body joints (not its feet: those whose bind pose stands kFootJointHeight or higher) where they are now:
// each joint's bind place (its inverse bind matrix inverted) taken by its bone's matrix (the skin's: inverse bind x
// pose) and the instance's transform. None when its rig isn't known.
void add_body_joints(const Singleton& instance, uint64_t id, const std::array<std::array<float, 3>, 4>& basis,
                     std::vector<snow::Presser>* found)
{
    constexpr float kFootJointHeight = 14.0F; // (a standing skeleton's ankles and toes are under this)
    constexpr float kBodyRadius = 4.5F;
    const gfx::RigData* data = gfx::rig_data_of_matrices(instance.bones());
    if (data == nullptr || data->num() != instance.bone_count_) {
        return;
    }
    const std::span<const maths::Mat4> binds = data->inverse_binds();
    const std::span<const maths::Mat4> bones(instance.bones(), static_cast<std::size_t>(instance.bone_count_));
    for (std::size_t bone = 0; bone < bones.size(); ++bone) {
        maths::Mat4 bind = core::at(binds, bone);
        bind.invert_orthonormal();
        const std::array<float, 3> joint = row_of(bind.floats(), kTranslationRow);
        if (!finite(joint) || core::at(joint, 1) < kFootJointHeight) {
            continue;
        }
        const std::span<const float, maths::Mat4::kFloatCount> skin = core::at(bones, bone).floats();
        const std::array<std::array<float, 3>, 4> skin_basis{row_of(skin, 0), row_of(skin, 1), row_of(skin, 2),
                                                             row_of(skin, kTranslationRow)};
        const std::array<float, 3> at = to_world(to_world(joint, skin_basis), basis);
        if (finite(at)) {
            found->push_back({.id = id ^ ((static_cast<uint64_t>(bone) + 1) << 48U), .kind = snow::PresserKind::kBody, .at = at,
                              .size = kBodyRadius});
        }
    }
}

// An instance drawn without bones, as a mover: its box's bottom middle, its sideways axis and half its track.
void add_mover(const Singleton& instance, uint64_t id, const std::array<std::array<float, 3>, 4>& basis,
               std::vector<snow::Presser>* found)
{
    constexpr float kLeastTrack = 4.0F;
    constexpr float kMostTrack = 60.0F;
    constexpr float kTrackShare = 0.4F; // (of the box's width: the wheels inside its sides)
    const SceneRef* scene = instance.scene();
    if (scene == nullptr) {
        return;
    }
    const maths::BBox& box = scene->bounds();
    if (!(box.max.x >= box.min.x) || !(box.max.y >= box.min.y) || !(box.max.z >= box.min.z) ||
        box.max.x - box.min.x > kMostTrack * 4.0F) {
        return; // (empty, or a building)
    }
    const std::array<float, 3> bottom{(box.min.x + box.max.x) * 0.5F, box.min.y, (box.min.z + box.max.z) * 0.5F};
    const std::array<float, 3> at = to_world(bottom, basis);
    const std::array<float, 3>& side = core::at(basis, 0);
    const float flat = std::sqrt((core::at(side, 0) * core::at(side, 0)) + (core::at(side, 2) * core::at(side, 2)));
    if (!finite(at) || !(flat > 0.0F)) {
        return;
    }
    const float track = std::clamp((box.max.x - box.min.x) * kTrackShare * flat, kLeastTrack, kMostTrack);
    found->push_back({.id = id, .kind = snow::PresserKind::kMover, .at = at,
                      .facing = {core::at(side, 0) / flat, core::at(side, 2) / flat}, .size = track});
}

// The pressers this step: each character's origin and body joints, each vehicle's wheels' hubs, each instance drawn
// without bones (a mover when it moves).
std::vector<snow::Presser> gather_pressers()
{
    std::vector<snow::Presser> found;
    for (const Singleton* instance = first_instance(); instance != nullptr; instance = instance->next()) {
        if (!instance->active()) {
            continue;
        }
        const std::span<const float, maths::Mat4::kFloatCount> values = instance->transform().floats();
        const std::array<float, 3> along_x = row_of(values, 0);
        const std::array<float, 3> along_y = row_of(values, 1);
        const std::array<float, 3> along_z = row_of(values, 2);
        const std::array<float, 3> origin = row_of(values, kTranslationRow);
        if (!finite(origin)) {
            continue;
        }
        const auto id = static_cast<uint64_t>(std::bit_cast<uintptr_t>(instance));
        const std::array<std::array<float, 3>, 4> basis{along_x, along_y, along_z, origin};
        if (instance->bones() == nullptr || instance->bone_count_ < kLeastWheelBones) {
            add_mover(*instance, id, basis, &found);
            continue;
        }
        if (instance->bone_count_ >= surface_trail::kSkeletonBones) {
            const float flat = std::sqrt((core::at(along_z, 0) * core::at(along_z, 0)) + (core::at(along_z, 2) * core::at(along_z, 2)));
            const std::array<float, 2> facing = flat > 0.0F ? std::array<float, 2>{core::at(along_z, 0) / flat, core::at(along_z, 2) / flat}
                                                            : std::array<float, 2>{0.0F, 1.0F};
            found.push_back({.id = id, .kind = snow::PresserKind::kCharacter, .at = origin, .facing = facing});
            add_body_joints(*instance, id, basis, &found);
            continue;
        }
        const std::span<const maths::Mat4> bones(instance->bones(), static_cast<std::size_t>(instance->bone_count_));
        for (std::size_t bone = 0; bone < bones.size(); ++bone) {
            const std::array<float, 3> local = row_of(core::at(bones, bone).floats(), kTranslationRow);
            if (!finite(local) || std::abs(core::at(local, 0)) < kWheelSide || core::at(local, 1) > kWheelLow) {
                continue;
            }
            std::array<float, 3> hub = origin;
            for (std::size_t axis = 0; axis < 3; ++axis) {
                core::at(hub, axis) += (core::at(local, 0) * core::at(along_x, axis)) + (core::at(local, 1) * core::at(along_y, axis)) +
                                       (core::at(local, 2) * core::at(along_z, axis));
            }
            found.push_back({.id = id ^ ((static_cast<uint64_t>(bone) + 1) << 56U), .kind = snow::PresserKind::kWheel, .at = hub});
        }
    }
    return found;
}

} // namespace

void step()
{
    State& own = state();
    // the level's fact for the settings' rule (snow is outdoors only), whether snow is on or not: a level's change is
    // seen in its scenes
    const uint64_t scenes = scenes_key();
    if (!own.keyed || scenes != own.world_key) {
        own.keyed = true;
        own.world_key = scenes;
        own.outdoor = world_has_sky();
        own.made = false;
        if (config::set_level_outdoor(own.outdoor)) {
            // (what the rule moved put to use: snow's settings follow the level - an outdoor level after the indoor
            // front end gets its snow from the first frame, not from the next change on the Options screen)
            graphics_options::reload_live("snow");
            graphics_options::reload_live("rain");
        }
        rain::reset(); // (NOT ORIGINAL - the rain: a new level session - its ground as the weather leaves it)
    }
    // (NOT ORIGINAL - the rain, engine/gfx/rain.h: its clock and the water on the ground, on the same steps - visual only)
    rain::step(frame_clock::get_slomo_frame_seconds());
    // (NOT ORIGINAL - round W: the level's air transport, render_pc/air_world.h - made again when its sectors change)
    air_world::step(world_key());
    // (the paint trails - engine/gfx/paint_trails.h: the game's brushes, the loaded scenes' key their level session)
    if (paint::settings().on) {
        const frame_profile::Scope paint_phase("paint"); // ([debug] frame_profile)
        paint::step_from_game(frame_clock::get_slomo_frame_seconds(), own.world_key);
    }
    if (!snow::settings().on) {
        if (own.made) {
            snow::set_sky_map(nullptr);
            own.made = false;
        }
        return;
    }
    const frame_profile::Scope phase("snow"); // ([debug] frame_profile)
    // (the sectors too: the editor's loads change a scene's sectors, not the scene)
    const uint64_t key = world_key();
    if (!own.made || key != own.sectors_key) {
        own.sectors_key = key;
        own.made = true;
        make_map();
    }
    const auto start = std::chrono::steady_clock::now();
    const std::vector<snow::Presser> pressers = gather_pressers();
    const WeatherSystem* system = weather();
    const bool game_snow = system != nullptr && system->active() && system->snow_rate() > 0.0F;
    snow::step(pressers, frame_clock::get_slomo_frame_seconds(), snow::settings().falling || game_snow);
    // (the step's cost in the log every kReportSteps: what it took, the most, the pressers and the stamps they made)
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    own.report_ms += ms;
    own.report_most = std::max(own.report_most, ms);
    own.report_pressers += pressers.size();
    if (++own.report_steps >= kReportSteps) {
        const snow::Counts counts = snow::counts();
        const auto steps = static_cast<double>(own.report_steps);
        get_log("snow: the trails' step takes %.2f ms (the most %.2f) - %.1f pressers and %.1f stamps a step (%.1f of them "
                "a body's), %zu tiles",
                own.report_ms / steps, own.report_most, static_cast<double>(own.report_pressers) / steps,
                static_cast<double>(counts.stamps - own.report_stamps) / steps,
                static_cast<double>(counts.body - own.report_body) / steps, snow::tile_count());
        own.report_steps = 0;
        own.report_ms = 0.0;
        own.report_most = 0.0;
        own.report_pressers = 0;
        own.report_stamps = counts.stamps;
        own.report_body = counts.body;
    }
}

} // namespace render::snow_world
