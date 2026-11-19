// NOT ORIGINAL - (round CS) who puts each live setting to use (engine/config/settings_live.h): one applier for each
// owner the settings model names (engine/config/settings_model.h Owner), and the level reload. The console, the
// automation channel and the Options screen all change a setting through settings_live; it calls these.

#include "app/settings_appliers.h"

#include <cstring>
#include <string>

#include "devtools/compare_split.h"
#include "engine/audio/continue_music.h"
#include "engine/audio/muted_device.h"
#include "engine/config/settings_live.h"
#include "engine/config/settings_model.h"
#include "engine/config/settings_state.h"
#include "engine/ext/extension.h"
#include "engine/gfx/glyphs.h"
#include "engine/gfx/gpu_context.h"
#include "engine/gfx/graphics_options.h"
#include "engine/gfx/settings.h"
#include "engine/gfx/texture_sets.h"
#include "engine/pad_input/sdl_input.h"
#include "engine/sys/mlp.h"
#include "frontend/mouse_menus.h"
#include "frontend/new_frontend.h"
#include "game/front_end/level_reload.h"
#include "game/front_end/live_pause.h"
#include "platform/mute_audio.h"
#include "platform/run_log.h"
#include "platform/test_workspace.h"

namespace app {

namespace {

using config::Owner;
using config::Setting;

// The setting's effective value now, as a switch.
bool switched_on(const Setting& setting)
{
    const config::Resolved& now = config::running().at(setting.section, setting.key);
    return now.in_effect && config::truthy(now.value);
}

// The renderer's part (its effect), and the frame pacing: read again as the Options screen always did.
bool apply_renderer(const Setting& setting) { return render::graphics_options::reload_live(setting.key); }

// The display made again before the next frame: the back buffer, its samples and filtering, the window, the upscaler.
bool apply_display(const Setting& /*setting*/)
{
    if (!render::gpu_context().device_made()) {
        return false;
    }
    frame_loop::request_display_reset();
    return true;
}

// PartyMod's drawing settings and the distance scales: read and handed to the renderer again.
bool apply_view(const Setting& /*setting*/)
{
    render::load_graphics_settings();
    render::write_graphics_settings();
    return true;
}

bool apply_textures(const Setting& /*setting*/) { return render::texture_sets::request_from_ini(); }

bool apply_compare(const Setting& /*setting*/)
{
    devtools::compare_split::read_settings();
    return true;
}

bool apply_input(const Setting& /*setting*/)
{
    sio::reread_input_settings();
    return true;
}

bool apply_glyphs(const Setting& /*setting*/)
{
    render::load_glyph_settings();
    return true;
}

// (a workspace's game stays muted whatever its ini says: platform/test_workspace.h)
bool apply_audio(const Setting& setting)
{
    const bool muted = platform::test_workspace() || switched_on(setting);
    sound::audio_muted() = muted;
    audio::set_output_muted(muted);
    return true;
}

// A switch its owner keeps (they were the Options screen's own cases).
bool apply_switch(const Setting& setting)
{
    const bool on = switched_on(setting);
    if (std::strcmp(setting.key, "mouse_menus") == 0) {
        mouse_menus::is_switched_on() = on;
    } else if (std::strcmp(setting.key, "continue_music") == 0) {
        audio::continue_music::is_switched_on() = on;
    } else if (std::strcmp(setting.key, "live_pause") == 0) {
        modules::live_pause::is_switched_on() = on;
    } else if (std::strcmp(setting.key, "respect_locks") == 0) {
        frontend::new_frontend::refresh_level_views();
    } else if (bool* feature = ext::feature_switch(setting.key)) {
        *feature = on; // (the restart tester's)
    } else {
        get_log("settings: %s - no switch of that name here", setting.key);
        return false;
    }
    return true;
}

// (build_info::LiveInt: the owner reads it again at its next use)
bool apply_self(const Setting& /*setting*/) { return true; }

} // namespace

void install_setting_appliers()
{
    using config::live::set_applier;
    set_applier(Owner::kRenderer, &apply_renderer);
    set_applier(Owner::kPacing, &apply_renderer);
    set_applier(Owner::kDisplay, &apply_display);
    set_applier(Owner::kView, &apply_view);
    set_applier(Owner::kTextures, &apply_textures);
    set_applier(Owner::kCompare, &apply_compare);
    set_applier(Owner::kInput, &apply_input);
    set_applier(Owner::kGlyphs, &apply_glyphs);
    set_applier(Owner::kAudio, &apply_audio);
    set_applier(Owner::kSwitch, &apply_switch);
    set_applier(Owner::kSelf, &apply_self);
    config::live::set_level_reloader(&frontend::level_reload::request);
    const std::string missing = config::live::missing_appliers();
    if (!missing.empty()) {
        get_log("settings: live without an applier here (written only): %s", missing.c_str());
    }
}

} // namespace app
