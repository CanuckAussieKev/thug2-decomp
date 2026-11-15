// NOT ORIGINAL - the surface trails' world: the snow's sky map and pressers, the paint's step (snow_world.h).
#include "render_pc/snow_world.h"

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
#include "engine/gfx/sector.h"
#include "engine/gfx/paint_trails.h"
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
};

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

// Every collidable face of the loaded world's sectors (the skies' left out), an indoor sector's marked.
std::vector<snow::Face> gather_faces(bool* has_sky)
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
                snow::Face out{.settles = settles};
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

void make_map()
{
    const auto start = std::chrono::steady_clock::now();
    bool has_sky = false;
    const std::vector<snow::Face> faces = gather_faces(&has_sky);
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

// The pressers this step: each character's origin, each vehicle's wheels' hubs.
std::vector<snow::Presser> gather_pressers()
{
    std::vector<snow::Presser> found;
    for (const Singleton* instance = first_instance(); instance != nullptr; instance = instance->next()) {
        if (!instance->active() || instance->bones() == nullptr || instance->bone_count_ < kLeastWheelBones) {
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
        if (instance->bone_count_ >= surface_trail::kSkeletonBones) {
            const float flat = std::sqrt((core::at(along_z, 0) * core::at(along_z, 0)) + (core::at(along_z, 2) * core::at(along_z, 2)));
            const std::array<float, 2> facing = flat > 0.0F ? std::array<float, 2>{core::at(along_z, 0) / flat, core::at(along_z, 2) / flat}
                                                            : std::array<float, 2>{0.0F, 1.0F};
            found.push_back({.id = id, .kind = snow::PresserKind::kCharacter, .at = origin, .facing = facing});
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
        config::set_level_outdoor(own.outdoor);
    }
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
    const std::vector<snow::Presser> pressers = gather_pressers();
    const WeatherSystem* system = weather();
    const bool game_snow = system != nullptr && system->active() && system->snow_rate() > 0.0F;
    snow::step(pressers, frame_clock::get_slomo_frame_seconds(), snow::settings().falling || game_snow);
}

} // namespace render::snow_world
