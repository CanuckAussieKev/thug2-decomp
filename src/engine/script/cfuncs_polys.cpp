// Script commands of the engine, 0x004472D0-0x00447F30: spawned polygons, vertex colours of the scenes' sectors
// (CompressVC / NudgeVC), the light direction and the renderer's metrics (see cfuncs_engine.h).

#include "engine/script/cfuncs_engine.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <array>

#include "engine/core/arena_alloc.h"
#include "engine/core/mem.h"
#include "engine/core/slab_bank.h"
#include "engine/lists/key_map.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_geom.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/sector.h"
#include "engine/gfx/spawned_polys.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/engine_scene.h"
#include "engine/core/checked_index.h"
#include "engine/core/packed_colour.h"
#include "engine/gfx/engine_parts.h"
#include "engine/maths/vec2.h"
#include "engine/core/checksums.h"
#include <span>
#include <vector>
#include <gsl/assert>
namespace commands {

using script::Array;
using script::Script;
using script::Struct;

namespace {
constexpr double kMaxChannel = 255.0;
constexpr float kPercentToFraction = 0.01F;
constexpr unsigned kVectorValueBytes = 16;
constexpr double kHashPositionScale = 16.0;
constexpr uint32_t kHashMask = 0x7FFF;
constexpr double kHashRange = 32768.0;
constexpr double kDouble = 2.0;

constexpr uint32_t kPos = 0x7F261953;          // "pos"
constexpr uint32_t kSpawnVector1 = 0xC2B31CF0; // (name unknown) SpawnPoly's first vector, (0, 0, 1, 1)
constexpr uint32_t kSpawnVector2 = 0x70A38CFD; // (name unknown) SpawnPoly's second vector, (0, 1, 0, 1)
constexpr uint32_t kStartDims = 0x0B1FE8B6;    // "start_dims"
constexpr uint32_t kEndDims = 0x1A640930;      // "end_dims"
constexpr uint32_t kLifetime = 0xC218CF77;     // "lifetime"
constexpr uint32_t kStartColors = 0xF78EABF0;  // "start_colors"
constexpr uint32_t kEndColors = 0x4D5A57A8;    // "end_colors"
constexpr uint32_t kStartColor = 0xECFCADE7;   // "start_color"
constexpr uint32_t kEndColor = 0xE836F013;     // "end_color"
constexpr uint32_t kSpawnFlag2 = 0x9C367E9B;   // (name unknown) SpawnPoly's flag 2
constexpr uint32_t kJust = 0x8B60022F;         // "just"
constexpr uint32_t kRight = 0x4B358AEB;        // "right"
constexpr uint32_t kLeft = 0x85981897;         // "left"
constexpr uint32_t kCenter = 0xBF0F14DB;       // "center"
constexpr uint32_t kTop = 0xE126E035;          // "top"
constexpr uint32_t kBottom = 0x76A08D5B;       // "bottom"
constexpr uint32_t kBlendMode = 0xDD6BB3D5;    // "BlendMode"
constexpr uint32_t kTexture = 0x7D99F28D;      // "texture"
constexpr uint32_t kTarget = 0xB990D003;       // "target"
constexpr uint32_t kPercent = 0x9E497FC6;      // "percent"
constexpr uint32_t kLightGroup = 0x7E4813A7;   // "LightGroup"
constexpr uint32_t kIndex = 0x7F8C98FE;        // "index"
constexpr uint32_t kDirection = 0xC1B52E4C;    // "direction"
constexpr uint32_t kHeading = 0xFD4BC03E;      // "heading"
constexpr uint32_t kPitch = 0xD8604126;        // "pitch"
constexpr uint32_t kMetrics = 0xDD755518;      // "Metrics"

// The loaded scenes' list (0x006FA7EC, ended by a node whose scene is -1): walked by the engine's helpers.
// A scene's flag at +0x8D: a sky. 0x0048C7B0: the first scene that is one.
bool is_sky_backdrop(const render::WorldScene* scene) { return scene->is_sky(); }
render::WorldScene* first_sky_scene() { return render::get_sky_scene(); }

// A sector's geometry (0x004A2BA0: +0x14) and the geometry's vertex count / positions / colours (0x0048DF70,
// 0x0048DF80, 0x0048E150 into a buffer, 0x0048DFA0 back from one).
void* sector_geometry(void* sector) { return static_cast<render::WorldCell*>(sector)->geom_handle(); }
// (the geometry's virtual functions)
render::Geom* as_geom(void* geometry) { return static_cast<render::Geom*>(geometry); }
int get_vertex_count(void* geometry) { return static_cast<int>(as_geom(geometry)->fetch_vertex_tally()); }
void get_positions(void* geometry, script::VectorValue* out)
{
    as_geom(geometry)->get_vertices({out, as_geom(geometry)->fetch_vertex_tally()}); // (x, y, z, w each)
}
void get_colours(void* geometry, std::span<colour::Bytes> out)
{
    static_assert(sizeof(colour::Bytes) == sizeof(uint32_t));
    std::vector<uint32_t> words(static_cast<size_t>(get_vertex_count(geometry)));
    static_cast<render::Geom*>(geometry)->fetch_tints(words);
    for (size_t i = 0; i < words.size(); ++i) {
        core::at(out, i) = colour::bytes_of(core::at(words, i));
    }
}
void set_colours(void* geometry, std::span<const colour::Bytes> colours)
{
    std::vector<uint32_t> words(static_cast<size_t>(get_vertex_count(geometry)));
    for (size_t i = 0; i < words.size(); ++i) {
        core::at(words, i) = colour::packed_of(core::at(colours, i));
    }
    as_geom(geometry)->change_colors(words);
}

// The CRT's _ftol (0x006236FC): truncation towards zero; callers keep the low bits.
int64_t ftol(double value) { return static_cast<int64_t>(value); }

// A colour channel limited to 0..255 (NaN goes through) and truncated to a byte.
uint8_t channel_byte(double value)
{
    if (value < 0.0) {
        value = 0.0;
    } else if (value > kMaxChannel) {
        value = kMaxChannel;
    }
    return static_cast<uint8_t>(ftol(value));
}

// A scene's sectors (its hash table at +0xD0) - those with geometry, of the light group if one is given.
template <typename Fn>
void for_each_sector_geometry(render::WorldScene* scene, uint32_t light_group, Fn fn)
{
    lists::KeyMap* sectors = scene->sectors();
    if (!sectors) {
        return;
    }
    sectors->set_iter_bucket(-1);
    sectors->set_iter_item(nullptr);
    if (!sectors->step_iterator() || !sectors->iter_item()) {
        return;
    }
    for (auto* sector = static_cast<render::WorldCell*>(sectors->iter_item()->value); sector;) {
        void* geometry = sector_geometry(sector);
        if (geometry && (!light_group || light_group == sector->light_group())) {
            fn(geometry);
        }
        if (!sectors->step_iterator() || !sectors->iter_item()) {
            break;
        }
        sector = static_cast<render::WorldCell*>(sectors->iter_item()->value);
    }
}

} // namespace

// 0x004472D0: a spawned polygon's details given back to their pool pair (0x00700500) - its operator delete.
void free_spawned_poly_details(void* details) { render::spawned_poly_pools().dealloc(details); }

// 0x00447330: SpawnPoly pos=<vector> <vector> <vector> start_dims=<pair> end_dims=<pair> lifetime=<f>
// start_colors=[[r g b a] x4] end_colors=[...] | start_color=[r g b a] end_color=[...] [flag] [just=[h v]]
// [BlendMode] [texture]: a new spawned polygon's details filled in and passed on (0x00492450) with the two vectors.
// Always true.
bool cfunc_spawn_poly(Struct* params, Script* /*script*/)
{
    gsl::owner<render::SpawnedPoly*> details =
        memory::adopt<render::SpawnedPoly*>(render::spawned_poly_pools().allocate<render::SpawnedPoly>());
    if (details) {
        details = memory::construct_in<render::SpawnedPoly>(details);
    }
    // (a failed allocation writes through null below, as in the original)
    Expects(details != nullptr);
    params->fetch_vec(kPos, &details->pos(), 1);
    script::VectorValue vector1 = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 1.0F};
    script::VectorValue vector2 = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
    params->fetch_vec(kSpawnVector1, &vector1, 0);
    params->fetch_vec(kSpawnVector2, &vector2, 0);
    maths::Vec2 dims{.x = core::at(details->start_dims(), 0), .y = core::at(details->start_dims(), 1)};
    params->fetch_pair(kStartDims, &dims, 1);
    core::at(details->start_dims(), 0) = dims.x;
    core::at(details->start_dims(), 1) = dims.y;
    dims = {.x = core::at(details->end_dims(), 0), .y = core::at(details->end_dims(), 1)};
    params->fetch_pair(kEndDims, &dims, 1);
    core::at(details->end_dims(), 0) = dims.x;
    core::at(details->end_dims(), 1) = dims.y;
    params->fetch_float(kLifetime, &details->lifetime(), 1);
    Array* start = nullptr; // (uninitialised in the original)
    Array* end = nullptr;
    if (params->fetch_list(kStartColors, &start, 0)) {
        // a colour per corner
        params->fetch_list(kEndColors, &end, 1);
        for (uint32_t corner = 0; corner < 4; ++corner) {
            auto* start_colour = start->array_at(corner);
            auto* end_colour = end->array_at(corner);
            for (uint32_t i = 0; i < 4; ++i) {
                core::at(core::at(details->start_colours(), corner), i) = static_cast<uint8_t>(start_colour->fetch_part(i));
            }
            for (uint32_t i = 0; i < 4; ++i) {
                core::at(core::at(details->end_colours(), corner), i) = static_cast<uint8_t>(end_colour->fetch_part(i));
            }
        }
    } else if (params->fetch_list(kStartColor, &start, 0)) {
        // one colour for every corner
        params->fetch_list(kEndColor, &end, 1);
        for (uint32_t corner = 0; corner < 4; ++corner) {
            for (uint32_t i = 0; i < 4; ++i) {
                core::at(core::at(details->start_colours(), corner), i) = static_cast<uint8_t>(start->fetch_part(i));
            }
            for (uint32_t i = 0; i < 4; ++i) {
                core::at(core::at(details->end_colours(), corner), i) = static_cast<uint8_t>(end->fetch_part(i));
            }
        }
    }
    if (params->has_bare_flag(kSpawnFlag2)) {
        details->flags() |= 2;
    }
    Array* just = nullptr;
    details->set_align_h(0.0F);
    details->set_align_v(0.0F);
    if (params->fetch_list(kJust, &just, 0)) {
        switch (just->fetch_part(0)) {
        case kRight:
            details->set_align_h(1.0F);
            break;
        case kLeft:
            details->set_align_h(-1.0F);
            break;
        case kCenter:
            details->set_align_h(0.0F);
            break;
        default:
            break;
        }
        switch (just->fetch_part(1)) {
        case kBottom:
            details->set_align_v(1.0F);
            break;
        case kCenter:
            details->set_align_v(0.0F);
            break;
        case kTop:
            details->set_align_v(-1.0F);
            break;
        default:
            break;
        }
    }
    params->read_hash_value(kBlendMode, &details->blend_mode(), 0);
    uint32_t texture = 0;
    params->read_hash_value(kTexture, &texture, 0);
    details->set_texture(
        (texture != 0U) ? render::engine_parts().particle_texture_dict()->fetch_skin(texture) // the texture dictionary
                        : nullptr);
    render::engine::spawn_poly(details,
                               &vector1, &vector2);
    return true;
}

// 0x00447710: the vertex colours of a scene's sectors (of `lightGroup`, if given) moved towards `target`: each
// channel c becomes (c - target) * factor + target, limited to 0..255 (alpha kept). Nothing when target and factor
// are both 0.
void compress_scene_vertex_colours(render::WorldScene* scene, float target, float factor, checksums::Name light_group)
{
    for_each_sector_geometry(scene, light_group.value(), [&](void* geometry) {
        const int count = get_vertex_count(geometry);
        if (!count) {
            return;
        }
        // (the original runs an empty constructor over the array: 0x005902E0)
        auto* colour_block = static_cast<colour::Bytes*>(memory::alloc(static_cast<unsigned int>(count) * 4));
        const std::span<colour::Bytes> colours(colour_block, static_cast<std::size_t>(count));
        get_colours(geometry, colours);
        if (target != 0.0F || factor != 0.0F) {
            for (int i = 0; i < count; ++i) {
                colour::Bytes& colour = core::at(colours, i);
                // x87: in double, rounded only by the truncation
                for (int channel = 0; channel < 3; ++channel) {
                    core::at(colour, channel) =
                        channel_byte(((static_cast<double>(core::at(colour, channel)) - target) * factor) + target);
                }
            }
        }
        set_colours(geometry, colours);
        memory::dealloc(colour_block);
    });
}

// 0x00447980: CompressVC target=<n> percent=<n> [LightGroup=<name>]: every scene's (skies apart, then the first
// sky's) vertex colours compressed towards target, to (100 - percent)% of their distance. Always true.
bool cfunc_compress_vc(Struct* params, Script* /*script*/)
{
    float target = 0.0F;
    float percent = 0.0F;
    params->fetch_float(kTarget, &target, 1);
    params->fetch_float(kPercent, &percent, 0);
    auto factor = static_cast<float>((100.0 - percent) * static_cast<double>(kPercentToFraction)); // (x87: rounded once)
    uint32_t light_group = 0;
    params->read_hash_value(kLightGroup, &light_group, 0);
    for (memory::ListNode* node = render::first_scene_node(); node;) {
        memory::ListNode* next = render::next_scene_node(node);
        if (!is_sky_backdrop(render::scene_of_node(node))) {
            compress_scene_vertex_colours(render::scene_of_node(node), target, factor, checksums::Name{light_group});
        }
        node = next;
    }
    if (render::WorldScene* sky = first_sky_scene()) {
        compress_scene_vertex_colours(sky, target, factor, checksums::Name{light_group});
    }
    return true;
}

// 0x00447A60: the vertex colours of a scene's sectors scaled by a pseudo-random amount per vertex: a 15-bit hash
// of the position (in 1/16ths) h gives the scale 1 - amount + 2 * amount * h / 32768 (colours limited to 0..255,
// alpha kept).
void nudge_scene_vertex_colours(render::WorldScene* scene, float amount)
{
    for_each_sector_geometry(scene, 0, [&](void* geometry) {
        const int count = get_vertex_count(geometry);
        if (!count) {
            return;
        }
        // (the original runs an empty constructor over both arrays: 0x005902E0)
        auto* position_block = static_cast<script::VectorValue*>(memory::alloc(static_cast<unsigned int>(count) * kVectorValueBytes));
        auto* colour_block = static_cast<colour::Bytes*>(memory::alloc(static_cast<unsigned int>(count) * 4));
        const std::span<script::VectorValue> positions(position_block, static_cast<std::size_t>(count));
        const std::span<colour::Bytes> colours(colour_block, static_cast<std::size_t>(count));
        get_positions(geometry, position_block);
        get_colours(geometry, colours);
        for (int i = 0; i < count; ++i) {
            const script::VectorValue& position = core::at(positions, i);
            colour::Bytes& colour = core::at(colours, i);
            auto hash = static_cast<uint32_t>(ftol(static_cast<double>(position.x) * kHashPositionScale)) *
                        static_cast<uint32_t>(ftol(static_cast<double>(position.y) * kHashPositionScale));
            hash = (hash ^ static_cast<uint32_t>(ftol(static_cast<double>(position.z) * kHashPositionScale))) & kHashMask;
            // x87: the scale rounded to a float; red and green products rounded to floats, blue's kept in double
            auto scale =
                static_cast<float>((static_cast<double>(hash) * (1.0 / kHashRange) * amount * kDouble) + 1.0 - amount);
            auto red = static_cast<float>(std::get<0>(colour) * static_cast<double>(scale));
            auto green = static_cast<float>(std::get<1>(colour) * static_cast<double>(scale));
            const double blue = std::get<2>(colour) * static_cast<double>(scale);
            std::get<0>(colour) = channel_byte(red);
            std::get<1>(colour) = channel_byte(green);
            std::get<2>(colour) = channel_byte(blue);
        }
        set_colours(geometry, colours);
        memory::dealloc(position_block);
        memory::dealloc(colour_block);
    });
}

// 0x00447D90: NudgeVC percent=<n>: every scene's vertex colours (skies too) nudged by up to percent%. Always true.
bool cfunc_nudge_vc(Struct* params, Script* /*script*/)
{
    float percent = 0.0F;
    params->fetch_float(kPercent, &percent, 1);
    percent = percent * kPercentToFraction;
    for (memory::ListNode* node = render::first_scene_node(); node;) {
        memory::ListNode* next = render::next_scene_node(node);
        nudge_scene_vertex_colours(render::scene_of_node(node), percent);
        node = next;
    }
    return true;
}

// 0x00447E00: SetLightDirection index=<n> direction=<vector> | heading=<degrees> pitch=<degrees>: the light's
// direction (0x0048ED70; w 0): the vector, or (0, 0, 1) turned by pitch about x and then by heading about y (both
// needed; otherwise (0, 0, 1)). Returns what 0x0048ED70 returns (its AL).
bool cfunc_set_light_direction(Struct* params, Script* /*script*/)
{
    constexpr float kRadiansPerDegree = 0.0174532924F; // (0x00645464)
    int index = 0;                                     // (uninitialised in the original)
    params->fetch_int_value(kIndex, &index, 0);
    script::VectorValue direction = {.x = 0.0F, .y = 0.0F, .z = 1.0F, .w = 0.0F};
    float heading = 0.0F;
    float pitch = 0.0F;
    if (params->fetch_vec(kDirection, &direction, 0)) {
        direction.w = 0.0F;
    } else if (params->fetch_float(kHeading, &heading, 0) && params->fetch_float(kPitch, &pitch, 0)) {
        const double pitch_radians = static_cast<double>(pitch) * kRadiansPerDegree;
        const float y = direction.y;
        const float z = direction.z;
        direction.y = static_cast<float>((std::cos(pitch_radians) * y) - (std::sin(pitch_radians) * z));
        const double turned_z = (std::cos(pitch_radians) * z) + (std::sin(pitch_radians) * y);
        const double heading_radians = static_cast<double>(heading) * kRadiansPerDegree;
        auto sine = static_cast<float>(std::sin(heading_radians));
        auto cosine = static_cast<float>(std::cos(heading_radians));
        const float x = direction.x;
        direction.z = static_cast<float>(-(static_cast<double>(sine) * x) + (turned_z * cosine));
        direction.x = static_cast<float>((turned_z * sine) + (static_cast<double>(cosine) * x));
    }
    return render::set_light_direction(index, &direction);
}

// 0x00447F30: GetMetrics: the renderer's metrics (0x0048CB00 into a new structure) added to the script's params as
// Metrics. Always true.
bool cfunc_get_metrics(Struct* /*params*/, Script* script)
{
    gsl::owner<Struct*> metrics = script::spawn_param_bag();
    if (metrics) {
        metrics = new (metrics) script::Struct;
    }
    render::get_scene_debug_info(metrics);
    script->args()->put_sub_struct(kMetrics, metrics);
    if (metrics) {
        metrics->get_clear();
        script::delete_struct(metrics);
    }
    return true;
}

} // namespace commands
