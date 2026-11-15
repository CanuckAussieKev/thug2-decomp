// Lighting on PC: see pc_lights.h. Colours follow the original's x87 code: a byte times float factors, kept in
// double precision, rounded once when stored.

#include "render_pc/pc_lights.h"

#include <cstddef>
#include <cmath>
#include <span>
#include <algorithm>
#include <utility>
#include <cstdint>
#include "engine/memory/manager.h"
#include "engine/memory/mem.h"
#include "engine/gfx/engine_model_lights.h"
#include "engine/gfx/light_manager.h"
#include <gsl/pointers>
#include <array>
#include "base/core/checked_index.h"
#include "base/core/packed_colour.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/light_sets.h"
#include "engine/gfx/rt_light_field.h"
#include "engine/gfx/rt_lights.h"
#include "render_pc/rt_relit_world.h"

namespace render {

constexpr int kLightGroupStride = 8;       // each light's constants are this many floats apart ...
constexpr int kLightDirectionOffset = 8;   // ... the direction after the first 8 floats of the shared block
constexpr int kBitsPerByte = 8;
constexpr int kByteMask = 0xFF;
constexpr int kVec4Bytes = 16;
constexpr int kShiftCountMask = 31;
constexpr std::size_t kModelLightSlots = 32;
constexpr int kLightLoopLimit = 31;

namespace {
// NOT ORIGINAL grouping - pc_lights.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_lights_state()).
struct PcLightsState {
    maths::Vec4 scene_light_direction_{};
    uint32_t scene_light_direction_guard_{};
    LocalLightSent local_light_{}; // NOT ORIGINAL - the one lighting path's local light (pc_lights.h)
};

PcLightsState& pc_lights_state()
{
    static core::NoDestroy<PcLightsState> state;
    return state.get();
}

} // namespace

namespace {

constexpr double kPer128 = 1.0 / 128.0;  // 0x0064A2FC
constexpr float kPer255 = 1.0F / 255.0F; // 0x00645D70 (0x3B808081)

// The renderer's light constants.
std::array<float, 4 + (kLightConstantGroups * (2 + kLightConstantGroupTail))>& ambient_constant()
{
    return light_sets().light_constants();
}
std::span<float, 3> light_direction_constant(uint32_t light)
{
    return std::span<float>(light_sets().light_constants()).subspan(4 + (kLightGroupStride * light)).first<3>();
}
std::span<float, 3> light_color_constant(uint32_t light)
{
    return std::span<float>(light_sets().light_constants())
        .subspan(kLightDirectionOffset + (kLightGroupStride * light))
        .first<3>();
}

// The scene's lights.
uint32_t& scene_ambient() { return light_sets().world_lighting().ambient; }

std::span<maths::Vec4> scene_directions() { return light_sets().world_lighting().directions; } // (0x0067DC9C)
std::span<uint32_t> scene_colors() { return light_sets().world_lighting().colours; }           // (0x0067DCA0)

// A colour's r, g, b bytes times `factor` and 1/128 into three floats.
void send_color(std::span<float, 3> out, uint32_t rgba, float factor)
{
    for (int k = 0; k < 3; ++k) {
        core::at(out, k) = static_cast<float>(static_cast<double>((rgba >> (kBitsPerByte * k)) & kByteMask) * factor * kPer128);
    }
}
void send_negated(std::span<float, 3> out, const maths::Vec4& v)
{
    core::at(out, 0) = -v.x;
    core::at(out, 1) = -v.y;
    core::at(out, 2) = -v.z;
}

// NOT ORIGINAL - a model's colours and directions as runs of the model light count they were made for.
std::span<uint32_t> colors_of(const PcModelLights& lights) { return {lights.colors(), light_sets().model_light_count()}; }
std::span<maths::Vec4> directions_of(const PcModelLights& lights) { return {lights.directions(), light_sets().model_light_count()}; }

// A dynamic light's falloff at `distance` (its vtable's slot 2).
double get_falloff(DynamicLightBase* light, float distance) { return light->get_falloff(distance); }

// The dynamic lights within `radius` of `position` (the engine's 0x0048EF90): their count, pointers into `out`.
uint32_t find_dynamic_lights(const maths::Vec4* position, uint32_t radius, std::span<DynamicLightBase*> out)
{
    return get_dynamic_lights_near(position, radius, out);
}

// NOT ORIGINAL - the one lighting path, for the game's fill and ambient (the offline relit data: the light energy
// audit's characters' item): at the bake they stand for the level's indirect light and its own lamps' where a thing is,
// so now they are the game's own times (indirect now + lamps) over (indirect at the bake + lamps) - the bake's own time
// gives the game's own light, exactly the original's (before, the lamps were added on top: the skater 1.3x the bake's
// beside a lamp-lit door). The lamps' directional part (the field's local light toward where most of it comes from)
// shows as the daylight it was under fades: by the share of the direct and indirect light gone since the bake, it moves
// out of the ambient into a light of its own toward the lamps (local_light_sent) - a face toward the lamp is lit as the
// wall facing it at night, the lamp's light the same in all. While the lamps pass lights (rt_lights) it owns the lamps:
// the field's give way by as much (each light counted once).
struct GameShare {
    std::array<float, 3> ambient{1.0F, 1.0F, 1.0F};
    std::array<float, 3> directional{}; // (a share of the game's ambient)
    std::array<float, 3> toward{0.0F, 1.0F, 0.0F};
};
GameShare game_share(const maths::Vec4& position)
{
    GameShare out;
    const rt::FieldPoint at{position.x, position.y, position.z};
    const rt::IndirectLight indirect = rt::indirect_light_at(at);
    const rt::LocalLight lamps = rt::local_light_at(at);
    const float kept = 1.0F - rt::lamps_drawn();
    out.toward = lamps.toward;
    for (std::size_t channel = 0; channel < out.ambient.size(); ++channel) {
        const float around = core::at(lamps.ambient, channel);
        const float facing = core::at(lamps.directional, channel);
        const float bake = core::at(indirect.own, channel) + around + facing;
        if (!(bake > rt::kLightFieldLeast)) {
            continue; // (no field here: the game's own)
        }
        // (the share of the daylight still there: 1 at the bake's own time)
        const float day_own = core::at(indirect.own, channel) + core::at(indirect.direct_own, channel);
        const float day_now = core::at(indirect.now, channel) + core::at(indirect.direct_now, channel);
        const float day = day_own > rt::kLightFieldLeast ? std::clamp(day_now / day_own, 0.0F, 1.0F) : 1.0F;
        core::at(out.ambient, channel) = std::clamp(
            (core::at(indirect.now, channel) + (kept * (around + (facing * day)))) / bake, 0.0F, rt::kLightFieldMostRatio);
        core::at(out.directional, channel) =
            std::clamp(kept * facing * (1.0F - day) / bake, 0.0F, rt::kLightFieldMostRatio);
    }
    return out;
}

// NOT ORIGINAL - the relit mode and the one lighting path (rt_relit_world.h object_lighting, engine/gfx/rt_light_field.h):
// the game's light `light` as sent, turned toward the relit sun, the key light (0) times the direct light now over the
// bake's, the others - the fill - times the game's share where the thing is (`position`: game_share).
void relight(uint32_t light, const maths::Vec4* position)
{
    rt_relit_world::ObjectLighting relit;
    const std::span<float, 3> colour = light_color_constant(light);
    if (rt_relit_world::object_lighting(&relit)) {
        const std::span<float, 3> direction = light_direction_constant(light);
        const std::array<float, 3> was{core::at(direction, 0), core::at(direction, 1), core::at(direction, 2)};
        for (std::size_t axis = 0; axis < was.size(); ++axis) {
            const std::array<float, 3>& row = core::at(relit.rotation, axis);
            core::at(direction, axis) = (core::at(row, 0) * core::at(was, 0)) + (core::at(row, 1) * core::at(was, 1)) +
                                        (core::at(row, 2) * core::at(was, 2));
        }
        if (light == 0) {
            for (std::size_t channel = 0; channel < relit.key_colour.size(); ++channel) {
                core::at(colour, channel) *= core::at(relit.key_colour, channel);
            }
        }
    }
    if (light != 0 && position != nullptr) {
        const GameShare share = game_share(*position);
        for (std::size_t channel = 0; channel < share.ambient.size(); ++channel) {
            core::at(colour, channel) *= core::at(share.ambient, channel);
        }
    }
}

// NOT ORIGINAL - the one lighting path: the ambient light sent times the game's share where the thing is (game_share),
// and the lamps' directional part sent as the local light (pc_lights.h); the ambient held to 1 (the device's ambient is
// a colour of bytes).
void relight_ambient(const maths::Vec4* position)
{
    LocalLightSent& local = pc_lights_state().local_light_;
    local.on = false;
    if (position == nullptr) {
        return;
    }
    const GameShare share = game_share(*position);
    const std::span<float> ambient(ambient_constant());
    for (std::size_t channel = 0; channel < share.ambient.size(); ++channel) {
        const float game = core::at(ambient, channel);
        core::at(local.colour, channel) = game * core::at(share.directional, channel);
        core::at(ambient, channel) = std::min(game * core::at(share.ambient, channel), 1.0F);
    }
    local.on = std::ranges::any_of(local.colour, [](float value) { return value > 0.0F; });
    for (std::size_t axis = 0; axis < local.direction.size(); ++axis) {
        core::at(local.direction, axis) = -core::at(share.toward, axis);
    }
}

} // namespace

const LocalLightSent& local_light_sent() { return pc_lights_state().local_light_; }

namespace {

} // namespace

// 0x004B6270
PcModelLights::PcModelLights()
    : colors_(static_cast<uint32_t*>(memory::alloc(4 * light_sets().model_light_count()))),
      directions_(static_cast<maths::Vec4*>(memory::alloc(kVec4Bytes * light_sets().model_light_count())))
{
    switch_ambient_light(false);
    for (uint32_t i = 0; i < light_sets().model_light_count(); ++i) {
        switch_diffuse_light(static_cast<int>(i), false);
        core::at(directions_of(*this), i) = {.x = 0.0F, .y = 1.0F, .z = 0.0F, .w = 1.0F};
    }
    first_dynamic_ = 2;
}

// 0x004B6360
PcModelLights::~PcModelLights()
{
    if (colors_) {
        memory::Hub::dealloc(colors_);
        colors_ = nullptr;
    }
    if (directions_) {
        memory::Hub::dealloc(directions_);
        directions_ = nullptr;
    }
}

// 0x004B6160
bool PcModelLights::put_ambient_light_tint(const uint32_t* color)
{
    ambient = *color;
    return true;
}

// 0x004B6170
uint32_t* PcModelLights::read_ambient_light_tint(uint32_t* out)
{
    *out = ambient;
    return out;
}

// 0x004B6780
bool PcModelLights::put_light_aim(int light, const maths::Vec4* direction)
{
    core::at(directions_of(*this), light) = *direction;
    return true;
}

// 0x004B6180
const maths::Vec4* PcModelLights::read_light_aim(int light)
{
    if (diffuse_light_is_on(light)) {
        return &core::at(directions_of(*this), light);
    }
    return render::get_light_direction(light);
}

// 0x004B61B0
bool PcModelLights::put_diffuse_light_tint(int light, const uint32_t* color)
{
    core::at(colors_of(*this), light) = *color;
    return true;
}

// 0x004B61D0
uint32_t* PcModelLights::read_diffuse_light_tint(uint32_t* out, int light)
{
    *out = core::at(colors_of(*this), light);
    return out;
}

// 0x004B63B0
void PcModelLights::plat_apply_scene_lights(const maths::Vec4* position, uint32_t radius)
{
    pc_lights_state().local_light_.on = false; // (not original: the local light - relight_ambient)
    uint32_t color = 0;
    if ((overrides & 1) != 0U) {
        color = ambient;
    } else {
        fetch_ambient_hue(&color);
    }
    send_color(std::span<float>(ambient_constant()).first<3>(), color, size_factor());
    for (uint32_t i = 0; i < light_sets().light_count(); ++i) {
        if ((overrides & (2U << (i & kShiftCountMask))) != 0U) {
            send_color(light_color_constant(i), core::at(colors_of(*this), i), core::at(intensities(), i));
            send_negated(light_direction_constant(i), core::at(directions_of(*this), i));
        } else {
            fetch_light_tint(&color, static_cast<int>(i));
            send_color(light_color_constant(i), color, core::at(intensities(), i));
            send_negated(light_direction_constant(i), *render::get_light_direction(static_cast<int>(i)));
            relight(i, position); // (not original: the relit mode's sun, the one lighting path - rt_relit_world.h)
        }
    }
    if ((overrides & 1) == 0U) {
        relight_ambient(position); // (not original: the one lighting path)
    }
    if (radius == 0U) {
        return;
    }
    std::array<DynamicLightBase*, kModelLightSlots> dynamic{};
    const uint32_t count = find_dynamic_lights(position, radius, dynamic);
    if (count == 0U) {
        const std::span<float, 3> off = light_color_constant(first_dynamic_);
        core::at(off, 0) = core::at(off, 1) = core::at(off, 2) = 0.0F;
        return;
    }
    for (uint32_t j = 0; j < count; ++j) {
        DynamicLightBase* light = core::at(dynamic, j);
        const auto& at = light->pos();
        double dx = static_cast<double>(position->x) - core::at(at, 0);
        double dy = static_cast<double>(position->y) - core::at(at, 1);
        const double dz = static_cast<double>(position->z) - core::at(at, 2);
        auto distance = static_cast<float>(std::sqrt((dz * dz) + (dy * dy) + (dx * dx)));
        const double falloff = get_falloff(light, distance);
        // the direction from the light, normalised (unless its length isn't above 0)
        dx = static_cast<double>(position->x) - core::at(at, 0);
        dy = static_cast<double>(position->y) - core::at(at, 1);
        auto zf = static_cast<float>(static_cast<double>(position->z) - core::at(at, 2));
        const double length = std::sqrt((dx * dx) + (static_cast<double>(zf) * zf) + (dy * dy));
        if (length > 0.0) {
            const double inverse = 1.0 / length;
            dx *= inverse;
            dy *= inverse;
            zf = static_cast<float>(zf * inverse);
        }
        const std::span<float, 3> direction = light_direction_constant(first_dynamic_ + j);
        core::at(direction, 0) = static_cast<float>(dx);
        core::at(direction, 1) = static_cast<float>(dy);
        core::at(direction, 2) = zf;
        if (light->type() == 1) {
            const double factor = (1.0 - falloff) * light->intensity() * kPer255;
            const std::span<float, 3> out = light_color_constant(first_dynamic_ + j);
            const colour::Bytes colour = colour::bytes_of(light->colour()); // (its r, g, b bytes)
            for (int k = 0; k < 3; ++k) {
                core::at(out, k) = static_cast<float>(core::at(colour, k) * factor);
            }
        }
    }
}

// 0x004B61F0
void PcModelLights::switch_ambient_light(bool on)
{
    if (on) {
        overrides |= 1;
    } else {
        overrides &= ~1U;
    }
}

// 0x004B6240
bool PcModelLights::ambient_light_is_on() { return (overrides & 1) != 0; }

// 0x004B6210
void PcModelLights::switch_diffuse_light(int light, bool on)
{
    const uint32_t bit = 2U << (light & 31);
    if (on) {
        overrides |= bit;
    } else {
        overrides &= ~bit;
    }
}

// 0x004B6250
bool PcModelLights::diffuse_light_is_on(int light) { return (overrides & (2U << (light & kShiftCountMask))) != 0; }

// 0x004B67E0
bool update_ambient_light()
{
    send_color(std::span<float>(ambient_constant()).first<3>(), scene_ambient(), light_sets().ambient_intensity());
    return true;
}

// 0x004B6850
uint32_t* fetch_ambient_hue(uint32_t* out)
{
    *out = scene_ambient();
    return out;
}

// 0x004B6860
bool update_light_color(int light)
{
    send_color(light_color_constant(light), core::at(scene_colors(), light), core::at(light_sets().intensities(), light));
    return true;
}

// 0x004B68E0
uint32_t* fetch_light_tint(uint32_t* out, int light)
{
    *out = core::at(scene_colors(), light);
    return out;
}

// 0x004B6900
void update_all_lights()
{
    update_ambient_light();
    // (lights 0-30 in the original, past the arrays' end for any world with fewer: the constants of lights nobody
    // has are never read, so only the world's own are sent)
    for (int i = 0; std::cmp_less(i, kLightLoopLimit) && std::cmp_less(i, light_sets().light_count()); ++i) {
        update_light_color(i);
    }
}

// 0x004B6920
bool delete_model_lights(gsl::owner<PcModelLights*> lights)
{
    delete lights;
    return true;
}

// 0x004B6940
void reset_lights()
{
    pc_lights_state().local_light_.on = false; // (not original: no local light - pc_lights.h)
    send_color(std::span<float>(ambient_constant()).first<3>(), scene_ambient(), light_sets().ambient_intensity());
    for (int i = 0; i < 2; ++i) {
        uint32_t color = 0;
        fetch_light_tint(&color, i);
        send_color(light_color_constant(i), color, core::at(light_sets().intensities(), i));
        send_negated(light_direction_constant(i), *get_light_direction(i));
    }
    const std::span<float, 3> third = light_color_constant(2);
    core::at(third, 0) = core::at(third, 1) = core::at(third, 2) = 0.0F;
}

// 0x004B6A70
bool update_light_direction(int light)
{
    send_negated(light_direction_constant(light), core::at(scene_directions(), light));
    return true;
}

// 0x004B6AB0
maths::Vec4* get_light_direction(int light)
{
    pc_lights_state().scene_light_direction_guard_ |= 1; // (the static's initialisation guard)
    maths::Vec4& out = pc_lights_state().scene_light_direction_;
    const maths::Vec4& direction = core::at(scene_directions(), light);
    out = {.x = direction.x, .y = direction.y, .z = direction.z, .w = 1.0F};
    return &out;
}

// 0x004B6B10
gsl::owner<PcModelLights*> spawn_model_lights() { return new PcModelLights; }

} // namespace render
