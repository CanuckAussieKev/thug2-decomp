// Script commands of the engine, 0x004472D0-0x00447F30: spawned polygons, vertex colours of the scenes' sectors
// (CompressVC / NudgeVC), the light direction and the renderer's metrics (see cfuncs_engine.h).

#include "engine/script/cfuncs_engine.h"

#include <cstdint>

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

namespace commands {

using script::Array;
using script::Script;
using script::Struct;

namespace {

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

// A spawned polygon's settings (0xB8 bytes, from the pool pair 0x00700500; constructed by 0x00491990: zeroed,
// blend mode 0x54628ED7). The fields not named are set elsewhere (0x00492450).
struct SpawnedPolyDetails {
    script::VectorValue pos; // +0x00
    uint8_t unknown10[0x48];
    float start_dims_[2];         // +0x58
    float end_dims_[2];           // +0x60
    uint8_t start_colours_[4][4]; // +0x68 (r, g, b, a per corner)
    uint8_t end_colours_[4][4];   // +0x78
    uint8_t unknown88[0x10];
    uint32_t flags; // +0x98
    uint32_t unknown9c_;
    float lifetime; // +0xA0
    void* texture;  // +0xA4
    uint32_t unknowna8_;
    uint32_t blend_mode_; // +0xAC
    float align_h_;       // +0xB0 (-1 left, 0 centre, 1 right)
    float align_v_;       // +0xB4 (-1 top, 0 centre, 1 bottom)
};
static_assert(sizeof(SpawnedPolyDetails) == 0xB8, "layout must match the original");

memory::PoolPair& spawned_poly_pools() { return render::g_spawned_poly_pools; }

// The loaded scenes' list (0x006FA7EC): nodes (memory::ListNode) {scene +0, priority, next +8}, ended by a node whose scene is -1.
struct SceneNode {
    uint8_t* scene;
    int priority;
    SceneNode* next;
};
static_assert(sizeof(SceneNode) == 0x0C, "memory::ListNode's data, priority and next");
SceneNode* first_scene_node() { return reinterpret_cast<SceneNode*>(render::first_scene_node()); }
SceneNode* next_scene_node(SceneNode* node)
{
    SceneNode* next = node->next;
    return next->scene == reinterpret_cast<uint8_t*>(-1) ? nullptr : next;
}
// A scene's flag at +0x8D: a sky. 0x0048C7B0: the first scene that is one.
bool is_sky_backdrop(const uint8_t* scene) { return scene[0x8D] != 0; }
uint8_t* first_sky_scene() { return reinterpret_cast<uint8_t*>(render::get_sky_scene()); }

// A sector's geometry (0x004A2BA0: +0x14) and the geometry's vertex count / positions / colours (0x0048DF70,
// 0x0048DF80, 0x0048E150 into a buffer, 0x0048DFA0 back from one).
void* sector_geometry(void* sector) { return static_cast<render::WorldCell*>(sector)->geom_handle_; }
// (the geometry's virtual functions)
render::Geom* as_geom(void* geometry) { return static_cast<render::Geom*>(geometry); }
int get_vertex_count(void* geometry) { return static_cast<int>(as_geom(geometry)->fetch_vertex_tally()); }
void get_positions(void* geometry, script::VectorValue* out)
{
    as_geom(geometry)->get_vertices(reinterpret_cast<maths::Vec4*>(out)); // (x, y, z, w each)
}
void get_colours(void* geometry, uint32_t* out) { static_cast<render::Geom*>(geometry)->fetch_tints(out); }
void set_colours(void* geometry, uint32_t* colours)
{
    as_geom(geometry)->change_colors(reinterpret_cast<const uint8_t*>(colours));
}

// The CRT's _ftol (0x006236FC): truncation towards zero; callers keep the low bits.
int64_t ftol(double value) { return static_cast<int64_t>(value); }

// A colour channel limited to 0..255 (NaN goes through) and truncated to a byte.
uint8_t channel_byte(double value)
{
    if (value < 0.0)
        value = 0.0;
    else if (value > 255.0)
        value = 255.0;
    return static_cast<uint8_t>(ftol(value));
}

// A scene's sectors (its hash table at +0xD0) - those with geometry, of the light group if one is given.
template <typename Fn>
void for_each_sector_geometry(uint8_t* scene, uint32_t light_group, Fn fn)
{
    auto* sectors = *reinterpret_cast<lists::KeyMap**>(scene + 0xD0);
    if (!sectors)
        return;
    sectors->iter_bucket_ = -1;
    sectors->iter_item_ = nullptr;
    if (!sectors->step_iterator() || !sectors->iter_item_)
        return;
    for (auto* sector = static_cast<uint8_t*>(sectors->iter_item_->value); sector;) {
        void* geometry = sector_geometry(sector);
        if (geometry && (!light_group || light_group == *reinterpret_cast<uint32_t*>(sector + 0xC)))
            fn(geometry);
        if (!sectors->step_iterator() || !sectors->iter_item_)
            break;
        sector = static_cast<uint8_t*>(sectors->iter_item_->value);
    }
}

// The x87's own FSIN / FCOS (the CRT's round differently); products with the result kept at extended precision,
// as the original keeps it on the x87 stack.
float x87_sin(double angle)
{
    float result;
    __asm {
        fld angle
        fsin
        fstp result
    }
    return result;
}
float x87_cos(double angle)
{
    float result;
    __asm {
        fld angle
        fcos
        fstp result
    }
    return result;
}
double x87_sin_times(double angle, float value)
{
    double result;
    __asm {
        fld angle
        fsin
        fmul value
        fstp result
    }
    return result;
}
double x87_cos_times(double angle, float value)
{
    double result;
    __asm {
        fld angle
        fcos
        fmul value
        fstp result
    }
    return result;
}

} // namespace

// 0x004472D0: a spawned polygon's details given back to their pool pair (0x00700500) - its operator delete.
void __cdecl free_spawned_poly_details(void* details) { spawned_poly_pools().dealloc(details); }

// 0x00447330: SpawnPoly pos=<vector> <vector> <vector> start_dims=<pair> end_dims=<pair> lifetime=<f>
// start_colors=[[r g b a] x4] end_colors=[...] | start_color=[r g b a] end_color=[...] [flag] [just=[h v]]
// [BlendMode] [texture]: a new spawned polygon's details filled in and passed on (0x00492450) with the two vectors.
// Always true.
bool __cdecl cfunc_spawn_poly(Struct* params, Script*)
{
    auto* details = static_cast<SpawnedPolyDetails*>(spawned_poly_pools().carve(sizeof(SpawnedPolyDetails)));
    if (details)
        details = reinterpret_cast<SpawnedPolyDetails*>(reinterpret_cast<render::SpawnedPoly*>(details)->make_new());
    // (a failed allocation writes through null below, as in the original)
    params->fetch_vec(kPos, &details->pos, 1);
    script::VectorValue vector1 = {0.0f, 0.0f, 1.0f, 1.0f};
    script::VectorValue vector2 = {0.0f, 1.0f, 0.0f, 1.0f};
    params->fetch_vec(kSpawnVector1, &vector1, 0);
    params->fetch_vec(kSpawnVector2, &vector2, 0);
    params->fetch_pair(kStartDims, &details->start_dims_[0], &details->start_dims_[1], 1);
    params->fetch_pair(kEndDims, &details->end_dims_[0], &details->end_dims_[1], 1);
    params->fetch_float(kLifetime, &details->lifetime, 1);
    Array* start = nullptr; // (uninitialised in the original)
    Array* end = nullptr;
    if (params->fetch_list(kStartColors, &start, 0)) {
        // a colour per corner
        params->fetch_list(kEndColors, &end, 1);
        for (uint32_t corner = 0; corner < 4; ++corner) {
            auto* start_colour = reinterpret_cast<Array*>(start->fetch_part(corner));
            auto* end_colour = reinterpret_cast<Array*>(end->fetch_part(corner));
            for (uint32_t i = 0; i < 4; ++i)
                details->start_colours_[corner][i] = static_cast<uint8_t>(start_colour->fetch_part(i));
            for (uint32_t i = 0; i < 4; ++i)
                details->end_colours_[corner][i] = static_cast<uint8_t>(end_colour->fetch_part(i));
        }
    } else if (params->fetch_list(kStartColor, &start, 0)) {
        // one colour for every corner
        params->fetch_list(kEndColor, &end, 1);
        for (uint32_t corner = 0; corner < 4; ++corner) {
            for (uint32_t i = 0; i < 4; ++i)
                details->start_colours_[corner][i] = static_cast<uint8_t>(start->fetch_part(i));
            for (uint32_t i = 0; i < 4; ++i)
                details->end_colours_[corner][i] = static_cast<uint8_t>(end->fetch_part(i));
        }
    }
    if (params->has_bare_flag(kSpawnFlag2))
        details->flags |= 2;
    Array* just = nullptr;
    details->align_h_ = 0.0f;
    details->align_v_ = 0.0f;
    if (params->fetch_list(kJust, &just, 0)) {
        switch (just->fetch_part(0)) {
        case kRight:
            details->align_h_ = 1.0f;
            break;
        case kLeft:
            details->align_h_ = -1.0f;
            break;
        case kCenter:
            details->align_h_ = 0.0f;
            break;
        default:
            break;
        }
        switch (just->fetch_part(1)) {
        case kBottom:
            details->align_v_ = 1.0f;
            break;
        case kCenter:
            details->align_v_ = 0.0f;
            break;
        case kTop:
            details->align_v_ = -1.0f;
            break;
        default:
            break;
        }
    }
    params->read_hash_value(kBlendMode, &details->blend_mode_, 0);
    uint32_t texture = 0;
    params->read_hash_value(kTexture, &texture, 0);
    details->texture = texture
                           ? static_cast<render::TextureSet*>(reinterpret_cast<void*>(render::g_particle_texture_dict))
                                 ->fetch_skin(texture) // the texture dictionary
                           : nullptr;
    render::engine::spawn_poly(reinterpret_cast<render::SpawnedPoly*>(details),
                               reinterpret_cast<maths::Vec4*>(&vector1), reinterpret_cast<maths::Vec4*>(&vector2));
    return true;
}

// 0x00447710: the vertex colours of a scene's sectors (of `lightGroup`, if given) moved towards `target`: each
// channel c becomes (c - target) * factor + target, limited to 0..255 (alpha kept). Nothing when target and factor
// are both 0.
void __cdecl compress_scene_vertex_colours(uint8_t* scene, float target, float factor, uint32_t light_group)
{
    for_each_sector_geometry(scene, light_group, [&](void* geometry) {
        int count = get_vertex_count(geometry);
        if (!count)
            return;
        // (the original runs an empty constructor over the array: 0x005902E0)
        auto* colours = static_cast<uint8_t*>(memory::alloc(static_cast<unsigned int>(count) * 4));
        get_colours(geometry, reinterpret_cast<uint32_t*>(colours));
        if (target != 0.0f || factor != 0.0f) {
            for (int i = 0; i < count; ++i) {
                uint8_t* colour = colours + 4 * i;
                // x87: in double, rounded only by the truncation
                for (int channel = 0; channel < 3; ++channel)
                    colour[channel] = channel_byte((static_cast<double>(colour[channel]) - target) * factor + target);
            }
        }
        set_colours(geometry, reinterpret_cast<uint32_t*>(colours));
        memory::dealloc(colours);
    });
}

// 0x00447980: CompressVC target=<n> percent=<n> [LightGroup=<name>]: every scene's (skies apart, then the first
// sky's) vertex colours compressed towards target, to (100 - percent)% of their distance. Always true.
bool __cdecl cfunc_compress_vc(Struct* params, Script*)
{
    float target = 0.0f;
    float percent = 0.0f;
    params->fetch_float(kTarget, &target, 1);
    params->fetch_float(kPercent, &percent, 0);
    auto factor = static_cast<float>((100.0 - percent) * static_cast<double>(0.01f)); // (x87: rounded once)
    uint32_t light_group = 0;
    params->read_hash_value(kLightGroup, &light_group, 0);
    for (SceneNode* node = first_scene_node(); node;) {
        SceneNode* next = next_scene_node(node);
        if (!is_sky_backdrop(node->scene))
            compress_scene_vertex_colours(node->scene, target, factor, light_group);
        node = next;
    }
    if (uint8_t* sky = first_sky_scene())
        compress_scene_vertex_colours(sky, target, factor, light_group);
    return true;
}

// 0x00447A60: the vertex colours of a scene's sectors scaled by a pseudo-random amount per vertex: a 15-bit hash
// of the position (in 1/16ths) h gives the scale 1 - amount + 2 * amount * h / 32768 (colours limited to 0..255,
// alpha kept).
void __cdecl nudge_scene_vertex_colours(uint8_t* scene, float amount)
{
    for_each_sector_geometry(scene, 0, [&](void* geometry) {
        int count = get_vertex_count(geometry);
        if (!count)
            return;
        // (the original runs an empty constructor over both arrays: 0x005902E0)
        auto* positions = static_cast<script::VectorValue*>(memory::alloc(static_cast<unsigned int>(count) * 16));
        auto* colours = static_cast<uint8_t*>(memory::alloc(static_cast<unsigned int>(count) * 4));
        get_positions(geometry, positions);
        get_colours(geometry, reinterpret_cast<uint32_t*>(colours));
        for (int i = 0; i < count; ++i) {
            const script::VectorValue& position = positions[i];
            uint8_t* colour = colours + 4 * i;
            auto hash = static_cast<uint32_t>(ftol(static_cast<double>(position.x) * 16.0)) *
                        static_cast<uint32_t>(ftol(static_cast<double>(position.y) * 16.0));
            hash = (hash ^ static_cast<uint32_t>(ftol(static_cast<double>(position.z) * 16.0))) & 0x7FFF;
            // x87: the scale rounded to a float; red and green products rounded to floats, blue's kept in double
            auto scale = static_cast<float>(static_cast<double>(hash) * (1.0 / 32768.0) * amount * 2.0 + 1.0 - amount);
            auto red = static_cast<float>(colour[0] * static_cast<double>(scale));
            auto green = static_cast<float>(colour[1] * static_cast<double>(scale));
            double blue = colour[2] * static_cast<double>(scale);
            colour[0] = channel_byte(red);
            colour[1] = channel_byte(green);
            colour[2] = channel_byte(blue);
        }
        set_colours(geometry, reinterpret_cast<uint32_t*>(colours));
        memory::dealloc(positions);
        memory::dealloc(colours);
    });
}

// 0x00447D90: NudgeVC percent=<n>: every scene's vertex colours (skies too) nudged by up to percent%. Always true.
bool __cdecl cfunc_nudge_vc(Struct* params, Script*)
{
    float percent = 0.0f;
    params->fetch_float(kPercent, &percent, 1);
    percent = percent * 0.01f;
    for (SceneNode* node = first_scene_node(); node;) {
        SceneNode* next = next_scene_node(node);
        nudge_scene_vertex_colours(node->scene, percent);
        node = next;
    }
    return true;
}

// 0x00447E00: SetLightDirection index=<n> direction=<vector> | heading=<degrees> pitch=<degrees>: the light's
// direction (0x0048ED70; w 0): the vector, or (0, 0, 1) turned by pitch about x and then by heading about y (both
// needed; otherwise (0, 0, 1)). Returns what 0x0048ED70 returns (its AL).
bool __cdecl cfunc_set_light_direction(Struct* params, Script*)
{
    constexpr float kRadiansPerDegree = 0.0174532924f; // (0x00645464)
    int index = 0;                                     // (uninitialised in the original)
    params->fetch_int_value(kIndex, &index, 0);
    script::VectorValue direction = {0.0f, 0.0f, 1.0f, 0.0f};
    float heading;
    float pitch;
    if (params->fetch_vec(kDirection, &direction, 0)) {
        direction.w = 0.0f;
    } else if (params->fetch_float(kHeading, &heading, 0) && params->fetch_float(kPitch, &pitch, 0)) {
        double p = static_cast<double>(pitch) * kRadiansPerDegree;
        float y = direction.y;
        float z = direction.z;
        direction.y = static_cast<float>(x87_cos_times(p, y) - x87_sin_times(p, z));
        double turned_z = x87_cos_times(p, z) + x87_sin_times(p, y);
        double h = static_cast<double>(heading) * kRadiansPerDegree;
        float s = x87_sin(h);
        float c = x87_cos(h);
        float x = direction.x;
        direction.z = static_cast<float>(-(static_cast<double>(s) * x) + turned_z * c);
        direction.x = static_cast<float>(turned_z * s + static_cast<double>(c) * x);
    }
    return render::set_light_direction(index, reinterpret_cast<const maths::Vec4*>(&direction));
}

// 0x00447F30: GetMetrics: the renderer's metrics (0x0048CB00 into a new structure) added to the script's params as
// Metrics. Always true.
bool __cdecl cfunc_get_metrics(Struct*, Script* script)
{
    Struct* metrics = script::spawn_param_bag();
    if (metrics)
        metrics = metrics->prepare_all();
    render::get_scene_debug_info(metrics);
    script->args_->put_sub_struct(kMetrics, metrics);
    if (metrics) {
        metrics->get_clear();
        script::delete_struct(metrics);
    }
    return true;
}

} // namespace commands
