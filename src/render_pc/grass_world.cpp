// NOT ORIGINAL - grass: the loaded world's grass found in its collision and handed to the backend (grass_world.h).
#include "render_pc/grass_world.h"

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
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/grass.h"
#include "engine/gfx/render_camera.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/sector.h"
#include "engine/lists/key_map.h"
#include "engine/memory/arena_alloc.h"
#include "engine/render/coll_obj.h"
#include "engine/render/coll_static.h"
#include "engine/render/coll_tri.h"
#include "engine/sound/surface_kind.h"
#include "platform/frame_profile.h"
#include "platform/run_log.h"
#include "render_pc/geom.h"

namespace render::grass_world {

namespace {

constexpr int kMostScenes = 256;         // a scene list longer than this is taken as broken (the editor's guard)
constexpr uint32_t kMostTableBits = 20;  // the largest sector table walked
constexpr uint32_t kSwitchedOff = 4U;    // a collision object's flag: switched off (not collided with)
constexpr uint32_t kTerrainByte = 0xFFU; // (a face's terrain: its low byte, as the game reads it)
constexpr int kTerrainCount = 256;
// The terrain indices the shipped terrain table gives grass (the level editor's list) - used when the table isn't loaded.
constexpr uint8_t kGrassIndex = 20;
constexpr uint8_t kDriedIndex = 21;
constexpr uint64_t kFnvBasis = 0xCBF29CE484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001B3ULL;
constexpr std::size_t kTranslationRow = 12; // (a Mat4's row 3, as floats)

struct State {
    uint64_t world_key = 0;
    float density = 0.0F;
    bool made = false;
    bool sink_was = false;
};

State& state()
{
    static State instance;
    return instance;
}

// Each loaded scene but the skies (the scene list: render::WorldScene nodes until the head).
template <typename Visit>
void for_each_scene(Visit visit)
{
    const memory::ListNode* node = engine_parts().scene_list().next();
    for (int guard = 0; node != nullptr && !node->is_head() && guard < kMostScenes; ++guard) {
        const auto* scene = static_cast<const WorldScene*>(node->data());
        node = node->next();
        if (scene != nullptr && !scene->is_sky()) {
            visit(*scene);
        }
    }
}

// Each sector of a scene (its sector table's buckets and their chains).
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

// The loaded world's key: which scenes, and how many sectors each has (a new level, the editor's load: another key).
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

// The terrain indices of grass and dried grass (the terrain table's, else the shipped order's).
struct GrassTerrains {
    int grass = kGrassIndex;
    int dried = kDriedIndex;
    bool from_table = false;
};

GrassTerrains grass_terrains()
{
    GrassTerrains found{.grass = -1, .dried = -1, .from_table = true};
    const uint32_t grass = checksums::hash_from_text("TERRAIN_GRASS");
    const uint32_t dried = checksums::hash_from_text("TERRAIN_GRASSDRIED");
    for (int index = 0; index < kTerrainCount; ++index) {
        const uint32_t key = surface_kind::fetch_surface_key(static_cast<uint8_t>(index));
        if (key == grass && found.grass < 0) {
            found.grass = index;
        } else if (key == dried && found.dried < 0) {
            found.dried = index;
        }
    }
    if (found.grass < 0 && found.dried < 0) {
        return {};
    }
    return found;
}

grass::Kind kind_of(int terrain, const GrassTerrains& terrains)
{
    if (terrain == terrains.grass) {
        return grass::Kind::kGrass;
    }
    return terrain == terrains.dried ? grass::Kind::kDried : grass::Kind::kBlocker;
}

// Every collidable face of the loaded world's sectors, the grass ones marked.
std::vector<grass::Face> gather_faces(const GrassTerrains& terrains)
{
    std::vector<grass::Face> faces;
    for_each_scene([&](const WorldScene& scene) {
        for_each_sector(scene, [&](const WorldCell& sector) {
            const HitMesh* body = sector.hit_body();
            if (!is_static_collider(body) || (body->flag_bits() & kSwitchedOff) != 0U || body->geometry() == nullptr) {
                return;
            }
            const TriMeshSource& geometry = *body->geometry();
            if (geometry.faces() == nullptr || geometry.vertices() == nullptr) {
                return;
            }
            const int vertices = geometry.num_vertices();
            for (int face = 0; std::cmp_less(face, geometry.tri_count()); ++face) {
                if (core::any(geometry.face_flags(face) & kTriggerVolumeFaceFlags)) {
                    continue; // (a trigger or not collidable: nothing stands on it)
                }
                grass::Face out;
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
                if (!inside) {
                    continue;
                }
                const auto terrain = static_cast<int>(geometry.face_terrain(face) & kTerrainByte);
                out.kind = kind_of(terrain, terrains);
                faces.push_back(out);
            }
        });
    });
    return faces;
}

void make_field(grass::Sink& sink, float density)
{
    const auto start = std::chrono::steady_clock::now();
    const GrassTerrains terrains = grass_terrains();
    const std::vector<grass::Face> faces = gather_faces(terrains);
    auto field = std::make_shared<grass::Field>(grass::make_field(faces, density));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    constexpr double kSquareInchesPerSquareMetre = 1550.0031;
    get_log(
        "grass: %u grass and %u dried grass faces (%.0f square metres; terrains %d / %d%s), %u steep ones left bare, "
        "%u other faces - %zu blades in %zu tiles at density %.2f%s, %u roots under something; made in %.0f ms",
        field->grass_faces, field->dried_faces, field->area / kSquareInchesPerSquareMetre, terrains.grass,
        terrains.dried, terrains.from_table ? "" : " - the terrain table isn't loaded: the shipped order",
        field->steep_faces, field->blocker_faces, field->blades.size(), field->tiles.size(),
        static_cast<double>(field->density), field->capped ? " (lowered: too many blades)" : "", field->covered, ms);
    if (field->blades.empty()) {
        get_log("grass: no grass in this world's collision - none drawn");
    }
    sink.set_field(field->blades.empty() ? nullptr : std::move(field));
}

// The skaters and peds (instances drawn with bones), nearest the camera first.
void send_pushers(grass::Sink& sink)
{
    const std::array<float, 3> camera = renderer().render_camera().position;
    struct Near {
        grass::Pusher pusher;
        float distance = 0.0F;
    };
    std::vector<Near> found;
    for (const Singleton* instance = first_instance(); instance != nullptr; instance = instance->next()) {
        if (!instance->active() || instance->bones() == nullptr || instance->bone_count_ <= 0) {
            continue;
        }
        const std::span<const float, maths::Mat4::kFloatCount> values = instance->transform().floats();
        const std::array<float, 3> at{core::at(values, kTranslationRow), core::at(values, kTranslationRow + 1),
                                      core::at(values, kTranslationRow + 2)};
        if (!std::isfinite(core::at(at, 0)) || !std::isfinite(core::at(at, 1)) || !std::isfinite(core::at(at, 2))) {
            continue;
        }
        const float dx = core::at(at, 0) - core::at(camera, 0);
        const float dy = core::at(at, 1) - core::at(camera, 1);
        const float dz = core::at(at, 2) - core::at(camera, 2);
        found.push_back({.pusher = {.at = at, .radius = grass::kCharacterReach},
                         .distance = std::sqrt((dx * dx) + (dy * dy) + (dz * dz))});
    }
    std::ranges::sort(found, [](const Near& one, const Near& two) { return one.distance < two.distance; });
    std::array<grass::Pusher, grass::kMostPushers> pushers{};
    const std::size_t count = std::min(found.size(), pushers.size());
    for (std::size_t index = 0; index < count; ++index) {
        core::at(pushers, index) = core::at(found, index).pusher;
    }
    sink.set_pushers(std::span<const grass::Pusher>(pushers.data(), count));
}

} // namespace

void frame()
{
    grass::Sink* sink = grass::sink();
    State& st = state();
    if (sink == nullptr) {
        if (st.sink_was) {
            st = State{}; // (installed again later: the field made again)
        }
        return;
    }
    st.sink_was = true;
    const frame_profile::Scope phase("grass"); // ([debug] frame_profile)
    const float density = sink->wanted_density();
    if (!(density > 0.0F)) {
        if (st.made) {
            sink->set_field(nullptr);
            st.made = false;
        }
        return;
    }
    const uint64_t key = world_key();
    if (!st.made || key != st.world_key || density != st.density) {
        st.world_key = key;
        st.density = density;
        st.made = true;
        make_field(*sink, density);
    }
    send_pushers(*sink);
}

} // namespace render::grass_world
