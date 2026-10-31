// Drawing a frame of a movie (the Bink player's texture) as a screen quad - fitted to 4:3 on PC (the fit
// contains code from PartyMod (MIT, (c) 2024 PARTYMANX) - see THIRD_PARTY_NOTICES.md).

#include "engine/gfx/movie.h"

#include <windows.h>

#include <d3d9.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "engine/core/dbg.h"
#include "engine/core/checksums.h"
#include "frontend/start_level.h"
#include "engine/gfx/engine_effects.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/audio/music_player.h"
#include "engine/audio/music_api.h"
#include "engine/script/cscript.h"
#include "engine/script/param_struct.h"
#include "engine/sound/sfx.h"
#include "engine/pad_input/device.h"
#include "engine/sys/mlp.h"
#include "platform/platform.h"
#include "engine/sound/mute_audio.h"
#include "platform/run_log.h"
#include "platform/winmain.h"
#include "platform/crt.h"

namespace render {

// (device.h declares this too; not included here, its SetRenderState is another one than the one below)
extern IDirect3DDevice9* g_d3d_device;

namespace {

struct MovieVertex {
    float x, y, z, w;
    uint32_t color;
    float u, v;
};
static_assert(sizeof(MovieVertex) == 0x1C, "the draw's stride");

void** input_port() { return reinterpret_cast<void**>(g_d3d_device); }
template <typename F>
F get_method(int index)
{
    return reinterpret_cast<F>((*reinterpret_cast<void***>(input_port()))[index]);
}
void set_texture_stage_state(int stage, int type, int value)
{
    get_method<long(__stdcall*)(void*, int, int, int)>(0x10C / 4)(input_port(), stage, type, value);
}

uint32_t s_movie_width = 0;
uint32_t s_movie_height = 0;

} // namespace

void set_movie_texture(int stage, void* texture, int unknown)
{
    // GetLevelDesc(0): a D3DSURFACE_DESC (0x20 bytes: format, type, usage, pool, multisample type and quality,
    // width, height)
    uint32_t desc[8];
    reinterpret_cast<long(__stdcall*)(void*, unsigned int, void*)>((*static_cast<void***>(texture))[0x44 / 4])(texture,
                                                                                                               0, desc);
    s_movie_width = desc[6];
    s_movie_height = desc[7];
    (void)unknown; // (passed on by the original; SetTexture doesn't take it)
    set_texture(stage, texture);
}

void __stdcall draw_movie_quad(void* device, int type, uint32_t count, void* vertices, uint32_t stride)
{
    uint32_t back_buffer_width = reinterpret_cast<uint32_t&>(g_back_buffer_width);
    uint32_t back_buffer_height = reinterpret_cast<uint32_t&>(g_back_buffer_height);
    // (4:3, whatever the movie's own shape: some, like the credits, are anamorphic)
    float target_aspect = 4.0f / 3.0f;
    float back_buffer_aspect = static_cast<float>(back_buffer_width) / static_cast<float>(back_buffer_height);
    float width = static_cast<float>(back_buffer_width);
    float height = static_cast<float>(back_buffer_height);
    float offset_x = 0.0f;
    float offset_y = 0.0f;
    if (back_buffer_aspect > target_aspect) {
        width = target_aspect / back_buffer_aspect * static_cast<float>(back_buffer_width);
        offset_x = (static_cast<float>(back_buffer_width) - width) / 2;
    } else if (back_buffer_aspect < target_aspect) {
        height = back_buffer_aspect / target_aspect * static_cast<float>(back_buffer_height);
        offset_y = (static_cast<float>(back_buffer_height) - height) / 2;
    }
    auto* quad = static_cast<MovieVertex*>(vertices);
    quad[0].x = offset_x;
    quad[0].y = offset_y;
    quad[1].x = offset_x + width;
    quad[1].y = offset_y;
    quad[2].x = offset_x;
    quad[2].y = offset_y + height;
    quad[3].x = offset_x + width;
    quad[3].y = offset_y + height;
    // DrawPrimitiveUP
    reinterpret_cast<long(__stdcall*)(void*, int, uint32_t, const void*, uint32_t)>(
        (*static_cast<void***>(device))[0x14C / 4])(device, type, count, vertices, stride);
}

float g_one_percent{0.01f};
float g_float255{255.0f};
float g_float16384{16384.0f};
uint32_t g_movie_surface_type{3};
float g_movie_back_buffer_width{640.0f};
float g_movie_back_buffer_height{480.0f};
float g_movie_picture_width{640.0f};
float g_movie_picture_height{480.0f};
float g_movie_draw_width{1.0f};
float g_movie_draw_height{1.0f};
uint32_t g_saved_min_filter;
uint32_t g_saved_antialias;
uint32_t g_saved_cull_mode;
bool g_movie_playing;
int g_movie_dump_count;
bool g_device_lost;
bool g_quit_requested;
Bink* g_current_movie; // a Bink movie playing

// 0x0044FA80
void draw_movie_frame(void* texture, float x, float y, float width, float height, float alpha)
{
    if (!texture)
        return;
    choose_blend_mode(0);
    change_phase(kAddressMode0, 0x10001); // (clamp)
    set_vertex_format(0x144);
    get_method<long(__stdcall*)(void*, int, int)>(0xE4 / 4)(input_port(), 22, 1);  // D3DRS_CULLMODE: none
    get_method<long(__stdcall*)(void*, uint32_t)>(0x164 / 4)(input_port(), 0x144); // SetFVF
    set_pixel_shader(nullptr, nullptr);                                            // (no pixel shader)
    set_texture_stage_state(0, 1, 5);                                              // colour: modulate
    set_texture_stage_state(0, 2, 2);
    set_texture_stage_state(0, 3, 1);
    set_texture_stage_state(0, 4, 3);
    set_texture_stage_state(1, 1, 1); // stage 1 off
    set_texture(0, nullptr);          // (the original also passes 0, unused)
    // (a leftover from testing: the name of a frame dump that's never written)
    char dump_name[256];
    std::snprintf(dump_name, sizeof(dump_name), "e:\\movietest\\movie%04d.bmp", g_movie_dump_count);
    ++g_movie_dump_count;
    // Originally from PartyMod Fixes (its call at 0x0044FB8C): set_movie_texture in place of the renderer's texture set (0x004DA730)
    set_movie_texture(0, texture, 0);

    // (x87: alpha * 255 truncated; each corner is position + size * the screen scale, rounded once)
    uint32_t color = static_cast<uint32_t>(static_cast<int>(static_cast<double>(alpha) * g_float255)) << 24 | 0x808080;
    MovieVertex vertices[4];
    vertices[0] = {x, y, 0.0f, 1.0f, color, 0.0f, 0.0f};
    vertices[1] = vertices[0];
    vertices[1].x = static_cast<float>(static_cast<double>(g_movie_back_buffer_width) * width + x);
    vertices[1].u = 1.0f;
    vertices[2] = vertices[0];
    vertices[2].y = static_cast<float>(static_cast<double>(g_movie_back_buffer_height) * height + y);
    vertices[2].v = 1.0f;
    vertices[3] = vertices[1];
    vertices[3].y = vertices[2].y;
    vertices[3].v = 1.0f;
    if (get_method<long(__stdcall*)(void*)>(0xC / 4)(input_port()) == 0) // TestCooperativeLevel: the device isn't lost
        // Originally from PartyMod Fixes (its call at 0x0044FC88): fitted to 4:3, in place of the device's DrawPrimitiveUP
        draw_movie_quad(input_port(), 5 /* triangle strip */, 2, vertices, sizeof(MovieVertex));
    set_texture_stage_state(0, 1, 4); // colour: modulate back
}

namespace {

// The Bink SDK (binkw32.dll), through the game's import table.
int bink_set_sound_system(void* open, void* param)
{
    return reinterpret_cast<int(__stdcall*)(void*, void*)>(
        crt::library_function("binkw32.dll", "_BinkSetSoundSystem@8"))(open, param);
}
void* bink_open_direct_sound()
{
    return reinterpret_cast<void*>(crt::library_function("binkw32.dll", "_BinkOpenDirectSound@4"));
} // the function, passed to the above
Bink* video_open(const char* path, uint32_t flags)
{
    return reinterpret_cast<Bink*(__stdcall*)(const char*, uint32_t)>(
        crt::library_function("binkw32.dll", "_BinkOpen@8"))(path, flags);
}
void video_toggle_sound(Bink* bink, int on)
{
    reinterpret_cast<int(__stdcall*)(Bink*, int)>(crt::library_function("binkw32.dll", "_BinkSetSoundOnOff@8"))(bink,
                                                                                                                on);
}
void video_set_loudness(Bink* bink, uint32_t track, int volume)
{
    reinterpret_cast<void(__stdcall*)(Bink*, uint32_t, int)>(crt::library_function("binkw32.dll", "_BinkSetVolume@12"))(
        bink, track, sound::output_bink_volume(volume));
} // (NOT ORIGINAL: mute_audio)
void bink_pause(Bink* bink, int pause)
{
    reinterpret_cast<int(__stdcall*)(Bink*, int)>(crt::library_function("binkw32.dll", "_BinkPause@8"))(bink, pause);
}
int video_wait_ready(Bink* bink)
{
    return reinterpret_cast<int(__stdcall*)(Bink*)>(crt::library_function("binkw32.dll", "_BinkWait@4"))(bink);
}
void video_decode_frame(Bink* bink)
{
    reinterpret_cast<int(__stdcall*)(Bink*)>(crt::library_function("binkw32.dll", "_BinkDoFrame@4"))(bink);
}
void video_copy_frame(Bink* bink, void* bits, int pitch, uint32_t height, uint32_t x, uint32_t y, uint32_t flags)
{
    reinterpret_cast<int(__stdcall*)(Bink*, void*, int, uint32_t, uint32_t, uint32_t, uint32_t)>(
        crt::library_function("binkw32.dll", "_BinkCopyToBuffer@28"))(bink, bits, pitch, height, x, y, flags);
}
void video_advance_frame(Bink* bink)
{
    reinterpret_cast<void(__stdcall*)(Bink*)>(crt::library_function("binkw32.dll", "_BinkNextFrame@4"))(bink);
}
void video_close(Bink* bink)
{
    reinterpret_cast<void(__stdcall*)(Bink*)>(crt::library_function("binkw32.dll", "_BinkClose@4"))(bink);
}

Bink*& current_movie() { return g_current_movie; }
bool& movie_playing() { return g_movie_playing; }

// The d_3d device's render and sampler states (IDirect3DDevice9 methods 57/58 and 69/70).
void set_render_state(int state, uint32_t value)
{
    get_method<long(__stdcall*)(void*, int, uint32_t)>(0xE4 / 4)(input_port(), state, value);
}
void get_render_state(int state, uint32_t* value)
{
    get_method<long(__stdcall*)(void*, int, uint32_t*)>(0xE8 / 4)(input_port(), state, value);
}
void get_sampler_state(int sampler, int type, uint32_t* value)
{
    get_method<long(__stdcall*)(void*, int, int, uint32_t*)>(0x110 / 4)(input_port(), sampler, type, value);
}
void set_sampler_state(int sampler, int type, uint32_t value)
{
    get_method<long(__stdcall*)(void*, int, int, uint32_t)>(0x114 / 4)(input_port(), sampler, type, value);
}
enum { kD3dCullMode = 22, kRsMultisample = 161, kD3dLighting = 137, kMinFilter = 6 };
// The states begin_movie changes, for end_movie.
uint32_t& saved_cull_mode() { return g_saved_cull_mode; }
uint32_t& saved_antialias() { return g_saved_antialias; }
uint32_t& get_saved_min_filter() { return g_saved_min_filter; }
// The renderer's cached states (change_phase: 1 the z write, 2 the z test).
void set_renderer_sampler(int state, int value) { change_phase(state, static_cast<uint32_t>(value)); }

// A texture's methods (IDirect3DTexture9: Release, LockRect, UnlockRect).
template <typename F>
F texture_method(void* texture, int index)
{
    return reinterpret_cast<F>((*reinterpret_cast<void***>(texture))[index]);
}
struct SurfaceLock {
    int pitch;
    void* bits;
};

} // namespace

// 0x0044F8C0
void* __cdecl begin_movie(uint32_t width, uint32_t height)
{
    get_render_state(kD3dCullMode, &saved_cull_mode());
    set_render_state(kD3dCullMode, 1); // none
    get_render_state(kRsMultisample, &saved_antialias());
    set_render_state(kRsMultisample, 0);
    set_render_state(kD3dLighting, 0);
    set_renderer_sampler(1, 0);
    set_renderer_sampler(2, 0);
    get_sampler_state(0, kMinFilter, &get_saved_min_filter());
    set_sampler_state(0, kMinFilter, 2); // linear
    // (x87: unsigned to float through FILD and +2^32)
    g_movie_picture_width = static_cast<float>(static_cast<double>(width));
    g_movie_picture_height = static_cast<float>(static_cast<double>(height));
    void** device = input_port();
    g_movie_back_buffer_width = static_cast<float>(g_back_buffer_width); // the back buffer
    g_movie_back_buffer_height = static_cast<float>(g_back_buffer_height);
    void* texture;
    // CreateTexture: one level, no usage, D3DFMT_X8R8G8B8 (0x16), D3DPOOL_MANAGED
    long result = get_method<long(__stdcall*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, int, int, void**, void*)>(
        0x5C / 4)(device, width, height, 1, 0, 0x16, 1, &texture, nullptr);
    if (result < 0)
        return nullptr;
    movie_playing() = true;
    return texture;
}

// 0x0044F9F0
void __cdecl end_movie(void* texture)
{
    movie_playing() = false;
    set_render_state(kD3dCullMode, saved_cull_mode());
    set_render_state(kRsMultisample, saved_antialias());
    set_sampler_state(0, kMinFilter, get_saved_min_filter());
    set_renderer_sampler(1, 1);
    set_renderer_sampler(2, 1);
    if (texture)
        texture_method<unsigned long(__stdcall*)(void*)>(texture, 2)(texture); // Release
    set_texture(0, nullptr);                                                   // (the original also passes 0, unused)
}

// 0x0044FCC0
void __cdecl draw_movie(Bink* bink, void* texture)
{
    get_method<long(__stdcall*)(void*)>(0xA4 / 4)(input_port()); // BeginScene
    // Clear: the target, to black (Z 1.0)
    get_method<long(__stdcall*)(void*, uint32_t, const void*, uint32_t, uint32_t, float, uint32_t)>(0xAC / 4)(
        input_port(), 0, nullptr, 1, 0, 1.0f, 0);
    draw_movie_frame(texture, 0.0f, 0.0f, g_movie_draw_width, g_movie_draw_height, 1.0f);
    get_method<long(__stdcall*)(void*)>(0xA8 / 4)(input_port()); // EndScene
    get_method<long(__stdcall*)(void*, const void*, const void*, void*, const void*)>(0x44 / 4)(
        input_port(), nullptr, nullptr, nullptr, nullptr); // Present
    video_advance_frame(bink);
}

// 0x0044F840
bool button_pressed_again(const uint8_t* control_data, uint16_t* seen_up, uint32_t mask)
{
    if (!control_data)
        return false;
    uint16_t up = static_cast<uint16_t>(control_data[2] << 8 | control_data[3]);
    if (up & mask) {
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
bool app_active() { return sys::g_app_active; }
bool quit_requested() { return g_quit_requested; }
bool device_lost() { return g_device_lost; }

enum : uint32_t { kStart = 0x0800, kCross = 0x0040 }; // PS2 control data bytes 2-3 (set while up)
} // namespace

// 0x0044FDC0
void __cdecl play_movie_file(const char* name)
{
    if (!bink_set_sound_system(bink_open_direct_sound(), audio::g_sound_device))
        debug::printf("Bink can't use DSound\n"); // "Bink can't use DSound\n"
    create_sound_manager();
    float music = current_music_volume();
    float effects = sound_volume();
    float volume = music > effects ? music : effects;

    // data\movies\bik\ + the name after its last backslash + .bik (the original only looks for a backslash
    // from the end down to the second character)
    char path[256] = "data\\movies\\bik\\";
    size_t length = std::strlen(name);
    size_t start = 0;
    for (size_t i = length; i != 0; --i) {
        if (name[i] == '\\') {
            start = i + 1;
            break;
        }
    }
#pragma warning(suppress : 4996) // (unbounded, as in the original: the buffer is zeroed, so terminated)
    std::strncpy(path + 16, name + start, length - start);
    std::memcpy(path + std::strlen(path), ".bik", 5);

    audio::stop_music();
    audio::halt_streams(-1); // (all of them)
    current_movie() = video_open(path, 0);
    if (!current_movie()) {
        release_sound_manager();
        return;
    }
    void* texture = begin_movie(current_movie()->width, current_movie()->height);
    // The screen saver's timer is reset every 200 frames (by setting its timeout again).
    UINT screen_saver_timeout = 0;
    SystemParametersInfoA(SPI_GETSCREENSAVETIMEOUT, 0, &screen_saver_timeout, 0);
    uint32_t screen_saver_frames = 0;
    bool fog = reinterpret_cast<int&>(g_fog_active) != 0;
    toggle_fog(false);
    if (texture) {
        sio::Hub* input = sio::create_manager(false);
        uint16_t seen_up[4] = {};
        video_toggle_sound(current_movie(), 1);
        video_set_loudness(current_movie(), 0,
                           static_cast<int>(static_cast<double>(volume) * g_one_percent // 0.01
                                            * g_float16384));                           // 16384
        int frames = 0;
        for (;;) {
            if (screen_saver_timeout && screen_saver_frames++ >= 200) {
                screen_saver_frames = 0;
                SystemParametersInfoA(SPI_SETSCREENSAVETIMEOUT, screen_saver_timeout, nullptr, 0);
            }
            if (!app_active()) {
                bink_pause(current_movie(), 1);
                while (!app_active()) {
                    Sleep(100);
                    os_layer::process_window_events();
                }
                continue;
            }
            if (quit_requested())
                break;
            if (device_lost()) {
                frame_loop::Hub::reset_device_if_lost(); // (through the jump at 0x0044F250)
                if (device_lost()) {
                    // Not original - the original (0x00450053-0x00450066) tries again at once, without handling the
                    // window's messages: with the device lost (another full-screen program on the display) it
                    // never gets it back and spins at full CPU, hung. Here it waits and lets the window be
                    // restored, as the inactive-app wait above does.
                    Sleep(100);
                    os_layer::process_window_events();
                    continue;
                }
                bink_pause(current_movie(), 0);
            }
            if (video_wait_ready(current_movie()))
                continue;
            Bink* bink = current_movie();
            video_decode_frame(bink);
            SurfaceLock locked;
            if (texture_method<long(__stdcall*)(void*, uint32_t, SurfaceLock*, const void*, uint32_t)>(
                    texture, 0x4C / 4)(texture, 0, &locked, nullptr, 0) >= 0) {
                video_copy_frame(bink, locked.bits, locked.pitch, bink->height, 0, 0,
                                 g_movie_surface_type | 0x80000000); // the surface type, copy all
                texture_method<long(__stdcall*)(void*, uint32_t)>(texture, 0x50 / 4)(texture, 0);
            }
            draw_movie(current_movie(), texture);
            if (current_movie()->frame_num_ >= current_movie()->frames - 1)
                break;
            ++frames;
            if (frames % 2 == 0)
                os_layer::process_window_events();
            // PartyMod NOPs the original's "if (frames % 5 != 0) continue;" (0x00450121): the pads are read
            // every frame, so skipping reacts at once.
            input->poll_pads();
            bool skip = false;
            for (int port = 0; port < 4 && !skip; ++port) {
                sio::InputPort* device = input->get_pad_at_slot(port);
                if (!device)
                    continue;
                const uint8_t* data = device->control_data_;
                if (data[2] << 8 & kStart)
                    seen_up[port] |= kStart;
                else if (seen_up[port] & kStart)
                    skip = true;
                if (!skip && button_pressed_again(data, &seen_up[port], kCross))
                    skip = true;
            }
            if (skip)
                break;
        }
        sio::release_manager();
    }
    movie_playing() = false;
    toggle_fog(fog);
    end_movie(texture);
    video_close(current_movie());
    current_movie() = nullptr;
    set_texture_stage_state(0, 4, 4); // alpha: modulate
    release_sound_manager();
}

namespace {
// thug2-decomp.ini [features] skip_intro_movies=1: the logo and intro movies at start-up aren't played.
bool is_intro_movie(const char* name)
{
    const char* base = name;
    for (const char* c = name; *c; ++c)
        if (*c == '\\' || *c == '/')
            base = c + 1;
    for (const char* intro : {"ATVI", "Beenox_Shift", "Dolby_Digital", "Intel_intro", "NSlogo", "intro", "introf"}) {
        size_t length = std::strlen(intro);
        if (_strnicmp(base, intro, length) == 0 && (base[length] == '\0' || base[length] == '.'))
            return true;
    }
    return false;
}

// NOT ORIGINAL (with skip_intro_movies): any movie the start-up script itself plays (startup_loading_screen - where
// the game plays its logos and intro) counts as an intro movie too: e.g. Anteara Mod's Shorty's movie
// (tools/anteara), while the same movie from GAME MOVIES still plays.
bool from_start_up_script(void* script)
{
    static const uint32_t kStartUp = checksums::hash_from_text("startup_loading_screen");
    return script && static_cast<script::Script*>(script)->script_title_ == kStartUp;
}
} // namespace

bool& skip_intro_movies()
{
    // (not original either: -edit implies it - straight into the level editor)
    static bool skip = config_int("features", "skip_intro_movies", 0) != 0 || frontend::begin_level::edit_requested();
    return skip;
}

// 0x0059F0C0: the PlayMovie script command: play the movie named by the unnamed string (through the jump at
// 0x0044F830).
bool __cdecl play_movie(script::Struct* params, void* script)
{
    const char* name = nullptr;
    if (!params->fetch_text(0u, &name, 1))
        return false;
    if (skip_intro_movies() && (is_intro_movie(name) || from_start_up_script(script))) {
        get_log("skipping intro movie %s (thug2-decomp.ini [features] skip_intro_movies)", name);
        return true;
    }
    play_movie_file(name);
    return true;
}

} // namespace render
