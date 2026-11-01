// Script commands (CFunctions) 0x005A5360-0x005A595F: light groups, the skaters' pause, the viewer, the weather
// and the screen effects (see world_cfuncs.h).

#include "engine/script/world_cfuncs.h"

#include <cstdint>
#include <memory>

#include "engine/core/mem.h"
#include "engine/net/gamenet_manager.h"
#include "engine/objects/compositemgr.h"
#include "engine/objects/entity.h"
#include "engine/script/param_struct.h"
#include "game/rider/rider.h"
#include "game/skate/skate_module.h"
#include "engine/sys/game_module.h"
#include "engine/sys/model_viewer.h"
#include "engine/gfx/weather.h"
#include "engine/gfx/screen_effects.h"
#include "engine/script/game_cfuncs.h"
#include "engine/gfx/engine_glue.h"
#include "engine/sys/game_config.h"
#include "engine/core/down_cast.h"
#include <gsl/pointers>

namespace commands {

memory::HeapUnique<modules::Hub> g_module_manager;
namespace {
bool g_viewer_launched;

using script::Script;
using script::Struct;

constexpr uint32_t kViewerCam = 0xEB17151B; // "viewer_cam": the viewer camera object's name (its id)

// 0x005A4EF0 (no source yet): a command (7 light group, 5 alive...) applied to the node name= or the nodes
// prefix= names; true unless neither is given.
bool node_command(Struct* params, Script* script, int command)
{
    return commands::node_command_by_name(params, script, command);
}

// The skater's own pause and unpause (the original's vtable +0x2C / +0x30).
void pause_skater(objects::GameEntity* skater) { core::down_cast<objects::Rider>(skater)->get_freeze(); }
void unpause_skater(objects::GameEntity* skater) { core::down_cast<objects::Rider>(skater)->unpause(); }

// Whether LaunchViewer has made the viewer (0x007CEBF0).
bool& viewer_launched() { return g_viewer_launched; }

// The weather (0x006FA7C8) and the screen effects (0x006FA7CC).
render::WeatherSystem* get_weather() { return render::g_weather.get(); }
render::ScreenEffects* screen_effects() { return render::g_screen_effects.get(); }
} // namespace

// 0x005A5360
bool cfunc_set_light_group(Struct* params, Script* script) { return node_command(params, script, 7); }

// 0x005A5380
bool cfunc_is_alive(Struct* params, Script* script) { return node_command(params, script, 5); }

// 0x005A53A0
bool cfunc_pause_skaters(Struct* params, Script*)
{
    const bool hide = params->has_bare_flag("hide");
    modules::Skate* skate = modules::Skate::sole();
    auto count = static_cast<uint32_t>(skate->count_riders());
    for (uint32_t i = 0; i < count; ++i) {
        objects::GameEntity* skater = skate->get_rider_at(static_cast<int>(i));
        if (skater && skater->is_local_skater_) {
            pause_skater(skater);
            skater->conceal(hide);
        }
    }
    return true;
}

// 0x005A5420
bool cfunc_un_pause_skaters(Struct*, Script*)
{
    modules::Skate* skate = modules::Skate::sole();
    auto count = static_cast<uint32_t>(skate->count_riders());
    for (uint32_t i = 0; i < count; ++i) {
        objects::GameEntity* skater = skate->get_rider_at(static_cast<int>(i));
        if (skater) {
            unpause_skater(skater);
            skater->conceal(false);
        }
    }
    return true;
}

// 0x005A5480
bool cfunc_skaters_are_ready(Struct*, Script*)
{
    if (!online::g_online_hub->cleared_to_start_)
        return false;
    if (modules::Skate::sole()->count_riders() > 0)
        return true;
    // IsObserving (0x005A0D90, no source yet)
    return commands::cfunc_is_observing(nullptr, nullptr);
}

// 0x005A54E0
bool cfunc_launch_viewer(Struct*, Script*)
{
    if (viewer_launched())
        return true;
    modules::Hub* modules = g_module_manager.get();
    auto* viewer = new modules::Viewer;
    modules->enrol_unit(viewer);
    modules->launch_module(viewer);
    viewer_launched() = true;

    Struct* node_data = script::spawn_param_bag();
    if (node_data)
        node_data = new (node_data) script::Struct;
    node_data->put_name_key("name", kViewerCam);
    objects::GameEntity* camera = objects::object_hub_slot()->spawn_entity_from_node(
        script::fetch_list("viewercam_composite_structure"), node_data, true);
    if (node_data) {
        node_data->get_clear();
        script::delete_struct(node_data);
    }
    camera->object_flags_ |= 2;
    return true;
}

// 0x005A5610
bool cfunc_add_skater_early(Struct*, Script*)
{
    if (modules::Skate::sole()->count_riders() == 0) {
        modules::Skate* skate = modules::Skate::sole();
        // its profile 0 (0x00564FC0) as a new local skater (0x00567180), no id, heap 0
        objects::RiderIdentity* profile = skate->fetch_account(static_cast<int>(0));
        skate->spawn_skater_entity(profile, true, 0, 0);
    }
    return true;
}

// --- the weather ---

// 0x005A5680: its vtable +0x04 (through 0x004A1090).
bool cfunc_weather_update_grid(Struct*, Script*)
{
    get_weather()->tick_grid(); // (0x004A1090: a jump through the vtable +0x04)
    return true;
}
// 0x005A5690
bool cfunc_weather_set_rain_height(Struct* params, Script* script)
{
    return get_weather()->script_set_rain_height(params, script);
}
// 0x005A56B0
bool cfunc_weather_set_rain_frames(Struct* params, Script* script)
{
    return get_weather()->script_set_rain_frames(params, script);
}
// 0x005A56D0
bool cfunc_weather_set_rain_length(Struct* params, Script* script)
{
    return get_weather()->script_set_rain_length(params, script);
}
// 0x005A56F0
bool cfunc_weather_set_rain_blend_mode(Struct* params, Script* script)
{
    return get_weather()->script_set_rain_blend_mode(params, script);
}
// 0x005A5710
bool cfunc_weather_set_rain_rate(Struct* params, Script* script)
{
    return get_weather()->script_set_rain_rate(params, script);
}
// 0x005A5730
bool cfunc_weather_set_rain_color(Struct* params, Script* script)
{
    return get_weather()->script_set_rain_color(params, script);
}
// 0x005A5750
bool cfunc_weather_set_splash_rate(Struct* params, Script* script)
{
    return get_weather()->script_set_splash_rate(params, script);
}
// 0x005A5770
bool cfunc_weather_set_splash_life(Struct* params, Script* script)
{
    return get_weather()->script_set_splash_life(params, script);
}
// 0x005A5790
bool cfunc_weather_set_splash_size(Struct* params, Script* script)
{
    return get_weather()->script_set_splash_size(params, script);
}
// 0x005A57B0
bool cfunc_weather_set_splash_color(Struct* params, Script* script)
{
    return get_weather()->script_set_splash_color(params, script);
}
// 0x005A57D0
bool cfunc_weather_set_splash_blend_mode(Struct* params, Script* script)
{
    return get_weather()->script_set_splash_blend_mode(params, script);
}
// 0x005A57F0
bool cfunc_weather_set_snow_height(Struct* params, Script* script)
{
    return get_weather()->script_set_snow_height(params, script);
}
// 0x005A5810
bool cfunc_weather_set_snow_frames(Struct* params, Script* script)
{
    return get_weather()->script_set_snow_frames(params, script);
}
// 0x005A5830
bool cfunc_weather_set_snow_size(Struct* params, Script* script)
{
    return get_weather()->script_set_snow_size(params, script);
}
// 0x005A5850
bool cfunc_weather_set_snow_blend_mode(Struct* params, Script* script)
{
    return get_weather()->script_set_snow_blend_mode(params, script);
}
// 0x005A5870
bool cfunc_weather_set_snow_rate(Struct* params, Script* script)
{
    return get_weather()->script_set_snow_rate(params, script);
}
// 0x005A5890
bool cfunc_weather_set_snow_color(Struct* params, Script* script)
{
    return get_weather()->script_set_snow_color(params, script);
}
// 0x005A58B0
bool cfunc_weather_set_snow_active(Struct* params, Script* script)
{
    return get_weather()->script_set_snow_active(params, script);
}
// 0x005A58D0
bool cfunc_weather_set_rain_active(Struct* params, Script* script)
{
    return get_weather()->script_set_rain_active(params, script);
}

// --- the screen effects ---

// 0x005A58F0
bool cfunc_set_screen_effects_on(Struct* params, Script* script) { return screen_effects()->set_on(params, script); }
// 0x005A5910
bool cfunc_set_screen_effects_off(Struct* params, Script* script) { return screen_effects()->set_off(params, script); }
// 0x005A5930
bool cfunc_set_screen_effects_dof_params(Struct* params, Script* script)
{
    return screen_effects()->set_dof_params(params, script);
}

// 0x005A5950
bool cfunc_is_pal(Struct*, Script*) { return game_config::g_language != 2; }

} // namespace commands
