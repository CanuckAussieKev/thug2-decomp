#pragma once

#include <cstddef>
#include <cstdint>

// The PC weather (Nx engine: the engine's CWeather - constructor 0x004A14C0, destructor body 0x004A1080, its script
// setters 0x004A10B0-0x004A1450 - over it; made by StartEngine, 0x006FA7C8): rain (lines, with splashes where drops
// land) or snow (point sprites) over a grid of the ground's heights about the camera - 128-unit cells, the heights
// found by casting down through the scene's collision when the grid is updated, the drops cycled through 1024 slots
// by a linear-feedback step.
namespace Nx {

// A row of the grid (6 bytes; at 0x00764330, the rows' columns' height indices after them): the first and last
// columns with ground (0x4000: none) and where its height indices start.
struct sWeatherRow {
    uint16_t first, last;
    uint16_t offset;
};

// The PC weather (0x12AC bytes, vtable 0x0064A860).
struct CPcWeather {
    void** vtable;
    float rainHeight;           // +0x04 how far up a drop starts
    int rainFrames;             // +0x08 its fall's frames (0-254)
    float rainLength;           // +0x0C
    float rainRate;             // +0x10 drops a frame
    uint8_t rainTop[4];         // +0x14 RGBA
    uint8_t rainBottom[4];      // +0x18
    float splashRate;           // +0x1C splashes a frame
    int splashLife;             // +0x20 frames
    float splashSize;           // +0x24
    uint8_t splashColour[4];    // +0x28
    int drops;                  // +0x2C falling now
    bool active;                // +0x30 (a grid made)
    uint8_t pad31[3];
    float snowHeight;           // +0x34
    int snowFrames;             // +0x38
    float snowSize;             // +0x3C
    float snowRate;             // +0x40
    uint8_t snowColour[4];      // +0x44
    float cameraX;              // +0x48 where the camera was
    float cameraY;
    float cameraZ;              // +0x50
    uint8_t engine54[4];
    bool raining;               // +0x58 (else snow)
    uint8_t pad59[3];
    void* splashTexture;        // +0x5C sTexture* (0x45C7EB0F)
    void* snowTexture;          // +0x60 sTexture* (0xC97C5A4C)
    uint8_t* columns;           // +0x64 the rows' columns' height indices
    sWeatherRow* rows;          // +0x68
    int heights[0x100];         // +0x6C the ground's heights (16ths), by index
    int width;                  // +0x46C the grid's columns
    int depth;                  // +0x470 and rows
    float originX, originZ;     // +0x474 its corner
    float rainDue;              // +0x47C drops started so far, with the fraction due
    float splashDue;            // +0x480
    float snowDue;              // +0x484
    uint8_t unknown488[0x10];
    uint8_t stage[0x400];       // +0x498 each slot's frame of its fall (0xFF: none)
    uint8_t offsetX[0x400];     // +0x898 its place in its cell (random)
    uint8_t offsetZ[0x400];     // +0xC98
    uint32_t next;              // +0x1098 the next slot (the feedback step's state)
    uint32_t rainBlend;         // +0x109C blend modes (the fixed alpha in the high byte)
    uint32_t splashBlend;       // +0x10A0
    uint32_t snowBlend;         // +0x10A4
    float splashX[0x20];        // +0x10A8
    float splashY[0x20];        // +0x1128
    float splashZ[0x20];        // +0x11A8
    int splashFrames[0x20];     // +0x1228 left (0: none)
    int nextSplash;             // +0x12A8

    // 0x004CC560: construct (no grid, no splash texture - the snow's left as it was; blend mode 0xA86285A1 with alpha
    // 0x80 for all three). Returns this.
    CPcWeather* Construct();
    // 0x004CBDA0: the destructor's body (a jump to the engine's).
    void DestroyBody();
    // 0x004CC600 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CPcWeather* Destroy(unsigned int freeFlags);
    // 0x004CD1A0 (virtual [1]): the grid made over the scene's collision box (at most 350 x 300 cells): every
    // cell's height cast for and put in a list of levels (within 30 units of one: the same), each cell then taking
    // the highest of itself and its right, lower and lower-right neighbours; the rows' ground spans kept; the slots
    // emptied with random places; the textures found.
    void UpdateGrid();
    // 0x004CBDB0 (virtual [2]): the frame's new drops started (the rate, at most one a frame per slot freed), each
    // in the next free slot (none free: no more).
    void Update(int unused);
    // 0x004CD120 (virtual [3]): drawn about the camera (rain and splashes, or snow), then every falling drop moved
    // on a frame.
    void Render();
    // 0x004CC4D0 / 0x004CC500 / 0x004CC530 (virtual [4] - [6]): the rain's / splashes' / snow's blend mode by name,
    // with a fixed alpha.
    void SetRainBlendMode(uint32_t name, uint32_t fixedAlpha);
    void SetSplashBlendMode(uint32_t name, uint32_t fixedAlpha);
    void SetSnowBlendMode(uint32_t name, uint32_t fixedAlpha);
    // 0x004CBFE0: the rain about (x, z): lines from each drop's height to its length above, coloured bottom to top
    // (blend mode 10 drawn as 9 with the mode's alpha), 16 cells each way.
    void RenderRain(float x, float z);
    // 0x004CC630: the snow: point sprites swaying on a sine.
    void RenderSnow(float x, float z);
    // 0x004CCC00: a new splash where a cast at a random place before the camera lands (when one is due), then every
    // live splash drawn as a quad fading with its frames.
    void RenderSplashes(float x, float z);
};
static_assert(offsetof(CPcWeather, active) == 0x30 && offsetof(CPcWeather, raining) == 0x58 &&
                  offsetof(CPcWeather, heights) == 0x6C && offsetof(CPcWeather, width) == 0x46C &&
                  offsetof(CPcWeather, stage) == 0x498 && offsetof(CPcWeather, next) == 0x1098 &&
                  offsetof(CPcWeather, splashX) == 0x10A8 && offsetof(CPcWeather, nextSplash) == 0x12A8 &&
                  sizeof(CPcWeather) == 0x12AC,
              "layout must match the original");

} // namespace Nx
