#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <span>

#include "engine/gfx/weather.h"
#include "engine/maths/vec2.h"

// The PC weather (Nx engine: the engine's WeatherSystem - constructor 0x004A14C0, destructor body 0x004A1080, its script
// setters 0x004A10B0-0x004A1450 - over it; made by start_engine, 0x006FA7C8): rain (lines, with splashes where drops
// land) or snow (point sprites) over a grid of the ground's heights about the camera - 128-unit cells, the heights
// found by casting down through the scene's collision when the grid is updated, the drops cycled through 1024 slots
// by a linear-feedback step.
namespace render {

constexpr std::size_t kHeightGridCells = 0x100;
constexpr std::size_t kWeatherUnknown488Bytes = 0x10;
constexpr std::size_t kWeatherSlots = 0x400;
constexpr std::size_t kSplashSlots = 0x20;

// A row of the grid (6 bytes; at 0x00764330, the rows' columns' height indices after them): the first and last
// columns with ground (0x4000: none) and where its height indices start.
struct WeatherRow {
    uint16_t first, last;
    uint16_t offset;
};

// The PC weather (0x12AC bytes, vtable 0x0064A860).
class PcWeather final : public WeatherSystem {
private:
    void* splash_texture_{nullptr}; // +0x5C TextureRef* (0x45C7EB0F)
    void* snow_texture_{};      // +0x60 TextureRef* (0xC97C5A4C)
    std::span<uint8_t> columns;     // the rows' columns' height indices
    std::span<WeatherRow> rows;     // the rows (one more than the grid is deep)
    std::array<int, kHeightGridCells> heights{};       // +0x6C the ground's heights (16ths), by index
    int width{};                // +0x46C the grid's columns
    int depth{};                // +0x470 and rows
    float origin_x_{}, origin_z_{}; // +0x474 its corner
    float rain_due_{0.0f};          // +0x47C drops started so far, with the fraction due
    float splash_due_{0.0f};        // +0x480
    float snow_due_{};          // +0x484
    std::array<uint8_t, kWeatherUnknown488Bytes> unknown488{};
    std::array<uint8_t, kWeatherSlots> stage{};     // +0x498 each slot's frame of its fall (0xFF: none)
    std::array<uint8_t, kWeatherSlots> offset_x_{}; // +0x898 its place in its cell (random)
    std::array<uint8_t, kWeatherSlots> offset_z_{}; // +0xC98
    uint32_t next{};            // +0x1098 the next slot (the feedback step's state)
    uint32_t rain_blend_;     // +0x109C blend modes (the fixed alpha in the high byte)
    uint32_t splash_blend_;   // +0x10A0
    uint32_t snow_blend_;     // +0x10A4
    std::array<float, kSplashSlots> splash_x_{};  // +0x10A8
    std::array<float, kSplashSlots> splash_y_{};  // +0x1128
    std::array<float, kSplashSlots> splash_z_{};  // +0x11A8
    std::array<int, kSplashSlots> splash_frames_{}; // +0x1228 left (0: none)
    int next_splash_{};         // +0x12A8

public:

    // 0x004CC560: no grid, no splash texture - the snow's left as it was; blend mode 0xA86285A1 with alpha 0x80 for
    // all three.
    PcWeather();
    // 0x004CBDA0 (0x004CC600 the scalar deleting destructor): a jump to the engine's.
    ~PcWeather() override = default;
    // 0x004CD1A0 (vtable slot 1): the grid made over the scene's collision box (at most 350 x 300 cells): every
    // cell's height cast for and put in a list of levels (within 30 units of one: the same), each cell then taking
    // the highest of itself and its right, lower and lower-right neighbours; the rows' ground spans kept; the slots
    // emptied with random places; the textures found.
    void tick_grid() override;
    // 0x004CBDB0 (slot 2): the frame's new drops started (the rate, at most one a frame per slot freed), each
    // in the next free slot (none free: no more).
    void tick_frame(float frame_seconds) override;
    // 0x004CD120 (slot 3): drawn about the camera (rain and splashes, or snow), then every falling drop moved
    // on a frame.
    void render() override;
    // 0x004CC4D0 / 0x004CC500 / 0x004CC530 (slots 4 - 6): the rain's / splashes' / snow's blend mode by name, with a
    // fixed alpha.
    void set_rain_blend_mode(uint32_t name, uint32_t fixed_alpha) override;
    void set_splash_blend_mode(uint32_t name, uint32_t fixed_alpha) override;
    void set_snow_blend_mode(uint32_t name, uint32_t fixed_alpha) override;
    // 0x004CBFE0: the rain about (x, z): lines from each drop's height to its length above, coloured bottom to top
    // (blend mode 10 drawn as 9 with the mode's alpha), 16 cells each way.
    void render_rain(maths::Vec2 centre);
    // 0x004CC630: the snow: point sprites swaying on a sine.
    void render_snow(maths::Vec2 centre);
    // 0x004CCC00: a new splash where a cast at a random place before the camera lands (when one is due), then every
    // live splash drawn as a quad fading with its frames.
    void render_splashes(float x, float z);
};

} // namespace render
