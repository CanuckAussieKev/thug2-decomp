// Drawing a frame of a movie (the Bink player's texture) as a screen quad - fitted to 4:3 on PC (the fit
// contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).

#include "engine/gfx/movie.h"
#include "engine/gfx/render_constants.h"

#include <windows.h>

#include <d3d9.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <array>
#include <span>
#include <string>
#include <string_view>

#include "base/core/dbg.h"
#include "gpu/d3d9_handles.h"
#include "gpu/gpu_handles.h"
#include "base/core/checksums.h"
#include "engine/ext/extension.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/render_state.h"
#include "engine/audio/music_player.h"
#include "engine/audio/music_api.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/pad_input/device.h"
#include "engine/sys/mlp.h"
#include "platform/sdl_events.h"
#include "platform/mute_audio.h"
#include "platform/run_log.h"
#include "platform/process_flags.h"
#include "platform/bink.h"
#include "base/core/checked_index.h"
#include "base/core/format.h"
#include "base/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/d3d9_state.h"
#include "engine/gfx/engine_parts.h"
#include "base/maths/rect.h"
#include "base/maths/vec2.h"

namespace render {

constexpr float kLayoutWidth = 640.0F;
constexpr float kLayoutHeight = 480.0F;
constexpr std::size_t kDumpNameBytes = 256;
constexpr int kBitsPerByte = 8;
constexpr int kScreenSaverFrames = 200;
constexpr unsigned long kPollMilliseconds = 100;
constexpr uint32_t kMovieSurfaceFlag = 0x80000000;

// (device.h declares this too; not included here, its SetRenderState is another one than the one below)

namespace {

struct MovieVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};
constexpr std::size_t kMovieVertexBytes = 0x1C;

static_assert(sizeof(MovieVertex) == kMovieVertexBytes, "the draw's stride");

void set_texture_stage_state(int stage, int type, int value)
{
    d3d9::state().d3d_device()->SetTextureStageState(static_cast<DWORD>(stage), static_cast<D3DTEXTURESTAGESTATETYPE>(type),
                                       static_cast<DWORD>(value));
}

// NOT ORIGINAL grouping - movie.cpp's state (the original's globals). Made on first use, never destroyed
// (movie_state()).
struct MovieState {
    uint32_t movie_width_ = 0;
    uint32_t movie_height_ = 0;
};

MovieState& movie_state()
{
    static core::NoDestroy<MovieState> state;
    return state.get();
}

} // namespace

void set_movie_texture(int stage, GpuTexture* texture, int unknown)
{
    D3DSURFACE_DESC desc;
    d3d9::object(texture)->GetLevelDesc(0, &desc);
    movie_state().movie_width_ = desc.Width;
    movie_state().movie_height_ = desc.Height;
    (void)unknown; // (passed on by the original; SetTexture doesn't take it)
    set_texture(stage, texture);
}

void draw_movie_quad(GpuDevice* device, int type, uint32_t count, void* vertices, uint32_t stride)
{
    const auto buffer_width = static_cast<uint32_t>(renderer().back_buffer_width());
    const auto buffer_height = static_cast<uint32_t>(renderer().back_buffer_height());
    // (4:3, whatever the movie's own shape: some, like the credits, are anamorphic)
    const float target_aspect = 4.0F / 3.0F;
    const float back_buffer_aspect = static_cast<float>(buffer_width) / static_cast<float>(buffer_height);
    auto width = static_cast<float>(buffer_width);
    auto height = static_cast<float>(buffer_height);
    float offset_x = 0.0F;
    float offset_y = 0.0F;
    if (back_buffer_aspect > target_aspect) {
        width = target_aspect / back_buffer_aspect * static_cast<float>(buffer_width);
        offset_x = (static_cast<float>(buffer_width) - width) / 2;
    } else if (back_buffer_aspect < target_aspect) {
        height = back_buffer_aspect / target_aspect * static_cast<float>(buffer_height);
        offset_y = (static_cast<float>(buffer_height) - height) / 2;
    }
    const std::span<MovieVertex, 4> quad(static_cast<MovieVertex*>(vertices), 4);
    core::at(quad, 0).x = offset_x;
    core::at(quad, 0).y = offset_y;
    core::at(quad, 1).x = offset_x + width;
    core::at(quad, 1).y = offset_y;
    core::at(quad, 2).x = offset_x;
    core::at(quad, 2).y = offset_y + height;
    core::at(quad, 3).x = offset_x + width;
    core::at(quad, 3).y = offset_y + height;
    d3d9::object(device)->DrawPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(type), count, vertices, stride);
}

namespace {
constexpr float kFloat255{255.0F};
constexpr float kFloat16384{16384.0F};
constexpr uint32_t kMovieSurfaceType{3};
// NOT ORIGINAL grouping - the back buffer's and the movie picture's sizes while a movie plays (the original's globals).
// Made on first use, never destroyed (movie_sizes()).
struct MovieSizes {
    float movie_back_buffer_width_{kLayoutWidth};
    float movie_back_buffer_height_{kLayoutHeight};
    float movie_picture_width_{kLayoutWidth};
    float movie_picture_height_{kLayoutHeight};
};

MovieSizes& movie_sizes()
{
    static core::NoDestroy<MovieSizes> state;
    return state.get();
}

constexpr float kMovieDrawWidth{1.0F};
constexpr float kMovieDrawHeight{1.0F};
// NOT ORIGINAL grouping - the render states a movie saves and puts back (filter, antialias, cull mode) (the original's
// globals). Made on first use, never destroyed (movie_saved_states()).
struct MovieSavedStates {
    uint32_t saved_min_filter_{};
    uint32_t saved_antialias_{};
    uint32_t saved_cull_mode_{};
};

MovieSavedStates& movie_saved_states()
{
    static core::NoDestroy<MovieSavedStates> state;
    return state.get();
}

} // namespace

// 0x0044FA80
void draw_movie_frame(GpuTexture* texture, maths::Vec2 at, maths::Size size, float alpha)
{
    const float x = at.x;
    const float y = at.y;
    const float width = size.width;
    const float height = size.height;
    if (!texture) {
        return;
    }
    choose_blend_mode(0);
    change_phase(RenderStateId::kAddressMode0, kClampBothWays); // (clamp)
    set_vertex_format(kScreenVertexFormat);
    d3d9::state().d3d_device()->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d3d9::state().d3d_device()->SetFVF(kScreenVertexFormat);
    set_pixel_shader(nullptr, nullptr);                                            // (no pixel shader)
    set_texture_stage_state(0, 1, kTextureOpModulate2x);                                              // colour: modulate
    set_texture_stage_state(0, 2, 2);
    set_texture_stage_state(0, 3, 1);
    set_texture_stage_state(0, 4, 3);
    set_texture_stage_state(1, 1, 1); // stage 1 off
    set_texture(0, nullptr);          // (the original also passes 0, unused)
    // (a leftover from testing: the name of a frame dump that's never written)
    std::array<char, kDumpNameBytes> dump_name{};
    text::format_to(dump_name, "e:\\movietest\\movie%04d.bmp", engine_parts().movie_dump_count());
    ++engine_parts().movie_dump_count();
    // Originally from PartyMod Fixes (its call at 0x0044FB8C): set_movie_texture in place of the renderer's texture set (0x004DA730)
    set_movie_texture(0, texture, 0);

    // (x87: alpha * 255 truncated; each corner is position + size * the screen scale, rounded once)
    const uint32_t color =
        static_cast<uint32_t>(static_cast<int>(static_cast<double>(alpha) * kFloat255)) << 24 | 0x808080;
    std::array<MovieVertex, 4> vertices{};
    core::at(vertices, 0) = {.x = x, .y = y, .z = 0.0F, .w = 1.0F, .color = color, .u = 0.0F, .v = 0.0F};
    core::at(vertices, 1) = core::at(vertices, 0);
    core::at(vertices, 1).x =
        static_cast<float>((static_cast<double>(movie_sizes().movie_back_buffer_width_) * width) + x);
    core::at(vertices, 1).u = 1.0F;
    core::at(vertices, 2) = core::at(vertices, 0);
    core::at(vertices, 2).y =
        static_cast<float>((static_cast<double>(movie_sizes().movie_back_buffer_height_) * height) + y);
    core::at(vertices, 2).v = 1.0F;
    core::at(vertices, 3) = core::at(vertices, 1);
    core::at(vertices, 3).y = core::at(vertices, 2).y;
    core::at(vertices, 3).v = 1.0F;
    if (d3d9::state().d3d_device()->TestCooperativeLevel() == D3D_OK) { // the device isn't lost
        // Originally from PartyMod Fixes (its call at 0x0044FC88): fitted to 4:3, in place of the device's DrawPrimitiveUP
        draw_movie_quad(d3d9::handle(d3d9::state().d3d_device()), D3DPT_TRIANGLESTRIP, 2, vertices.data(), sizeof(MovieVertex));
    }
    set_texture_stage_state(0, 1, 4); // colour: modulate back
}

namespace {

void video_set_loudness(Bink* bink, uint32_t track, int volume)
{
    bink::set_volume(bink, {.track = track, .volume = sound::output_bink_volume(volume)});
} // (NOT ORIGINAL: mute_audio)

// The d_3d device's render and sampler states (IDirect3DDevice9 methods 57/58 and 69/70).
void put_render_state(int state, uint32_t value)
{
    d3d9::state().d3d_device()->SetRenderState(static_cast<D3DRENDERSTATETYPE>(state), value);
}
void get_render_state(int state, uint32_t* value)
{
    DWORD read = *value; // (left as it was if the call fails)
    d3d9::state().d3d_device()->GetRenderState(static_cast<D3DRENDERSTATETYPE>(state), &read);
    *value = read;
}
void get_sampler_state(int sampler, int type, uint32_t* value)
{
    DWORD read = *value;
    d3d9::state().d3d_device()->GetSamplerState(static_cast<DWORD>(sampler), static_cast<D3DSAMPLERSTATETYPE>(type), &read);
    *value = read;
}
void set_sampler_state(int sampler, int type, uint32_t value)
{
    d3d9::state().d3d_device()->SetSamplerState(static_cast<DWORD>(sampler), static_cast<D3DSAMPLERSTATETYPE>(type), value);
}
constexpr int kD3dCullMode = 22;
constexpr int kRsMultisample = 161;
constexpr int kD3dLighting = 137;
constexpr int kMinFilter = 6;
// The states begin_movie changes, for end_movie.
uint32_t& saved_cull_mode() { return movie_saved_states().saved_cull_mode_; }
uint32_t& saved_antialias() { return movie_saved_states().saved_antialias_; }
uint32_t& get_saved_min_filter() { return movie_saved_states().saved_min_filter_; }
// The renderer's cached states (change_phase: 1 the z write, 2 the z test).
void set_renderer_sampler(RenderStateId state, int value) { change_phase(state, static_cast<uint32_t>(value)); }

} // namespace

// 0x0044F8C0
GpuTexture* begin_movie(uint32_t width, uint32_t height)
{
    get_render_state(kD3dCullMode, &saved_cull_mode());
    put_render_state(kD3dCullMode, 1); // none
    get_render_state(kRsMultisample, &saved_antialias());
    put_render_state(kRsMultisample, 0);
    put_render_state(kD3dLighting, 0);
    set_renderer_sampler(RenderStateId::kZWrite, 0);
    set_renderer_sampler(RenderStateId::kZTest, 0);
    get_sampler_state(0, kMinFilter, &get_saved_min_filter());
    set_sampler_state(0, kMinFilter, 2); // linear
    // (x87: unsigned to float through FILD and +2^32)
    movie_sizes().movie_picture_width_ = static_cast<float>(static_cast<double>(width));
    movie_sizes().movie_picture_height_ = static_cast<float>(static_cast<double>(height));
    movie_sizes().movie_back_buffer_width_ = static_cast<float>(renderer().back_buffer_width()); // the back buffer
    movie_sizes().movie_back_buffer_height_ = static_cast<float>(renderer().back_buffer_height());
    IDirect3DTexture9* texture = nullptr;
    // one level, no usage
    const HRESULT result =
        d3d9::state().d3d_device()->CreateTexture(width, height, 1, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, &texture, nullptr);
    if (FAILED(result)) {
        return nullptr;
    }
    engine_parts().set_movie_playing(true);
    return d3d9::handle(texture);
}

// 0x0044F9F0
void end_movie(GpuTexture* texture)
{
    engine_parts().set_movie_playing(false);
    put_render_state(kD3dCullMode, saved_cull_mode());
    put_render_state(kRsMultisample, saved_antialias());
    set_sampler_state(0, kMinFilter, get_saved_min_filter());
    set_renderer_sampler(RenderStateId::kZWrite, 1);
    set_renderer_sampler(RenderStateId::kZTest, 1);
    if (texture) {
        d3d9::object(texture)->Release();
    }
    set_texture(0, nullptr);                                                   // (the original also passes 0, unused)
}

// 0x0044FCC0
void draw_movie(Bink* bink, GpuTexture* texture)
{
    d3d9::state().d3d_device()->BeginScene();
    d3d9::state().d3d_device()->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.0F, 0); // the target, to black (Z 1.0)
    draw_movie_frame(texture, {.x = 0.0F, .y = 0.0F}, {.width = kMovieDrawWidth, .height = kMovieDrawHeight}, 1.0F);
    d3d9::state().d3d_device()->EndScene();
    d3d9::state().d3d_device()->Present(nullptr, nullptr, nullptr, nullptr);
    bink::next_frame(bink);
}

// 0x0044F840
bool button_pressed_again(const std::array<uint8_t, kControlDataBytes>& control_data, uint16_t* seen_up, uint32_t mask)
{
    auto up = static_cast<uint16_t>(core::at(control_data, 2) << kBitsPerByte | core::at(control_data, 3));
    if ((up & mask) != 0U) {
        *seen_up |= static_cast<uint16_t>(mask);
        return false;
    }
    return (*seen_up & mask) != 0;
}

namespace {
// The sound effects manager (a reference-counted singleton) and its main volume (the original calls it as a method
// of the manager; it reads a global), and the music's. 0-100.
void create_sound_manager() { sound::create_manager(false); }
void release_sound_manager() { sound::release_manager(); }
float sound_volume() { return sound::fetch_master_loudness(); }
float current_music_volume() { return audio::get_music_volume(); }
void toggle_fog(bool on) { render::engine::enable_fog(on); }

constexpr uint32_t kStart = 0x0800; // PS2 control data bytes 2-3 (set while up)

constexpr uint32_t kCross = 0x0040;

// Whether a pad asks to skip the movie: Start released after it was seen up, or Cross pressed again.
bool skip_pressed(sio::Hub* input, std::array<uint16_t, 4>& seen_up)
{
    bool skip = false;
    for (int port = 0; port < 4 && !skip; ++port) {
        const sio::InputPort* device = input->get_pad_at_slot(port);
        if (!device) {
            continue;
        }
        const auto& data = device->control_data();
        if ((core::at(data, 2) << kBitsPerByte & kStart) != 0U) {
            core::at(seen_up, port) |= kStart;
        } else if ((core::at(seen_up, port) & kStart) != 0U) {
            skip = true;
        }
        if (!skip && button_pressed_again(data, &core::at(seen_up, port), kCross)) {
            skip = true;
        }
    }
    return skip;
}

} // namespace

// 0x0044FDC0
void play_movie_file(const char* name)
{
    if (!bink::use_direct_sound(audio::get_sound_device())) {
        debug::printf("Bink can't use DSound\n"); // "Bink can't use DSound\n"
    }
    create_sound_manager();
    const float music = current_music_volume();
    const float effects = sound_volume();
    const float volume = music > effects ? music : effects;

    // data\movies\bik\ + the name after its last backslash + .bik (the original only looks for a backslash
    // from the end down to the second character)
    // (the original's 256-byte buffer took the name unbounded)
    const std::string_view text(name);
    size_t start = 0;
    for (size_t i = text.size(); i != 0; --i) {
        if (i < text.size() && core::at(text, i) == '\\') {
            start = i + 1;
            break;
        }
    }
    std::string path = R"(data\movies\bik\)";
    path += text.substr(start);
    path += ".bik";

    audio::stop_music();
    audio::halt_streams(-1); // (all of them)
    engine_parts().set_current_movie(bink::open(path.c_str(), 0));
    if (!engine_parts().current_movie()) {
        release_sound_manager();
        return;
    }
    GpuTexture* texture = begin_movie(engine_parts().current_movie()->width, engine_parts().current_movie()->height);
    // The screen saver's timer is reset every 200 frames (by setting its timeout again).
    UINT screen_saver_timeout = 0;
    SystemParametersInfoA(SPI_GETSCREENSAVETIMEOUT, 0, &screen_saver_timeout, 0);
    uint32_t screen_saver_frames = 0;
    const bool fog = renderer().fog_active() != 0;
    toggle_fog(false);
    if (texture) {
        sio::Hub* input = sio::create_manager(false);
        std::array<uint16_t, 4> seen_up = {};
        bink::set_sound_on_off(engine_parts().current_movie(), 1);
        video_set_loudness(engine_parts().current_movie(), 0,
                           static_cast<int>(static_cast<double>(volume) * kOnePercent // 0.01
                                            * kFloat16384));                           // 16384
        int frames = 0;
        for (;;) {
            if ((screen_saver_timeout != 0U) && screen_saver_frames++ >= kScreenSaverFrames) {
                screen_saver_frames = 0;
                SystemParametersInfoA(SPI_SETSCREENSAVETIMEOUT, screen_saver_timeout, nullptr, 0);
            }
            if (!sys::app_active()) {
                bink::pause(engine_parts().current_movie(), 1);
                while (!sys::app_active()) {
                    Sleep(kPollMilliseconds);
                    os_layer::process_window_events();
                }
                continue;
            }
            if (engine_parts().quit_requested()) {
                break;
            }
            if (engine_parts().device_lost()) {
                frame_loop::Hub::reset_device_if_lost(); // (through the jump at 0x0044F250)
                if (engine_parts().device_lost()) {
                    // Not original - the original (0x00450053-0x00450066) tries again at once, without handling the
                    // window's messages: with the device lost (another full-screen program on the display) it
                    // never gets it back and spins at full CPU, hung. Here it waits and lets the window be
                    // restored, as the inactive-app wait above does.
                    Sleep(kPollMilliseconds);
                    os_layer::process_window_events();
                    continue;
                }
                bink::pause(engine_parts().current_movie(), 0);
            }
            if (bink::wait(engine_parts().current_movie()) != 0) {
                continue;
            }
            Bink* bink = engine_parts().current_movie();
            bink::do_frame(bink);
            auto* d3d_texture = d3d9::object(texture);
            D3DLOCKED_RECT locked;
            if (SUCCEEDED(d3d_texture->LockRect(0, &locked, nullptr, 0))) {
                bink::copy_to_buffer(bink, locked.pBits, {.pitch = locked.Pitch, .height = bink->height, .x = 0, .y = 0, .flags = kMovieSurfaceType | kMovieSurfaceFlag}); // the surface type, copy all
                d3d_texture->UnlockRect(0);
            }
            draw_movie(engine_parts().current_movie(), texture);
            if (engine_parts().current_movie()->frame_num_ >= engine_parts().current_movie()->frames - 1) {
                break;
            }
            ++frames;
            if (frames % 2 == 0) {
                os_layer::process_window_events();
            }
            // PartyMod NOPs the original's "if (frames % 5 != 0) continue;" (0x00450121): the pads are read
            // every frame, so skipping reacts at once.
            input->poll_pads();
            const bool skip = skip_pressed(input, seen_up);
            if (skip) {
                break;
            }
        }
        sio::release_manager();
    }
    engine_parts().set_movie_playing(false);
    toggle_fog(fog);
    end_movie(texture);
    bink::close(engine_parts().current_movie());
    engine_parts().set_current_movie(nullptr);
    set_texture_stage_state(0, 4, 4); // alpha: modulate
    release_sound_manager();
}

namespace {
// thug2-decomp.ini [features] skip_intro_movies=1: the logo and intro movies at start-up aren't played.
bool is_intro_movie(const char* name)
{
    const std::string_view full(name);
    const std::size_t slash = full.find_last_of("\\/");
    const std::string_view base = slash == std::string_view::npos ? full : full.substr(slash + 1);
    for (const char* intro : {"ATVI", "Beenox_Shift", "Dolby_Digital", "Intel_intro", "NSlogo", "intro", "introf"}) {
        const std::string_view wanted(intro);
        if (base.size() < wanted.size()) {
            continue;
        }
        const std::string_view rest = base.substr(wanted.size());
        const bool same_name = std::ranges::equal(base.substr(0, wanted.size()), wanted, [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
        if (same_name && (rest.empty() || rest.starts_with('.'))) {
            return true;
        }
    }
    return false;
}

// NOT ORIGINAL (with skip_intro_movies): any movie the start-up script itself plays (startup_loading_screen - where
// the game plays its logos and intro) counts as an intro movie too: e.g. Anteara Mod's Shorty's movie
// (tools/anteara), while the same movie from GAME MOVIES still plays.
bool from_start_up_script(void* script)
{
    static const uint32_t kStartUp = checksums::hash_from_text("startup_loading_screen");
    return (script != nullptr) && static_cast<script::Script*>(script)->script_title() == kStartUp;
}
} // namespace

bool& skip_intro_movies()
{
    // (not original either: an extension may skip them - engine/ext/extension.h; -edit does: straight into the level
    // editor)
    static bool skip = config_int("features", "skip_intro_movies", 0) != 0 || ext::skips_intro_movies();
    return skip;
}

// 0x0059F0C0: the PlayMovie script command: play the movie named by the unnamed string (through the jump at
// 0x0044F830).
bool play_movie(script::Struct* params, script::Script* script)
{
    const char* name = nullptr;
    if (!params->fetch_text(0U, &name, 1)) {
        return false;
    }
    if (skip_intro_movies() && (is_intro_movie(name) || from_start_up_script(script))) {
        get_log("skipping intro movie %s (thug2-decomp.ini [features] skip_intro_movies)", name);
        return true;
    }
    play_movie_file(name);
    return true;
}

} // namespace render
