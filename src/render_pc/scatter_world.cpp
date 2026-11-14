// NOT ORIGINAL - decals and clutter: the loaded world's surfaces and rails read and handed to the backend
// (scatter_world.h).
#include "render_pc/scatter_world.h"

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ratio>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "base/core/checked_index.h"
#include "base/core/checksums.h"
#include "base/core/enum_ops.h"
#include "base/maths/vec4.h"
#include "engine/gfx/engine_parts.h"
#include "engine/gfx/engine_scene.h"
#include "engine/gfx/game_hooks.h"
#include "engine/gfx/scatter.h"
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

namespace render::scatter_world {

namespace {

constexpr int kMostScenes = 256;         // a scene list longer than this is taken as broken (the editor's guard)
constexpr uint32_t kMostTableBits = 20;  // the largest sector table walked
constexpr uint32_t kSwitchedOff = 4U;    // a collision object's flag: switched off (not collided with)
constexpr uint32_t kTerrainByte = 0xFFU; // (a face's terrain: its low byte, as the game reads it)
constexpr int kTerrainCount = 256;
constexpr uint64_t kFnvBasis = 0xCBF29CE484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001B3ULL;

// The shipped terrain table's order (the level editor's list) - used when the table isn't loaded.
constexpr std::array kShippedTerrains{
    "TERRAIN_DEFAULT",    "TERRAIN_CONCSMOOTH",   "TERRAIN_CONCROUGH",  "TERRAIN_METALSMOOTH", "TERRAIN_METALROUGH",
    "TERRAIN_METALCORRUGATED", "TERRAIN_METALGRATING", "TERRAIN_METALTIN", "TERRAIN_WOOD",     "TERRAIN_WOODMASONITE",
    "TERRAIN_WOODPLYWOOD", "TERRAIN_WOODFLIMSY",  "TERRAIN_WOODSHINGLE", "TERRAIN_WOODPIER",   "TERRAIN_BRICK",
    "TERRAIN_TILE",       "TERRAIN_ASPHALT",      "TERRAIN_ROCK",       "TERRAIN_GRAVEL",      "TERRAIN_SIDEWALK",
    "TERRAIN_GRASS",      "TERRAIN_GRASSDRIED",   "TERRAIN_DIRT",       "TERRAIN_DIRTPACKED",  "TERRAIN_WATER",
    "TERRAIN_ICE",        "TERRAIN_SNOW",         "TERRAIN_SAND",       "TERRAIN_PLEXIGLASS",  "TERRAIN_FIBERGLASS",
    "TERRAIN_CARPET",     "TERRAIN_CONVEYOR",     "TERRAIN_CHAINLINK",  "TERRAIN_METALFUTURE", "TERRAIN_GENERIC1",
    "TERRAIN_GENERIC2",   "TERRAIN_WHEELS",       "TERRAIN_WETCONC",
};

struct State {
    uint64_t key = 0;
    float decals = 0.0F;
    float clutter = 0.0F;
    bool made = false;
    bool sink_was = false;
};

State& state()
{
    static State instance;
    return instance;
}

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

std::vector<scatter::Rail> level_rails()
{
    std::vector<scatter::Rail> rails;
    const auto hook = game_hooks().level_rails;
    if (hook == nullptr) {
        return rails;
    }
    std::vector<std::array<float, 6>> segments;
    hook(&segments);
    rails.reserve(segments.size());
    for (const std::array<float, 6>& s : segments) {
        rails.push_back({.a = {core::at(s, 0), core::at(s, 1), core::at(s, 2)},
                         .b = {core::at(s, 3), core::at(s, 4), core::at(s, 5)}});
    }
    return rails;
}

// The loaded world's key: which scenes and how many sectors each has, and how many rails (a new level, the editor's
// load, the rails made after the scenes: another key).
uint64_t world_key(std::size_t rails)
{
    uint64_t key = kFnvBasis;
    for_each_scene([&key](const WorldScene& scene) {
        uint64_t sectors = 0;
        for_each_sector(scene, [&sectors](const WorldCell& /*sector*/) { ++sectors; });
        for (const uint64_t part : {static_cast<uint64_t>(std::bit_cast<uintptr_t>(&scene)), sectors}) {
            key = (key ^ part) * kFnvPrime;
        }
    });
    return (key ^ rails) * kFnvPrime;
}

// Each terrain index's surface (the terrain table's names, else the shipped order's).
std::array<scatter::Surface, kTerrainCount> terrain_surfaces(bool* from_table)
{
    std::array<scatter::Surface, kTerrainCount> surfaces{};
    std::array<uint32_t, kShippedTerrains.size()> keys{};
    for (std::size_t k = 0; k < kShippedTerrains.size(); ++k) {
        core::at(keys, k) = checksums::hash_from_text(core::at(kShippedTerrains, k));
    }
    *from_table = false;
    for (int index = 0; index < kTerrainCount; ++index) {
        const uint32_t key = surface_kind::fetch_surface_key(static_cast<uint8_t>(index));
        for (std::size_t k = 0; k < keys.size(); ++k) {
            if (key != 0 && key == core::at(keys, k)) {
                core::at(surfaces, static_cast<std::size_t>(index)) =
                    scatter::surface_of_terrain(core::at(kShippedTerrains, k));
                *from_table = true;
                break;
            }
        }
    }
    if (!*from_table) {
        for (std::size_t k = 0; k < kShippedTerrains.size(); ++k) {
            core::at(surfaces, k) = scatter::surface_of_terrain(core::at(kShippedTerrains, k));
        }
    }
    return surfaces;
}

std::vector<scatter::Face> gather_faces(const std::array<scatter::Surface, kTerrainCount>& surfaces)
{
    std::vector<scatter::Face> faces;
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
                const FaceFlag flags = geometry.face_flags(face);
                if (core::any(flags & kTriggerVolumeFaceFlags)) {
                    continue;
                }
                scatter::Face out;
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
                const auto terrain = static_cast<std::size_t>(geometry.face_terrain(face) & kTerrainByte);
                out.surface = core::at(surfaces, terrain);
                out.vert = core::any(flags & FaceFlag::kVert);
                faces.push_back(out);
            }
        });
    });
    return faces;
}

void make(scatter::Sink& sink, float decals, float clutter, const std::vector<scatter::Rail>& rails)
{
    const auto start = std::chrono::steady_clock::now();
    bool from_table = false;
    const auto surfaces = terrain_surfaces(&from_table);
    const std::vector<scatter::Face> faces = gather_faces(surfaces);
    const auto level = game_hooks().level_script;
    const uint32_t seed = level != nullptr ? level() : 0U;
    auto made = std::make_shared<scatter::Scatter>(scatter::make_scatter(
        {.faces = faces, .rails = rails, .seed = seed, .decal_density = decals, .clutter_density = clutter}));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto& of = made->faces_of;
    const auto surface = [&of](scatter::Surface s) { return core::at(of, static_cast<std::size_t>(s)); };
    get_log("scatter: %zu faces (asphalt %u, concrete %u, paving %u, brick %u, dirt %u, gravel %u, grass %u, other %u%s), "
            "%zu rails; made in %.0f ms",
            faces.size(), surface(scatter::Surface::kAsphalt), surface(scatter::Surface::kConcrete),
            surface(scatter::Surface::kPaving), surface(scatter::Surface::kBrick), surface(scatter::Surface::kDirt),
            surface(scatter::Surface::kGravel), surface(scatter::Surface::kGrass), surface(scatter::Surface::kOther),
            from_table ? "" : " - the terrain table isn't loaded: the shipped order", rails.size(), ms);
    if (made->decal_density > 0.0F) {
        const auto& d = made->decals_of;
        const auto kind = [&d](scatter::DecalKind k) { return core::at(d, static_cast<std::size_t>(k)); };
        const scatter::Rejected& r = made->decal_rejected;
        get_log("scatter: %zu decals at density %.2f (cracks %u, stains %u, oil %u, tyre marks %u, gum %u, wet %u, grime "
                "%u; %zu vertices%s) - turned away: %u by ramps, %u by edges, %u by rails, %u covered, %u not in a dip",
                made->decals.size(), static_cast<double>(made->decal_density), kind(scatter::DecalKind::kCrack),
                kind(scatter::DecalKind::kStain), kind(scatter::DecalKind::kOil), kind(scatter::DecalKind::kTyre),
                kind(scatter::DecalKind::kGum), kind(scatter::DecalKind::kWet), kind(scatter::DecalKind::kGrime),
                made->decal_vertices.size(), made->decals_capped ? ", capped" : "", r.ramp, r.drop, r.rail, r.covered,
                r.no_dip);
    }
    if (made->clutter_density > 0.0F) {
        const auto& i = made->items_of;
        const auto kind = [&i](scatter::ItemKind k) { return core::at(i, static_cast<std::size_t>(k)); };
        const scatter::Rejected& r = made->item_rejected;
        get_log("scatter: %zu clutter items at density %.2f (leaves %u, paper %u, cans %u, weeds %u, pebbles %u%s) in %zu "
                "tiles - turned away: %u far from walls, %u by ramps, %u by edges, %u by rails, %u at stairs, %u covered",
                made->items.size(), static_cast<double>(made->clutter_density), kind(scatter::ItemKind::kLeaf),
                kind(scatter::ItemKind::kPaper), kind(scatter::ItemKind::kCan), kind(scatter::ItemKind::kWeed),
                kind(scatter::ItemKind::kPebble), made->items_capped ? ", capped" : "", made->tiles.size(),
                r.far_from_wall, r.ramp, r.drop, r.rail, r.stairs, r.covered);
    }
    const bool empty = made->decals.empty() && made->items.empty();
    sink.set_scatter(empty ? nullptr : std::move(made));
}

} // namespace

void frame()
{
    scatter::Sink* sink = scatter::sink();
    State& st = state();
    if (sink == nullptr) {
        if (st.sink_was) {
            st = State{};
        }
        return;
    }
    st.sink_was = true;
    const frame_profile::Scope phase("scatter"); // ([debug] frame_profile)
    const float decals = sink->wanted_decals();
    const float clutter = sink->wanted_clutter();
    if (!(decals > 0.0F) && !(clutter > 0.0F)) {
        if (st.made) {
            sink->set_scatter(nullptr);
            st.made = false;
        }
        return;
    }
    const std::vector<scatter::Rail> rails = level_rails();
    const uint64_t key = world_key(rails.size());
    if (!st.made || key != st.key || decals != st.decals || clutter != st.clutter) {
        st.key = key;
        st.decals = decals;
        st.clutter = clutter;
        st.made = true;
        make(*sink, decals, clutter, rails);
    }
}

} // namespace render::scatter_world
