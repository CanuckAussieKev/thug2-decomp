// The PC weather: see pc_weather.h. The maths follows the original's x87 code: double precision where it keeps
// values in registers, floats where it stores them.

#include "engine/gfx/pc_weather.h"

#include <windows.h>

#include <d3d9.h>

#include <bit>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>
#include <array>

#include "engine/core/manager.h"
#include "engine/core/mem.h"
#include "engine/gfx/engine_scene_list.h"
#include "engine/gfx/draw_frame.h"
#include "engine/gfx/render_state.h"
#include "engine/gfx/simple_material.h"
#include "engine/gfx/bitmap_texture.h"
#include "engine/gfx/texture_dict.h"
#include "engine/maths/random.h"
#include "engine/maths/shape_tests.h"
#include "engine/maths/vec4.h"
#include "engine/render/probe_ray.h"
#include "engine/core/checked_index.h"
#include "engine/core/cursor.h"
#include "engine/gfx/pc_texture.h"
#include "engine/render/coll_grid.h"
#include "engine/core/no_destroy.h"
#include "engine/gfx/renderer.h"
#include "engine/gfx/engine_parts.h"
#include "engine/maths/vec2.h"

namespace render {

constexpr std::size_t kWeatherRowCount = 302;
constexpr std::size_t kWeatherColumnBytes = 0x11828;
constexpr int kByte1Shift = 8;
constexpr int kByte2Shift = 16;
constexpr int kByte3Shift = 24;
constexpr int kByteMask = 0xFF;
// The drop slots (0-0x3FF) are visited in a linear-feedback order; a slot holds its frame, or kSlotFree.
constexpr uint32_t kSlotSequenceStart = 0x19B;
constexpr uint32_t kSlotFeedbackMask = 0x480;
constexpr int kDropSlotCount = 0x400;
constexpr int kSlotFree = 0xFF;
constexpr int kSlotLastFrame = 0xFE;
constexpr int kSlotOffsetRange = 0xFF;
constexpr uint32_t kBlendAlphaBits = 0x80000000;
constexpr std::size_t kFrameTableSize = 0x100;
constexpr int kFrameTableEntries = 0x100;
constexpr float kHeightFixedScale = 16.0f;
constexpr float kFromFixed16 = 0.0625f;
constexpr float kRainSpreadWidth = 2048.0f;
constexpr float kByteMaxFloat = 255.0f;
constexpr unsigned kLineVertexBytes = 0x10;
constexpr unsigned kPointVertexBytes = 0x10;
constexpr int kEmptyRow = 0x4000;
// The weather grid: cells 2048 wide (a shift of 11), 16 x 16 of them wrapped.
constexpr int kGridCellShift = 11;
constexpr int kGridCellSize = 0x800;
constexpr int kGridWrapMask = 0xF;
constexpr int kCellColumnStride = 0x100;
// A rain line's second vertex: x in float 4, then y, z and the colour.
constexpr int kLineEndY = 5;
constexpr int kLineEndZ = 6;
constexpr int kLineEndColour = 7;
constexpr int kLineFloats = 8;
constexpr int kRainBatchLimit = 0x7F8;
constexpr float kSnowSpreadWidth = 64.0f;
constexpr float kSwayCycleLength = 256.0f;
constexpr float kFullCircleRadians = 6.28318548f;
constexpr float kSwayAmplitude = 64.0f;
constexpr std::size_t kSnowBatchLimit = 0x7FC;
constexpr float kSnowCellSize = 128.0f;
constexpr float kProbeHeight = 50000.0f;
constexpr int kRandomRange = 0x8000;
constexpr double kRandomUnit = 32768.0;
constexpr float kSplashRadius = 2048.0f;
constexpr float kSplashLift = 4.0f;
constexpr int kSplashCount = 0x20;
constexpr std::size_t kSplashCountSize = 0x20;
constexpr std::size_t kQuadVertices = 6;
constexpr int kQuadSixthVertex = 5;
constexpr int kQuadFloats = 0x24;
constexpr unsigned kSplashVertexBytes = 0x18;
constexpr int kCellSize = 128;
constexpr int kMaxSpanX = 350;
constexpr int kMaxSpanZ = 300;
constexpr int kCellShift = 7;
constexpr int kMaxHeightDifference = 480;

// Checksums used in this file (the text each hashes).
constexpr uint32_t kFixadd = 0xA86285A1u; // fixadd
constexpr uint32_t kSNOW = 0xC97C5A4Cu; // SNOW
constexpr uint32_t kSPLASH = 0x45C7EB0Fu; // SPLASH

namespace {
// NOT ORIGINAL grouping - pc_weather.cpp's state (the original's globals). Made on first use, never destroyed
// (pc_weather_state()).
struct PcWeatherState {
    // The rain / snow grid's rows (SWeatherRow, one more than the grid is deep: at most 302) and the columns' height
    // indices (a 16-bit offset into them). The original kept the indices right after the rows in one block (0x00764330,
    // all the bytes up to 0x00775B58); here each has its own, the indices the whole block's size.
    std::array<WeatherRow, kWeatherRowCount> weather_rows_{};
    std::array<uint8_t, kWeatherColumnBytes> weather_columns_{};
};

PcWeatherState& pc_weather_state()
{
    static core::NoDestroy<PcWeatherState> state;
    return state.get();
}

constexpr int kNoGround = 800000; // (a cell's height when the cast hit nothing)

int ftol(double value) { return static_cast<int>(value); } // (the game's _ftol)
uint32_t get_argb(std::span<const uint8_t, 4> rgba)
{
    return static_cast<uint32_t>(core::at(rgba, 3)) << kByte3Shift | static_cast<uint32_t>(core::at(rgba, 0)) << kByte2Shift |
           static_cast<uint32_t>(core::at(rgba, 1)) << kByte1Shift | core::at(rgba, 2);
}

// The frame's draw counts.
void count_draw(uint32_t primitives)
{
    renderer().count_draw(primitives);
}

// The drop slots' order: a linear-feedback step through 0-0x3FF (0 goes to 0x19B first; 0x19B back to 0).
void step_slot(uint32_t& next)
{
    if (next == 0) {
        next = kSlotSequenceStart;
    } else if (next == kSlotSequenceStart) {
        next = 0;
        return;
    }
    uint32_t value = next;
    if (value & 1)
        value ^= kSlotFeedbackMask;
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
PcWeather::PcWeather()
{
    // (the snow's texture: zero - the original leaves it as it was)
    // NOLINTBEGIN(cppcoreguidelines-prefer-member-initializer): values made by calls: the original's order
    rain_blend_ = blend_mode_from_name(kFixadd) | kBlendAlphaBits;
    splash_blend_ = blend_mode_from_name(kFixadd) | kBlendAlphaBits;
    snow_blend_ = blend_mode_from_name(kFixadd) | kBlendAlphaBits;
    // NOLINTEND(cppcoreguidelines-prefer-member-initializer)
}

// 0x004CC4D0
void PcWeather::set_rain_blend_mode(uint32_t name, uint32_t fixed_alpha)
{
    rain_blend_ = blend_mode_from_name(name) | fixed_alpha << kByte3Shift;
}

// 0x004CC500
void PcWeather::set_splash_blend_mode(uint32_t name, uint32_t fixed_alpha)
{
    splash_blend_ = blend_mode_from_name(name) | fixed_alpha << kByte3Shift;
}

// 0x004CC530
void PcWeather::set_snow_blend_mode(uint32_t name, uint32_t fixed_alpha)
{
    snow_blend_ = blend_mode_from_name(name) | fixed_alpha << kByte3Shift;
}

// 0x004CBDB0
void PcWeather::tick_frame(float /*frame_seconds*/)
{
    if (!active())
        return;
    const int frames = rain_on() ? rain_frames() : snow_frames();
    const float rate = rain_on() ? rain_rate() : snow_rate();
    float& due = rain_on() ? rain_due_ : snow_due_;
    // NOLINTNEXTLINE(bugprone-integer-division): as the original
    auto most = static_cast<float>(kDropSlotCount / frames); // (no more than the slots free up)
    const double amount = rate > most ? most : rate;
    const float before = due;
    const double total = amount + due;
    due = static_cast<float>(total);
    const int count = ftol(total) - ftol(before);
    for (int i = 0; i < count; ++i) {
        uint8_t& slot = core::at(stage, next);
        if (slot != kSlotFree)
            return;
        slot = static_cast<uint8_t>(kSlotLastFrame - static_cast<uint8_t>(frames));
        core::at(offset_x_, next) = static_cast<uint8_t>(maths::rnd(kSlotOffsetRange));
        core::at(offset_z_, next) = static_cast<uint8_t>(maths::rnd(kSlotOffsetRange));
        ++drops();
        step_slot(next);
    }
}

// 0x004CD120
void PcWeather::render()
{
    if (!active())
        return;
    const float x = camera_x();
    const float z = camera_z();
    if (rain_on()) {
        render_splashes(x, z);
        render_rain({.x = x, .y = z});
    } else {
        render_snow({.x = x, .y = z});
    }
    if (!drops())
        return;
    for (uint8_t& slot : stage) { // (each falling drop a frame on; one finishing is freed)
        if (slot == kSlotLastFrame) {
            --drops();
            ++slot;
        } else if (slot != kSlotFree) {
            ++slot;
        }
    }
}

// 0x004CBFE0
void PcWeather::render_rain(maths::Vec2 centre)
{
    const auto x = centre.x;
    const auto z = centre.y;
    if (!drops())
        return;
    const int cell_x = ftol((static_cast<double>(x) - origin_x_) * (1.0f / 128.0f));
    const int cell_z = ftol((static_cast<double>(z) - origin_z_) * (1.0f / 128.0f));
    const int min_x = cell_x - 16;
    const int max_x = cell_x + 16;
    const int min_z = cell_z - 16;
    const int max_z = cell_z + 16;
    if (max_z < 0 || min_z > depth - 1)
        return;
    uint32_t top = get_argb(rain_top()), bottom = get_argb(rain_bottom());
    if ((rain_blend_ & kBlendModeMask) == kBlendModeFixBrighten) { // (drawn as mode 9 with the mode's alpha)
        choose_blend_mode(kBlendModeBrighten);
        const uint32_t alpha = rain_blend_ & 0xFF000000;
        top = (top & kBlendModeMask) | alpha;
        bottom = (bottom & kBlendModeMask) | alpha;
    } else {
        choose_blend_mode(rain_blend_);
    }
    change_phase(kPhaseAlphaTest, 0);
    set_pixel_shader(nullptr, nullptr);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG2);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG2);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    // (in 16ths of a unit)
    const int base_x = ftol(static_cast<double>(origin_x_) * 16.0f);
    const int base_z = ftol(static_cast<double>(origin_z_) * 16.0f);
    const int length = ftol(static_cast<double>(rain_length()) * 16.0f);
    std::array<int, kFrameTableSize> fall{};
    std::array<int, kFrameTableSize> spread{};
    {
        const double frames_f = rain_frames();
        const double height = rain_height();
        int t = 0;
        for (int i = kSlotLastFrame - rain_frames(); i < kFrameTableEntries; ++i, ++t)
            core::at(fall, i) = ftol((height - t / frames_f * height) * kHeightFixedScale);
    }
    for (int i = 0; i < kFrameTableEntries; ++i)
        core::at(spread, i) = ftol(static_cast<double>(i) * kRainSpreadWidth * (1.0f / kByteMaxFloat));
    std::array<float, 0x7F8 + 8> lines{};
    core::Cursor<float> out(lines); // (at the start whenever nothing is used: the original's null stands for it)
    uint32_t used = 0;
    auto flush = [&] {
        const uint32_t count = used >> 5; // (a quarter of the lines: the original's count, as it is)
        if (count) {
            renderer().d3d_device()->DrawPrimitiveUP(D3DPT_LINELIST, count, lines.data(), kLineVertexBytes);
            count_draw(count);
        }
    };
    for (int gz = min_z; gz <= max_z; ++gz) {
        const int row_index = gz < 0 ? 0 : gz > depth - 1 ? depth - 1 : gz;
        const WeatherRow& row = core::at(rows, row_index);
        if (row.first == kEmptyRow)
            continue;
        int column_x = (min_x << kGridCellShift) + base_x;
        const int row_z = (row_index << 11) + base_z;
        const int cell_row = (row_index & 0xF) << 4;
        int height_at = row.offset - row.first + min_x;
        for (int gx = min_x; gx <= max_x; ++gx, ++height_at) {
            const int column = clamp_column(core::at(rows, row_index), gx);
            int cell = (column & kGridWrapMask) + cell_row;
            const int ground = core::at(heights, core::at(columns, height_at));
            column_x += kGridCellSize;
            for (int k = 0; k < 4; ++k, cell += kCellColumnStride) {
                const uint8_t at = core::at(stage, cell);
                if (at == kSlotFree)
                    continue;
                auto px = static_cast<float>(core::at(spread, core::at(offset_x_, cell)) + column_x) * kFromFixed16;
                auto py = static_cast<float>(core::at(fall, at) + ground) * kFromFixed16;
                auto pz = static_cast<float>(core::at(spread, core::at(offset_z_, cell)) + row_z) * kFromFixed16;
                auto py2 = static_cast<float>(length) * kFromFixed16 + py;
                out[0] = px, out[1] = py, out[2] = pz;
                out[3] = std::bit_cast<float>(bottom);
                out[4] = px, out[kLineEndY] = py2, out[kLineEndZ] = pz;
                out[kLineEndColour] = std::bit_cast<float>(top);
                used += kLineFloats;
                out += kLineFloats;
                if (used >= kRainBatchLimit) {
                    flush();
                    out = core::Cursor<float>(lines);
                    used = 0;
                }
            }
        }
    }
    flush();
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

// 0x004CC630
void PcWeather::render_snow(maths::Vec2 centre)
{
    const auto x = centre.x;
    const auto z = centre.y;
    if (!drops())
        return;
    const int cell_x = ftol((static_cast<double>(x) - origin_x_) * (1.0f / 128.0f));
    const int cell_z = ftol((static_cast<double>(z) - origin_z_) * (1.0f / 128.0f));
    const int min_x = cell_x - 16;
    const int max_x = cell_x + 16;
    const int min_z = cell_z - 16;
    const int max_z = cell_z + 16;
    if (max_z < 0 || min_z > depth - 1)
        return;
    choose_blend_mode(snow_blend_);
    change_phase(kPhaseAlphaTest, 0);
    change_phase(kAddressMode0, 0);
    static_cast<TextureRef*>(snow_texture_)->assign_value(0);
    renderer().d3d_device()->SetRenderState(D3DRS_POINTSPRITEENABLE, TRUE);
    renderer().d3d_device()->SetRenderState(D3DRS_POINTSCALEENABLE, TRUE);
    renderer().d3d_device()->SetRenderState(D3DRS_POINTSIZE, std::bit_cast<DWORD>(snow_size()));
    renderer().d3d_device()->SetRenderState(D3DRS_POINTSCALE_A, 0);
    renderer().d3d_device()->SetRenderState(D3DRS_POINTSCALE_B, 0);
    renderer().d3d_device()->SetRenderState(D3DRS_POINTSCALE_C, std::bit_cast<DWORD>(1.0f));
    set_pixel_shader(nullptr, nullptr);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    const float base_x = origin_x_;
    const float base_z = origin_z_;
    std::array<float, kFrameTableSize> fall{};
    std::array<float, kFrameTableSize> spread{};
    std::array<float, kFrameTableSize> sway{};
    {
        const double frames_f = snow_frames();
        const double height = snow_height();
        int t = 0;
        for (int i = kSlotLastFrame - snow_frames(); i < kFrameTableEntries; ++i, ++t)
            core::at(fall, i) = static_cast<float>(height - t / frames_f * height);
    }
    for (int i = 0; i < kFrameTableEntries; ++i)
        core::at(spread, i) = static_cast<float>(static_cast<double>(i) * kSnowSpreadWidth * (1.0f / kByteMaxFloat));
    for (int i = 0; i < kFrameTableEntries; ++i)
        core::at(sway, i) = static_cast<float>(std::sin(static_cast<double>(i) * (1.0f / kSwayCycleLength) * kFullCircleRadians) * kSwayAmplitude);
    const uint32_t colour = get_argb(snow_colour());
    std::array<float, kSnowBatchLimit + 4> points{};
    core::Cursor<float> out(points);
    uint32_t used = 0;
    for (int gz = min_z; gz <= max_z; ++gz) {
        const int row_index = gz < 0 ? 0 : gz > depth - 1 ? depth - 1 : gz;
        const WeatherRow& row = core::at(rows, row_index);
        if (row.first == kEmptyRow)
            continue;
        auto column_x = static_cast<float>(static_cast<double>(min_x) * kSnowCellSize + base_x);
        const int cell_row = (row_index & 0xF) << 4;
        int height_at = row.offset - row.first + min_x;
        auto row_z = static_cast<float>(static_cast<double>(row_index) * kSnowCellSize + base_z);
        for (int gx = min_x; gx <= max_x; ++gx, ++height_at) {
            const int column = clamp_column(core::at(rows, row_index), gx);
            column_x = static_cast<float>(static_cast<double>(column_x) + kSnowCellSize);
            int cell = (column & kGridWrapMask) + cell_row;
            auto ground = static_cast<float>(core::at(heights, core::at(columns, height_at)) >> 4);
            for (int k = 0; k < 4; ++k, cell += kCellColumnStride) {
                const uint8_t at = core::at(stage, cell);
                if (at == kSlotFree)
                    continue;
                const int zb = core::at(offset_z_, cell);
                const int xb = core::at(offset_x_, cell);
                auto px = static_cast<float>(static_cast<double>(core::at(sway, (at + zb) & kByteMask)) + core::at(spread, xb) + column_x);
                auto py = static_cast<float>(static_cast<double>(core::at(fall, at)) + snow_size() + ground);
                auto pz = static_cast<float>(static_cast<double>(core::at(sway, (xb + at) & kByteMask)) + core::at(spread, zb) + row_z);
                out[0] = px, out[1] = py, out[2] = pz;
                out[3] = std::bit_cast<float>(colour);
                used += 4;
                out += 4;
                if (used >= kSnowBatchLimit) {
                    renderer().d3d_device()->DrawPrimitiveUP(D3DPT_POINTLIST, used >> 4, points.data(),
                                                  kPointVertexBytes); // (a quarter, as the original)
                    count_draw(used >> 4);
                    used = 0;
                    out = core::Cursor<float>(points);
                }
            }
        }
    }
    renderer().d3d_device()->DrawPrimitiveUP(D3DPT_POINTLIST, used >> 4, points.data(), kPointVertexBytes); // (even none, as the original)
    count_draw(used >> 4);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    renderer().d3d_device()->SetRenderState(D3DRS_POINTSPRITEENABLE, FALSE);
}

// 0x004CCC00
void PcWeather::render_splashes(float x, float z)
{
    const float before = splash_due_;
    const double total = static_cast<double>(splash_rate()) + splash_due_;
    splash_due_ = static_cast<float>(total);
    if (ftol(total) - ftol(before) != 0) {
        // A cast down at a random place before the camera (along its heading up to 2048 out, across by up to
        // 2048 either side - alternately - nearer in), its landing the splash's.
        ProbeRay feeler;
        const auto& camera = renderer().render_camera().position;
        maths::Vec4 heading = {.x = static_cast<float>(static_cast<double>(x) - core::at(camera, 0)), .y = 0.0f,
                               .z = static_cast<float>(static_cast<double>(z) - core::at(camera, 2)), .w = 1.0f};
        maths::Vec4 at = {.x = core::at(camera, 0), .y = kProbeHeight, .z = core::at(camera, 2), .w = 1.0f};
        heading.make_unit();
        auto along = static_cast<float>(maths::rnd(kRandomRange) * (1.0 / kRandomUnit));
        const double out = static_cast<double>(along) * 2048.0f;
        at.x = static_cast<float>(heading.x * out + at.x);
        at.y = static_cast<float>(at.y + heading.y * out);
        at.z = static_cast<float>(at.z + heading.z * out);
        at.w = static_cast<float>(at.w + heading.w * out);
        double side = (1.0 - maths::rnd(kRandomRange) * (1.0 / kRandomUnit)) * along;
        side *= next_splash_ & 1 ? kSplashRadius : -kSplashRadius;
        auto across = static_cast<float>(-static_cast<double>(heading.x)); // (stored, as the original)
        at.x = static_cast<float>(heading.z * side + at.x);
        at.y = static_cast<float>(at.y + side * 0.0f);
        at.z = static_cast<float>(at.z + across * side);
        at.w = static_cast<float>(at.w + side);
        feeler.change_origin(&at);
        at.y = -kProbeHeight;
        feeler.change_finish(&at);
        const float landing = feeler.get_hit_shape(false, false) ? feeler.contact_pos().y : 50000.0f;
        core::at(splash_x_, next_splash_) = at.x;
        core::at(splash_y_, next_splash_) = static_cast<float>(static_cast<double>(landing) + kSplashLift);
        core::at(splash_z_, next_splash_) = at.z;
        core::at(splash_frames_, next_splash_) = splash_life();
        next_splash_ = (next_splash_ + 1) % kSplashCount;
        feeler.entity().get_release();
    }
    choose_blend_mode(splash_blend_);
    change_phase(kPhaseAlphaTest, 0);
    change_phase(kAddressMode0, 0);
    static_cast<TextureRef*>(splash_texture_)->assign_value(0);
    set_pixel_shader(nullptr, nullptr);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE2X);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE2X);
    set_vertex_format(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    struct Vertex {
        float x, y, z;
        uint32_t colour;
        float u, v;
    };
    std::array<Vertex, kSplashCountSize * kQuadVertices> quads{};
    core::Cursor<Vertex> out(quads);
    uint32_t used = 0;
    for (int i = 0; i < kSplashCount; ++i) {
        const int frames = core::at(splash_frames_, i);
        if (!frames)
            continue;
        const double half = static_cast<double>(splash_size()) * 0.5f;
        auto x0 = static_cast<float>(core::at(splash_x_, i) - half), x1 = static_cast<float>(half + core::at(splash_x_, i));
        auto z0 = static_cast<float>(core::at(splash_z_, i) - half), z1 = static_cast<float>(half + core::at(splash_z_, i));
        std::array<uint8_t, 4> c{};
        for (int k = 0; k < 4; ++k)
            core::at(c, k) = static_cast<uint8_t>(core::at(splash_colour(), k) * frames / splash_life()); // (fading with its frames)
        const uint32_t colour = static_cast<uint32_t>(core::at(c, 3)) << 24 | static_cast<uint32_t>(core::at(c, 2)) << 16 |
                                static_cast<uint32_t>(core::at(c, 1)) << 8 | core::at(c, 0);
        const float y = core::at(splash_y_, i);
        out[0] = {.x = x0, .y = y, .z = z0, .colour = colour, .u = 0.0f, .v = 0.0f};
        out[1] = {.x = x1, .y = y, .z = z0, .colour = colour, .u = 1.0f, .v = 0.0f};
        out[2] = {.x = x1, .y = y, .z = z1, .colour = colour, .u = 1.0f, .v = 1.0f};
        out[3] = {.x = x0, .y = y, .z = z1, .colour = colour, .u = 0.0f, .v = 1.0f};
        out[4] = {.x = x0, .y = y, .z = z0, .colour = colour, .u = 0.0f, .v = 0.0f};
        out[kQuadSixthVertex] = {.x = x1, .y = y, .z = z1, .colour = colour, .u = 1.0f, .v = 1.0f};
        used += kQuadFloats;
        out += kQuadVertices;
        core::at(splash_frames_, i) = frames - 1;
    }
    const uint32_t triangles = used / 6 / 3;
    if (triangles) {
        renderer().d3d_device()->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triangles, quads.data(), kSplashVertexBytes);
        count_draw(triangles);
    }
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    renderer().d3d_device()->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
}

// 0x004CD1A0
void PcWeather::tick_grid()
{
    set_active(false);
    const CollGrid* grid_of_scene = get_first_coll_grid(); // (the scene's collision's box)
    if (!grid_of_scene)
        return;
    const maths::BBox& box = grid_of_scene->box();
    auto cell = [](float value) {
        const int v = ftol(value);
        return v / kCellSize; // (towards zero, as the original's shift with its correction)
    };
    int min_x = cell(box.min.x) - 1, max_x = cell(box.max.x) + 1;
    int min_z = cell(box.min.z) - 1, max_z = cell(box.max.z) + 1;
    if (max_x - min_x > kMaxSpanX) {
        const int trim = (max_x - min_x - 350) / 2;
        min_x += trim;
        max_x -= trim;
    }
    if (max_z - min_z > kMaxSpanZ) {
        const int trim = (max_z - min_z - 300) / 2;
        min_z += trim;
        max_z -= trim;
    }
    const int rows_count = max_z - min_z + 1;
    depth = rows_count;
    origin_x_ = static_cast<float>(min_x << kCellShift);
    width = max_x - min_x + 1;
    origin_z_ = static_cast<float>(min_z << kCellShift);
    memory::Hub* manager = memory::Hub::get_pull();
    manager->enter_heap_scope(manager->top_down_heap());
    auto* grid_block = static_cast<uint8_t*>(memory::alloc(static_cast<uint32_t>(width * depth)));
    const std::span<uint8_t> grid(grid_block, static_cast<size_t>(width) * depth);
    memory::Hub::get_pull()->leave_heap_scope();
    ProbeRay feeler;
    int levels = 0;
    for (int z = min_z, r = 0; z <= max_z; ++z, ++r) {
        auto at_z = static_cast<float>(z << kCellShift);
        for (int x = min_x; x <= max_x; ++x) {
            const int index = width * r - min_x + x;
            maths::Vec4 at = {.x = static_cast<float>(x << kCellShift), .y = kProbeHeight, .z = at_z, .w = 1.0f};
            feeler.change_origin(&at);
            at.y = -kProbeHeight;
            feeler.change_finish(&at);
            const int height = feeler.get_hit_shape(false, false)
                                   ? ftol(static_cast<double>(feeler.contact_pos().y) * 16.0f)
                                   : kNoGround;
            int found = -1;
            for (int i = 0; i < levels; ++i) {
                const int difference = core::at(heights, i) - height;
                if ((difference < 0 ? -difference : difference) < kMaxHeightDifference) {
                    found = i;
                    break;
                }
            }
            if (found >= 0) {
                core::at(grid, index) = static_cast<uint8_t>(found);
            } else {
                // (past 256 levels the original ran on over the fields after the table; ours ends the game)
                core::at(grid, index) = static_cast<uint8_t>(levels);
                core::at(heights, levels) = height;
                ++levels;
            }
        }
    }
    // Each cell the highest of itself and its right, lower and lower-right neighbours. The original also looked
    // past the grid's last row and column (into the heap after the block, whatever was there); ours takes a cell
    // there to be no higher (and the right neighbour of a row's last column is the next row's first, as there).
    const auto height_at = [&](size_t index) {
        return index < grid.size() ? core::at(heights, core::at(grid, index)) : std::numeric_limits<int>::min();
    };
    for (int r = 0; r < rows_count && min_z <= max_z; ++r) {
        for (int x = min_x; x <= max_x; ++x) {
            const auto index = static_cast<size_t>(width * r - min_x + x);
            const auto below = static_cast<size_t>(width * (r + 1) - min_x + x);
            if (height_at(index + 1) > height_at(index))
                core::at(grid, index) = core::at(grid, index + 1);
            if (height_at(below) > height_at(index))
                core::at(grid, index) = core::at(grid, below);
            if (height_at(below + 1) > height_at(index))
                core::at(grid, index) = core::at(grid, below + 1);
        }
    }
    // The rows' ground spans (one row past the last, as the original) and their height indices.
    rows = pc_weather_state().weather_rows_;
    columns = pc_weather_state().weather_columns_;
    uint32_t offset = 0;
    for (int r = 0; r <= depth; ++r) {
        uint16_t first = 0, last = static_cast<uint16_t>(width - 1);
        bool found = false;
        for (int c = 0; c < width; ++c) {
            const auto cell_index = static_cast<size_t>(width * r + c);
            if (cell_index >= grid.size() || height_at(cell_index) == kNoGround)
                continue; // (the row past the last: no ground, the original read the heap there)
            if (!found) {
                first = static_cast<uint16_t>(c);
                found = true;
            } else {
                last = static_cast<uint16_t>(c);
            }
        }
        if (first < last) {
            core::at(rows, r) = {.first = first, .last = last, .offset = static_cast<uint16_t>(offset)};
            for (int c = first; std::cmp_less_equal(c, last); ++c)
                core::at(columns, static_cast<uint16_t>(offset++)) = core::at(grid, width * r + c);
        } else {
            core::at(rows, r) = {.first = kEmptyRow, .last = 0, .offset = 0};
        }
    }
    memory::Hub::get_pull()->dealloc(grid_block);
    for (int i = 0; i < kDropSlotCount; ++i) {
        core::at(stage, i) = kSlotFree;
        core::at(offset_x_, i) = static_cast<uint8_t>(maths::rnd(kSlotOffsetRange));
        core::at(offset_z_, i) = static_cast<uint8_t>(maths::rnd(kSlotOffsetRange));
    }
    drops() = 0;
    next = kSlotSequenceStart;
    splash_texture_ = nullptr;
    if (const BitmapTexture* splash = engine_parts().particle_texture_dict()->fetch_skin(kSPLASH))
        splash_texture_ = image_of(splash);
    if (const BitmapTexture* snow = engine_parts().particle_texture_dict()->fetch_skin(kSNOW))
        snow_texture_ = image_of(snow);
    set_active(true);
    std::memset(splash_frames_.data(), 0, sizeof(splash_frames_));
    next_splash_ = 0;
    feeler.entity().get_release();
}

} // namespace render
