// The PC weather: see pc_weather.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include "engine/gfx/pc_weather.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cstdint>
#include <cstring>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/simple_material.h"
#include "engine/gfx/bitmap_texture.h"
#include "engine/gfx/texture_dict.h"
#include "engine/gfx/weather.h"
#include "engine/maths/random.h"
#include "engine/maths/vec4.h"
#include "engine/maths/x87_math.h"
#include "engine/render/probe_ray.h"
#include "engine/core/code_address.h"
#include "engine/gfx/device.h"

namespace render {

// The rain / snow grid's rows (SWeatherRow, one more than the grid is deep: at most 302) and, right after them, the
// columns' height indices (a 16-bit offset into them).
// (the grid, then the rows' spans and their columns - no fixed bound: all the bytes up to the next thing the original
// refers to, 0x00775B58)
uint8_t g_weather_grid[0x11828];

namespace {

extern void** const kWeatherVtable;
constexpr int kNoGround = 800000; // (a cell's height when the cast hit nothing)

IDirect3DDevice9* input_port() { return g_d3d_device; }
int ftol(double value) { return static_cast<int>(value); } // (the game's _ftol)
uint32_t get_argb(const uint8_t* rgba) { return uint32_t(rgba[3]) << 24 | uint32_t(rgba[0]) << 16 | uint32_t(rgba[1]) << 8 | rgba[2]; }

// The frame's draw counts.
void count_draw(uint32_t primitives)
{
    ++reinterpret_cast<uint32_t&>(g_draw_calls);
    reinterpret_cast<uint32_t&>(g_drawn_triangles) += primitives;
}

// The drop slots' order: a linear-feedback step through 0-0x3FF (0 goes to 0x19B first; 0x19B back to 0).
void step_slot(uint32_t& next)
{
    if (next == 0) {
        next = 0x19B;
    } else if (next == 0x19B) {
        next = 0;
        return;
    }
    uint32_t value = next;
    if (value & 1)
        value ^= 0x480;
    next = value >> 1;
}

// A grid column clamped into its row's ground span.
int clamp_column(const WeatherRow& row, int x)
{
    int column = row.first;
    if (x > column) {
        column = row.last;
        if (x < column)
            column = x;
    }
    return column;
}

} // namespace

// 0x004CC560
PcWeather* PcWeather::make_new()
{
    reinterpret_cast<WeatherSystem*>(this)->WeatherSystem::make_new();
    vtable = reinterpret_cast<void**>(kWeatherVtable);
    columns = nullptr;
    splash_texture_ = nullptr; // (the snow's texture left as it was)
    rain_blend_ = blend_mode_from_name(0xA86285A1) | 0x80000000;
    splash_blend_ = blend_mode_from_name(0xA86285A1) | 0x80000000;
    rain_due_ = 0.0f;
    splash_due_ = 0.0f;
    snow_blend_ = blend_mode_from_name(0xA86285A1) | 0x80000000;
    return this;
}

// 0x004CBDA0
void PcWeather::destroy_body()
{
    vtable = reinterpret_cast<void**>(kWeatherVtable);
    reinterpret_cast<WeatherSystem*>(this)->WeatherSystem::destroy_body();
}

// 0x004CC600
PcWeather* PcWeather::teardown(unsigned int free_flags)
{
    destroy_body();
    if (free_flags & 1)
        memory::Hub::get_pull()->dealloc(this);
    return this;
}

// 0x004CC4D0
void PcWeather::set_rain_blend_mode(uint32_t name, uint32_t fixed_alpha)
{
    rain_blend_ = blend_mode_from_name(name) | fixed_alpha << 24;
}

// 0x004CC500
void PcWeather::set_splash_blend_mode(uint32_t name, uint32_t fixed_alpha)
{
    splash_blend_ = blend_mode_from_name(name) | fixed_alpha << 24;
}

// 0x004CC530
void PcWeather::set_snow_blend_mode(uint32_t name, uint32_t fixed_alpha)
{
    snow_blend_ = blend_mode_from_name(name) | fixed_alpha << 24;
}

// 0x004CBDB0
void PcWeather::tick_frame(int /*unused*/)
{
    if (!active)
        return;
    int frames = raining ? rain_frames_ : snow_frames_;
    float rate = raining ? rain_rate_ : snow_rate_;
    float& due = raining ? rain_due_ : snow_due_;
    float most = static_cast<float>(0x400 / frames); // (no more than the slots free up)
    double amount = rate > most ? most : rate;
    float before = due;
    double total = amount + due;
    due = static_cast<float>(total);
    int count = ftol(total) - ftol(before);
    for (int i = 0; i < count; ++i) {
        uint8_t& slot = stage[next];
        if (slot != 0xFF)
            return;
        slot = static_cast<uint8_t>(0xFE - static_cast<uint8_t>(frames));
        offset_x_[next] = static_cast<uint8_t>(maths::rnd(0xFF));
        offset_z_[next] = static_cast<uint8_t>(maths::rnd(0xFF));
        ++drops;
        step_slot(next);
    }
}

// 0x004CD120
void PcWeather::render()
{
    if (!active)
        return;
    float x = camera_x_, z = camera_z_;
    if (raining) {
        render_splashes(x, z);
        render_rain(x, z);
    } else {
        render_snow(x, z);
    }
    if (!drops)
        return;
    for (uint8_t& slot : stage) { // (each falling drop a frame on; one finishing is freed)
        if (slot == 0xFE) {
            --drops;
            ++slot;
        } else if (slot != 0xFF) {
            ++slot;
        }
    }
}

// 0x004CBFE0
void PcWeather::render_rain(float x, float z)
{
    if (!drops)
        return;
    int cell_x = ftol((static_cast<double>(x) - origin_x_) * (1.0f / 128.0f));
    int cell_z = ftol((static_cast<double>(z) - origin_z_) * (1.0f / 128.0f));
    int min_x = cell_x - 16, max_x = cell_x + 16, min_z = cell_z - 16, max_z = cell_z + 16;
    if (max_z < 0 || min_z > depth - 1)
        return;
    uint32_t top = get_argb(rain_top_), bottom = get_argb(rain_bottom_);
    if ((rain_blend_ & 0xFFFFFF) == 10) { // (drawn as mode 9 with the mode's alpha)
        choose_blend_mode(9);
        uint32_t alpha = rain_blend_ & 0xFF000000;
        top = (top & 0xFFFFFF) | alpha;
        bottom = (bottom & 0xFFFFFF) | alpha;
    } else {
        choose_blend_mode(rain_blend_);
    }
    change_phase(kPhaseAlphaTest, 0);
    set_pixel_shader(nullptr, nullptr);
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    // (in 16ths of a unit)
    int base_x = ftol(static_cast<double>(origin_x_) * 16.0f);
    int base_z = ftol(static_cast<double>(origin_z_) * 16.0f);
    int length = ftol(static_cast<double>(rain_length_) * 16.0f);
    int fall[0x100], spread[0x100];
    {
        double frames_f = rain_frames_;
        double height = rain_height_;
        int t = 0;
        for (int i = 0xFE - rain_frames_; i < 0x100; ++i, ++t)
            fall[i] = ftol((height - t / frames_f * height) * 16.0f);
    }
    for (int i = 0; i < 0x100; ++i)
        spread[i] = ftol(static_cast<double>(i) * 2048.0f * (1.0f / 255.0f));
    float lines[0x7F8 + 8];
    float* out = nullptr;
    uint32_t used = 0;
    auto flush = [&] {
        uint32_t count = used >> 5; // (a quarter of the lines: the original's count, as it is)
        if (count) {
            input_port()->DrawPrimitiveUP(D3DPT_LINELIST, count, lines, 0x10);
            count_draw(count);
        }
    };
    for (int gz = min_z; gz <= max_z; ++gz) {
        int row_index = gz < 0 ? 0 : gz > depth - 1 ? depth - 1 : gz;
        const WeatherRow& row = rows[row_index];
        if (row.first == 0x4000)
            continue;
        int column_x = (min_x << 11) + base_x;
        int row_z = (row_index << 11) + base_z;
        int cell_row = (row_index & 0xF) << 4;
        int height_at = row.offset - row.first + min_x;
        for (int gx = min_x; gx <= max_x; ++gx, ++height_at) {
            int column = clamp_column(rows[row_index], gx);
            int cell = (column & 0xF) + cell_row;
            int ground = heights[columns[height_at]];
            column_x += 0x800;
            for (int k = 0; k < 4; ++k, cell += 0x100) {
                uint8_t at = stage[cell];
                if (at == 0xFF)
                    continue;
                float px = static_cast<float>((spread[offset_x_[cell]] + column_x) * 0.0625f);
                float py = static_cast<float>((fall[at] + ground) * 0.0625f);
                float pz = static_cast<float>((spread[offset_z_[cell]] + row_z) * 0.0625f);
                float py2 = static_cast<float>(length * 0.0625f + py);
                if (!out)
                    out = lines;
                out[0] = px, out[1] = py, out[2] = pz;
                std::memcpy(&out[3], &bottom, 4);
                out[4] = px, out[5] = py2, out[6] = pz;
                std::memcpy(&out[7], &top, 4);
                used += 8;
                out += 8;
                if (used >= 0x7F8) {
                    flush();
                    out = lines;
                    used = 0;
                }
            }
        }
    }
    flush();
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

// 0x004CC630
void PcWeather::render_snow(float x, float z)
{
    if (!drops)
        return;
    int cell_x = ftol((static_cast<double>(x) - origin_x_) * (1.0f / 128.0f));
    int cell_z = ftol((static_cast<double>(z) - origin_z_) * (1.0f / 128.0f));
    int min_x = cell_x - 16, max_x = cell_x + 16, min_z = cell_z - 16, max_z = cell_z + 16;
    if (max_z < 0 || min_z > depth - 1)
        return;
    choose_blend_mode(snow_blend_);
    change_phase(kPhaseAlphaTest, 0);
    change_phase(kAddressMode0, 0);
    static_cast<TextureRef*>(snow_texture_)->assign_value(0);
    input_port()->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE);
    input_port()->SetRenderState(D3DRS_POINTSCALEENABLE, TRUE);
    input_port()->SetRenderState(D3DRS_POINTSIZE, std::bit_cast<DWORD>(snow_size_));
    input_port()->SetRenderState(D3DRS_POINTSCALE_A, 0);
    input_port()->SetRenderState(D3DRS_POINTSCALE_B, 0);
    input_port()->SetRenderState(D3DRS_POINTSCALE_C, std::bit_cast<DWORD>(1.0f));
    set_pixel_shader(nullptr, nullptr);
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    float base_x = origin_x_, base_z = origin_z_;
    float fall[0x100], spread[0x100], sway[0x100];
    {
        double frames_f = snow_frames_;
        double height = snow_height_;
        int t = 0;
        for (int i = 0xFE - snow_frames_; i < 0x100; ++i, ++t)
            fall[i] = static_cast<float>(height - t / frames_f * height);
    }
    for (int i = 0; i < 0x100; ++i)
        spread[i] = static_cast<float>(static_cast<double>(i) * 64.0f * (1.0f / 255.0f));
    for (int i = 0; i < 0x100; ++i)
        sway[i] = static_cast<float>(x87::get_sin(static_cast<double>(i) * (1.0f / 256.0f) * 6.28318548f) * 64.0f);
    uint32_t colour = get_argb(snow_colour_);
    float points[0x7FC + 4];
    float* out = nullptr;
    uint32_t used = 0;
    for (int gz = min_z; gz <= max_z; ++gz) {
        int row_index = gz < 0 ? 0 : gz > depth - 1 ? depth - 1 : gz;
        const WeatherRow& row = rows[row_index];
        if (row.first == 0x4000)
            continue;
        float column_x = static_cast<float>(static_cast<double>(min_x) * 128.0f + base_x);
        int cell_row = (row_index & 0xF) << 4;
        int height_at = row.offset - row.first + min_x;
        float row_z = static_cast<float>(static_cast<double>(row_index) * 128.0f + base_z);
        for (int gx = min_x; gx <= max_x; ++gx, ++height_at) {
            int column = clamp_column(rows[row_index], gx);
            column_x = static_cast<float>(static_cast<double>(column_x) + 128.0f);
            int cell = (column & 0xF) + cell_row;
            float ground = static_cast<float>(heights[columns[height_at]] >> 4);
            for (int k = 0; k < 4; ++k, cell += 0x100) {
                uint8_t at = stage[cell];
                if (at == 0xFF)
                    continue;
                int zb = offset_z_[cell], xb = offset_x_[cell];
                float px = static_cast<float>(static_cast<double>(sway[(at + zb) & 0xFF]) + spread[xb] + column_x);
                float py = static_cast<float>(static_cast<double>(fall[at]) + snow_size_ + ground);
                float pz = static_cast<float>(static_cast<double>(sway[(xb + at) & 0xFF]) + spread[zb] + row_z);
                if (!out)
                    out = points;
                out[0] = px, out[1] = py, out[2] = pz;
                std::memcpy(&out[3], &colour, 4);
                used += 4;
                out += 4;
                if (used >= 0x7FC) {
                    input_port()->DrawPrimitiveUP(D3DPT_POINTLIST, used >> 4, points, 0x10); // (a quarter, as the original)
                    count_draw(used >> 4);
                    used = 0;
                    out = points;
                }
            }
        }
    }
    input_port()->DrawPrimitiveUP(D3DPT_POINTLIST, used >> 4, points, 0x10); // (even none, as the original)
    count_draw(used >> 4);
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    input_port()->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
}

// 0x004CCC00
void PcWeather::render_splashes(float x, float z)
{
    float before = splash_due_;
    double total = static_cast<double>(splash_rate_) + splash_due_;
    splash_due_ = static_cast<float>(total);
    if (ftol(total) - ftol(before) != 0) {
        // A cast down at a random place before the camera (along its heading up to 2048 out, across by up to
        // 2048 either side - alternately - nearer in), its landing the splash's.
        ProbeRay feeler;
        feeler.make_new();
        const float* camera = &g_camera.position[0];
        maths::Vec4 heading = {static_cast<float>(static_cast<double>(x) - camera[0]), 0.0f,
                               static_cast<float>(static_cast<double>(z) - camera[2]), 1.0f};
        maths::Vec4 at = {camera[0], 50000.0f, camera[2], 1.0f};
        heading.make_unit();
        float along = static_cast<float>(maths::rnd(0x8000) * (1.0 / 32768.0));
        double out = static_cast<double>(along) * 2048.0f;
        at.x = static_cast<float>(heading.x * out + at.x);
        at.y = static_cast<float>(at.y + heading.y * out);
        at.z = static_cast<float>(at.z + heading.z * out);
        at.w = static_cast<float>(at.w + heading.w * out);
        double side = (1.0 - maths::rnd(0x8000) * (1.0 / 32768.0)) * along;
        side *= next_splash_ & 1 ? 2048.0f : -2048.0f;
        float across = static_cast<float>(-static_cast<double>(heading.x)); // (stored, as the original)
        at.x = static_cast<float>(heading.z * side + at.x);
        at.y = static_cast<float>(at.y + side * 0.0f);
        at.z = static_cast<float>(at.z + across * side);
        at.w = static_cast<float>(at.w + side);
        feeler.change_origin(&at);
        at.y = -50000.0f;
        feeler.change_finish(&at);
        float landing = feeler.get_hit_shape(false, false) ? feeler.contact_pos_.y : 50000.0f;
        splash_x_[next_splash_] = at.x;
        splash_y_[next_splash_] = static_cast<float>(static_cast<double>(landing) + 4.0f);
        splash_z_[next_splash_] = at.z;
        splash_frames_[next_splash_] = splash_life_;
        next_splash_ = (next_splash_ + 1) % 0x20;
        feeler.entity_.get_release();
    }
    choose_blend_mode(splash_blend_);
    change_phase(kPhaseAlphaTest, 0);
    change_phase(kAddressMode0, 0);
    static_cast<TextureRef*>(splash_texture_)->assign_value(0);
    set_pixel_shader(nullptr, nullptr);
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    struct Vertex {
        float x, y, z;
        uint32_t colour;
        float u, v;
    };
    Vertex quads[0x20 * 6];
    Vertex* out = quads;
    uint32_t used = 0;
    for (int i = 0; i < 0x20; ++i) {
        int frames = splash_frames_[i];
        if (!frames)
            continue;
        double half = static_cast<double>(splash_size_) * 0.5f;
        float x0 = static_cast<float>(splash_x_[i] - half), x1 = static_cast<float>(half + splash_x_[i]);
        float z0 = static_cast<float>(splash_z_[i] - half), z1 = static_cast<float>(half + splash_z_[i]);
        uint8_t c[4];
        for (int k = 0; k < 4; ++k)
            c[k] = static_cast<uint8_t>(splash_colour_[k] * frames / splash_life_); // (fading with its frames)
        uint32_t colour = uint32_t(c[3]) << 24 | uint32_t(c[2]) << 16 | uint32_t(c[1]) << 8 | c[0];
        float y = splash_y_[i];
        out[0] = {x0, y, z0, colour, 0.0f, 0.0f};
        out[1] = {x1, y, z0, colour, 1.0f, 0.0f};
        out[2] = {x1, y, z1, colour, 1.0f, 1.0f};
        out[3] = {x0, y, z1, colour, 0.0f, 1.0f};
        out[4] = {x0, y, z0, colour, 0.0f, 0.0f};
        out[5] = {x1, y, z1, colour, 1.0f, 1.0f};
        used += 0x24;
        out += 6;
        splash_frames_[i] = frames - 1;
    }
    uint32_t triangles = used / 6 / 3;
    if (triangles) {
        input_port()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, quads, 0x18);
        count_draw(triangles);
    }
    input_port()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    input_port()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

// 0x004CD1A0
void PcWeather::tick_grid()
{
    active = false;
    auto* box = reinterpret_cast<const float*>(get_first_coll_grid()); // (the scene's collision's box)
    if (!box)
        return;
    auto cell = [](float value) {
        int v = ftol(value);
        return v / 128; // (towards zero, as the original's shift with its correction)
    };
    int min_x = cell(box[0]) - 1, max_x = cell(box[4]) + 1;
    int min_z = cell(box[2]) - 1, max_z = cell(box[6]) + 1;
    if (max_x - min_x > 350) {
        int trim = (max_x - min_x - 350) / 2;
        min_x += trim;
        max_x -= trim;
    }
    if (max_z - min_z > 300) {
        int trim = (max_z - min_z - 300) / 2;
        min_z += trim;
        max_z -= trim;
    }
    int rows_count = max_z - min_z + 1;
    depth = rows_count;
    origin_x_ = static_cast<float>(min_x << 7);
    width = max_x - min_x + 1;
    origin_z_ = static_cast<float>(min_z << 7);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap_);
    auto* grid = static_cast<uint8_t*>(memory::alloc(static_cast<uint32_t>(width * depth)));
    memory::Hub::get_pull()->leave_heap_scope();
    ProbeRay feeler;
    feeler.make_new();
    int levels = 0;
    int* level = heights; // (no limit on the levels, as the original: past 256 they run on over the fields after)
    for (int z = min_z, r = 0; z <= max_z; ++z, ++r) {
        float at_z = static_cast<float>(z << 7);
        for (int x = min_x; x <= max_x; ++x) {
            int index = width * r - min_x + x;
            maths::Vec4 at = {static_cast<float>(x << 7), 50000.0f, at_z, 1.0f};
            feeler.change_origin(&at);
            at.y = -50000.0f;
            feeler.change_finish(&at);
            int height = feeler.get_hit_shape(false, false) ? ftol(static_cast<double>(feeler.contact_pos_.y) * 16.0f)
                                                           : kNoGround;
            int found = -1;
            for (int i = 0; i < levels; ++i) {
                int difference = heights[i] - height;
                if ((difference < 0 ? -difference : difference) < 480) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                grid[index] = static_cast<uint8_t>(found);
            } else {
                grid[index] = static_cast<uint8_t>(levels);
                ++levels;
                *level++ = height;
            }
        }
    }
    // Each cell the highest of itself and its right, lower and lower-right neighbours (past the grid's edges too,
    // as the original).
    for (int r = 0; r < rows_count && min_z <= max_z; ++r) {
        for (int x = min_x; x <= max_x; ++x) {
            int index = width * r - min_x + x;
            int below = width * (r + 1) - min_x + x;
            if (heights[grid[index + 1]] > heights[grid[index]])
                grid[index] = grid[index + 1];
            if (heights[grid[below]] > heights[grid[index]])
                grid[index] = grid[below];
            if (heights[grid[below + 1]] > heights[grid[index]])
                grid[index] = grid[below + 1];
        }
    }
    // The rows' ground spans (one row past the last, as the original) and their height indices.
    rows = reinterpret_cast<WeatherRow*>(g_weather_grid);
    columns = reinterpret_cast<uint8_t*>(rows + depth);
    uint32_t offset = 0;
    for (int r = 0; r <= depth; ++r) {
        uint16_t first = 0, last = static_cast<uint16_t>(width - 1);
        bool found = false;
        for (int c = 0; c < width; ++c) {
            if (heights[grid[width * r + c]] == kNoGround)
                continue;
            if (!found) {
                first = static_cast<uint16_t>(c);
                found = true;
            } else {
                last = static_cast<uint16_t>(c);
            }
        }
        if (first < last) {
            rows[r] = {first, last, static_cast<uint16_t>(offset)};
            for (int c = first; c <= last; ++c)
                columns[static_cast<uint16_t>(offset++)] = grid[width * r + c];
        } else {
            rows[r] = {0x4000, 0, 0};
        }
    }
    memory::Hub::get_pull()->dealloc(grid);
    for (int i = 0; i < 0x400; ++i) {
        stage[i] = 0xFF;
        offset_x_[i] = static_cast<uint8_t>(maths::rnd(0xFF));
        offset_z_[i] = static_cast<uint8_t>(maths::rnd(0xFF));
    }
    drops = 0;
    next = 0x19B;
    splash_texture_ = nullptr;
    auto* dictionary = reinterpret_cast<void*>(g_particle_texture_dict);
    auto* splash = reinterpret_cast<uint8_t*>(static_cast<TextureSet*>(dictionary)->fetch_skin(0x45C7EB0F));
    if (splash)
        splash_texture_ = splash[8] & 1 ? **reinterpret_cast<void***>(splash + 0x10) : *reinterpret_cast<void**>(splash + 0x10);
    auto* snow = reinterpret_cast<uint8_t*>(
        static_cast<TextureSet*>(reinterpret_cast<void*>(g_particle_texture_dict))->fetch_skin(0xC97C5A4C));
    if (snow)
        snow_texture_ = snow[8] & 1 ? **reinterpret_cast<void***>(snow + 0x10) : *reinterpret_cast<void**>(snow + 0x10);
    active = true;
    std::memset(splash_frames_, 0, sizeof(splash_frames_));
    next_splash_ = 0;
    feeler.entity_.get_release();
}

namespace {
static VtableSlot g_weather_vtable_slots[] = {&render::PcWeather::teardown, &render::PcWeather::tick_grid,
                &render::PcWeather::tick_frame, &render::PcWeather::render, &render::PcWeather::set_rain_blend_mode,
                &render::PcWeather::set_splash_blend_mode, &render::PcWeather::set_snow_blend_mode};
void** const kWeatherVtable = reinterpret_cast<void**>(g_weather_vtable_slots);
} // namespace

} // namespace Nx
