// Script commands of the engine, 0x00445B50-0x00446450: the lens flare, terrain sounds, the sun, gamma, fog,
// dynamic lighting, UV wibble and texture animation of world sectors (see cfuncs_engine.h).

#include "engine/script/cfuncs_engine.h"

#include <array>
#include <windows.h>

#include <cstdint>
#include "engine/gfx/lens_flare.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/surface_kind.h"
#include "engine/gfx/engine.h"
#include "engine/gfx/engine_glue.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/light_manager.h"
#include "engine/gfx/sector.h"
#include "engine/sys/game_config.h"
#include "platform/registry.h"
#include "engine/gfx/view_setup.h"
#include "engine/core/checked_index.h"

namespace commands {

using script::Script;
using script::Struct;

namespace {

constexpr uint32_t kParam = 0x5B058376;         // "param"
constexpr uint32_t kTerrain = 0x3789AC4E;       // "terrain"
constexpr uint32_t kTerrainIndex = 0x75D274BE;  // (name unknown) the terrain as a number
constexpr uint32_t kTheta = 0xF4E775F6;         // "theta"
constexpr uint32_t kPhi = 0xCD0576EE;           // "phi"
constexpr uint32_t kRadius = 0xC48391A5;        // "radius"
constexpr uint32_t kSize = 0x083FDB95;          // "size"
constexpr uint32_t kTexture = 0x7D99F28D;       // "texture"
constexpr uint32_t kCoronaTexture = 0xEB1CCC76; // "corona_texture"
constexpr uint32_t kEnable = 0x0C5F37CD;        // "enable"
constexpr uint32_t kDisable = 0xB0BAB2AE;       // "disable"
constexpr uint32_t kOn = 0xF649D637;            // "on"
constexpr uint32_t kClear = 0x1A4E0EF9;         // "clear"
constexpr uint32_t kRed = 0x059EA070;           // "red"
constexpr uint32_t kGreen = 0x2F6511DE;         // "green"
constexpr uint32_t kBlue = 0x61C9354B;          // "blue"
constexpr uint32_t kValue = 0xE288A7CB;         // "value"
constexpr uint32_t kValue2 = 0xC483C21F;        // "value2"
constexpr uint32_t kAmbient = 0xBCEE1D37;       // "ambient"
constexpr uint32_t kDirectional = 0xB8F8BBE6;   // "directional"
constexpr uint32_t kSector = 0xB45C2617;        // "sector"
constexpr uint32_t kSeq = 0x69812EAC;           // "seq"
constexpr uint32_t kKey = 0x756F5456;           // "key"
constexpr uint32_t kTime = 0x906B67BA;          // "time"
constexpr uint32_t kReset = 0xAF6240B2;         // "reset"

// Fog and clipping distances are left alone while distance clipping (the setting) is on.

// The world sector named `name` (null if the scene has none of that name).
render::WorldCell* look_up_sector(uint32_t name)
{
    return static_cast<render::WorldCell*>(render::engine::look_up_sector(name));
}

// The renderer's gamma (0x004B24D0 / 0x004B24C0 through the jumps 0x0048C140 / 0x0048C130).
std::array<float, 3> get_gamma() { return render::get_gamma(); }
void set_gamma(float red, float green, float blue) { render::set_gamma(red, green, blue); }

// Whether the frame buffer is cleared each frame - compiled out of this build (0x0048C1B0, a jump to the empty
// function 0x005480A0).
inline void set_color_buffer_clear(bool /*clear*/) {}

// The sun's settings as SetSunProps passes them (0x004426D0 copies the fields its mask names).

} // namespace

// 0x00445B50: HideLensFlare (0x00442DF0 with the same arguments). Always true.
bool cfunc_hide_lens_flare(Struct* params, Script* script)
{
    sun::hide_flare(params, script);
    return true;
}

// 0x00445B70: UnhideLensFlare (0x00443030). Always true.
bool cfunc_unhide_lens_flare(Struct* params, Script* script)
{
    sun::unhide_flare(params, script);
    return true;
}

// 0x00445B90: GetTerrainTypeParam param=<name> terrain=<checksum> (or the terrain's number): the terrain's
// structure for `param` (0x00444480) added to the script's params as `param`. False if there is none.
bool cfunc_get_terrain_type_param(Struct* params, Script* script)
{
    uint32_t param = 0;
    uint32_t terrain_name = 0;
    int terrain_index = 0;
    params->read_hash_value(kParam, &param, 1);
    uint8_t terrain = 0;
    if (params->read_hash_value(kTerrain, &terrain_name, 0)) {
        terrain = surface_kind::get_terrain_index(terrain_name);
    } else {
        params->fetch_int_value(kTerrainIndex, &terrain_index, 1);
        terrain = static_cast<uint8_t>(terrain_index);
    }
    Struct* value = nullptr; // (uninitialised in the original: the callee sets it)
    if (!surface_kind::get_terrain_structure_into(&value, terrain, param))
        return false;
    script->args()->put_sub_struct(param, value);
    return true;
}

// 0x00445C50: SetSunProps [theta] [phi] [radius] [size] [texture] [corona_texture] [enable | disable]: the
// settings given are passed on (0x004426D0 with the mask of those given). Always true.
bool cfunc_set_sun_props(Struct* params, Script*)
{
    sun::Settings props{}; // (the fields not given are uninitialised in the original; the callee doesn't read them)
    uint32_t mask = 0;
    if (params->fetch_float(kTheta, &props.theta, 0))
        mask = 0x01;
    if (params->fetch_float(kPhi, &props.phi, 0))
        mask |= 0x02;
    if (params->fetch_float(kRadius, &props.radius, 0))
        mask |= 0x04;
    if (params->fetch_float(kSize, &props.size, 0))
        mask |= 0x08;
    if (params->read_hash_value(kTexture, &props.texture, 0))
        mask |= 0x20;
    if (params->read_hash_value(kCoronaTexture, &props.corona_texture_, 0))
        mask |= 0x40;
    if (params->has_bare_flag(kEnable)) {
        props.shown = true;
        mask |= 0x80;
    } else if (params->has_bare_flag(kDisable)) {
        props.shown = false;
        mask |= 0x80;
    }
    sun::set_sun(&props, mask);
    return true;
}

// 0x00445D50: SetProgressive on=<n>: progressive scan (0x005B6090) on for n > 0. Always true.
bool cfunc_set_progressive(Struct* params, Script*)
{
    int on = 0;
    params->fetch_int_value(kOn, &on, 0);
    game_config::set_ntsc_flag(on > 0);
    return true;
}

// 0x00445D90: SetColorBufferClear clear=<n> (does nothing in this build). Always true.
bool cfunc_set_color_buffer_clear(Struct* params, Script*)
{
    int clear = 0;
    params->fetch_int_value(kClear, &clear, 0);
    set_color_buffer_clear(clear > 0);
    return true;
}

// 0x00445DD0
void write_gamma(Gamma gamma)
{
    HKEY key = nullptr;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, R"(Software\Activision\Tony Hawk's Underground 2\Settings\Gamma)", 0,
                        nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    sys::set_registry_value(key, "R", REG_DWORD, &gamma.red, 4);
    sys::set_registry_value(key, "G", REG_DWORD, &gamma.green, 4);
    sys::set_registry_value(key, "B", REG_DWORD, &gamma.blue, 4);
    RegCloseKey(key);
}

// 0x00445E60: ApplyChangeGamma color=red|green|blue change=<amount>: the channel's gamma changed by `amount`
// while it stays in range (raised only below 1, lowered only above 0), applied and saved - twice, as the original
// does. Always true.
bool cfunc_apply_change_gamma(Struct* params, Script*)
{
    const std::array<float, 3> gamma = get_gamma();
    float red = core::at(gamma, 0);
    float green = core::at(gamma, 1);
    float blue = core::at(gamma, 2);
    uint32_t color = 0; // (uninitialised in the original: both are asserted)
    float change = 0.0f;
    params->read_hash_value("color", &color, 1);
    params->fetch_float("change", &change, 1);
    float* channel = nullptr;
    if (color == kRed)
        channel = &red;
    else if (color == kGreen)
        channel = &green;
    else if (color == kBlue)
        channel = &blue;
    if (channel && ((change > 0.0f && *channel < 1.0f) || (change < 0.0f && *channel > 0.0f)))
        *channel = change + *channel;
    set_gamma(red, green, blue);
    write_gamma({.red = red, .green = green, .blue = blue});
    write_gamma({.red = red, .green = green, .blue = blue});
    return true;
}

// 0x00446000: SetDynamicLightModulationFactor value=<f> [value2=<f>] ambient | directional=<index>: the ambient
// modulation factors (0x0048E970) or a directional light's (0x0048E990). Nothing without `value`. Always true.
bool cfunc_set_dynamic_light_modulation_factor(Struct* params, Script*)
{
    float value = 0.0f;
    float value2 = 0.0f;
    int directional = 0;
    if (!params->fetch_float(kValue, &value, 0))
        return true;
    params->fetch_float(kValue2, &value2, 0);
    if (params->has_bare_flag(kAmbient)) {
        render::change_ambient_scale(value, value2);
        return true;
    }
    params->fetch_int_value(kDirectional, &directional, 0);
    render::change_diffuse_scale(directional, value, value2);
    return true;
}

// 0x004460B0: EnableFog (0x00490DE0), unless distance clipping is on. Always true.
bool cfunc_enable_fog(Struct*, Script*)
{
    if (!render::view_setup().distance_clipping())
        render::engine::enable_fog(true);
    return true;
}

// 0x004460D0: DisableFog, unless distance clipping is on. Always true.
bool cfunc_disable_fog(Struct*, Script*)
{
    if (!render::view_setup().distance_clipping())
        render::engine::enable_fog(false);
    return true;
}

// 0x004460F0: SetFogExponent exponent=<f> (0x00490E10). False (and nothing) while distance clipping is on.
bool cfunc_set_fog_exponent(Struct* params, Script*)
{
    if (render::view_setup().distance_clipping())
        return false;
    float exponent = 0.0f; // (uninitialised in the original)
    params->fetch_float("exponent", &exponent, 0);
    render::engine::set_fog_exponent(exponent);
    return true;
}

// 0x00446130: VCWibblePlayFromKeyframe sector=<name> [seq] [key] [time=seconds]: the sector's vertex colour
// wibble played from a keyframe (0x0049E820, time in ms). False without a sector name; true otherwise (also if
// the scene has no such sector).
bool cfunc_vc_wibble_play_from_keyframe(Struct* params, Script*)
{
    uint32_t name = 0;
    if (!params->read_hash_value(kSector, &name, 0))
        return false;
    render::WorldCell* sector = look_up_sector(name);
    if (!sector)
        return true;
    int seq = 0; // (seq and key are uninitialised in the original)
    int key = 0;
    int time = 0;
    params->fetch_int_value(kSeq, &seq, 0);
    params->fetch_int_value(kKey, &key, 0);
    params->fetch_int_value(kTime, &time, 0);
    sector->play_vc_wibble_from_keyframe(seq, key, time * 1000);
    return true;
}

// 0x004461D0: SetUVWibbleParams sector=<name> u_vel u_amp u_freq u_phase v_vel v_amp v_freq v_phase: the
// sector's UV wibble (0x0049E830 - on a missing sector too, as the original). Always true.
bool cfunc_set_uv_wibble_params(Struct* params, Script*)
{
    uint32_t name = 0; // (these are uninitialised in the original)
    params->read_hash_value("sector", &name, 0);
    render::WorldCell* sector = look_up_sector(name);
    float u_vel = 0.0f, u_amp = 0.0f, u_freq = 0.0f, u_phase = 0.0f;
    float v_vel = 0.0f, v_amp = 0.0f, v_freq = 0.0f, v_phase = 0.0f;
    params->fetch_float("u_vel", &u_vel, 0);
    params->fetch_float("u_amp", &u_amp, 0);
    params->fetch_float("u_freq", &u_freq, 0);
    params->fetch_float("u_phase", &u_phase, 0);
    params->fetch_float("v_vel", &v_vel, 0);
    params->fetch_float("v_amp", &v_amp, 0);
    params->fetch_float("v_freq", &v_freq, 0);
    params->fetch_float("v_phase", &v_phase, 0);
    sector->set_uv_wibble_params({.u_vel_ = u_vel, .u_amp_ = u_amp, .u_freq_ = u_freq, .u_phase_ = u_phase, .v_vel_ = v_vel, .v_amp_ = v_amp, .v_freq_ = v_freq, .v_phase_ = v_phase});
    return true;
}

namespace {
// The sector named by the params' "sector" (uninitialised in the original when missing).
render::WorldCell* named_sector(Struct* params)
{
    uint32_t name = 0;
    params->read_hash_value("sector", &name, 0);
    return look_up_sector(name);
}
} // namespace

// 0x004462D0: EnableExplicitUVWibble sector=<name> (0x0049E840). Always true.
bool cfunc_enable_explicit_uv_wibble(Struct* params, Script*)
{
    named_sector(params)->force_uv_wobble(true);
    return true;
}

// 0x00446300: DisableExplicitUVWibble sector=<name>. Always true.
bool cfunc_disable_explicit_uv_wibble(Struct* params, Script*)
{
    named_sector(params)->force_uv_wobble(false);
    return true;
}

// 0x00446330: SetUVWibbleOffsets sector=<name> u_off v_off (0x0049E850). Always true.
bool cfunc_set_uv_wibble_offsets(Struct* params, Script*)
{
    render::WorldCell* sector = named_sector(params);
    float u_off = 0.0f; // (uninitialised in the original)
    float v_off = 0.0f;
    params->fetch_float("u_off", &u_off, 0);
    params->fetch_float("v_off", &v_off, 0);
    sector->set_uv_wibble_offsets({.x = u_off, .y = v_off});
    return true;
}

// 0x004463A0: ResetTextureAnim sector=<name> (0x0049E860). Always true.
bool cfunc_reset_texture_anim(Struct* params, Script*)
{
    named_sector(params)->reset_texture_anim();
    return true;
}

// 0x004463D0: StartTextureAnim sector=<name> [reset] (0x0049E870). Always true.
bool cfunc_start_texture_anim(Struct* params, Script*)
{
    render::WorldCell* sector = named_sector(params);
    const bool reset = params->has_bare_flag(kReset);
    sector->start_texture_anim(reset);
    return true;
}

// 0x00446420: StopTextureAnim sector=<name> (0x0049E880). Always true.
bool cfunc_stop_texture_anim(Struct* params, Script*)
{
    named_sector(params)->stop_texture_anim();
    return true;
}

// 0x00446450: ToggleRenderMode: the next render mode (0x0048C0D0: 0..3 in turn). Always true.
bool cfunc_toggle_render_mode(Struct*, Script*)
{
    render::engine::toggle_render_mode();
    return true;
}

} // namespace commands
