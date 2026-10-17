// Self-tests for src/game/gfx/sprite.cpp (the 2D sprite draw): the original against ours with the device and the
// renderer's vertex format call stood in by recorders, and the widescreen front end's edge rule against worked-out
// values.

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "game/gfx/screen.h"
#include "game/gfx/sprite.h"
#include "hook/game.h"
#include "hook/hook.h"
#include "hook/log.h"
#include "hook/selftest.h"
#include "hook/standins.h"

namespace {

using TestStandIns::Branch;
using TestStandIns::Patch;

std::vector<uint32_t> g_sprite_log;

uint32_t Bits(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}

void __cdecl SpriteVertexFormat(uint32_t fvf) { g_sprite_log.insert(g_sprite_log.end(), {0xF7Fu, fvf}); }
long __stdcall SpriteStageState(void* device, int stage, int type, int value)
{
    g_sprite_log.insert(g_sprite_log.end(), {0x557u, reinterpret_cast<uint32_t>(device) & 0xFFF,
                                             static_cast<uint32_t>(stage), static_cast<uint32_t>(type),
                                             static_cast<uint32_t>(value)});
    return 0;
}
long __stdcall SpriteDraw(void* device, int type, unsigned int minIndex, unsigned int vertexCount,
                          unsigned int primitiveCount, const void* indices, int format, const void* vertices,
                          unsigned int stride)
{
    g_sprite_log.insert(g_sprite_log.end(),
                        {0xD1A0u, reinterpret_cast<uint32_t>(device) & 0xFFF, static_cast<uint32_t>(type), minIndex,
                         vertexCount, primitiveCount, reinterpret_cast<uint32_t>(indices),
                         static_cast<uint32_t>(format), stride});
    const auto* words = static_cast<const uint32_t*>(vertices);
    g_sprite_log.insert(g_sprite_log.end(), words, words + 7 * 4);
    return 0;
}

// The device, back buffer, layout conversion, counters, skip flag and the front end's skater pointer, saved.
struct SpriteState {
    uint8_t bytes[6][0x10];
    static constexpr uint32_t kAddresses[6] = {0x007D69E0, 0x00786D6C, 0x00786D80, 0x00786EBC, 0x007D1EC0, 0x007CE478};
    static constexpr uint32_t kSizes[6] = {4, 8, 0x10, 8, 1, 4};
    bool widescreenMenus;
    SpriteState()
    {
        for (int i = 0; i < 6; ++i)
            std::memcpy(bytes[i], reinterpret_cast<void*>(kAddresses[i]), kSizes[i]);
        widescreenMenus = Nx::WidescreenMenus();
    }
    ~SpriteState()
    {
        for (int i = 0; i < 6; ++i)
            std::memcpy(reinterpret_cast<void*>(kAddresses[i]), bytes[i], kSizes[i]);
        Nx::WidescreenMenus() = widescreenMenus;
    }
};

void* StandInDevice()
{
    static void* vtable[0x60];
    for (void*& slot : vtable)
        slot = nullptr;
    vtable[0x10C / 4] = reinterpret_cast<void*>(&SpriteStageState);
    vtable[0x150 / 4] = reinterpret_cast<void*>(&SpriteDraw);
    alignas(16) static void* device[2] = {vtable, nullptr};
    return device;
}

struct Screen {
    int32_t width, height;
    float scaleX, scaleY;
    int32_t offsetX, offsetY;
};
void SetScreen(const Screen& s)
{
    GameVariable<int32_t>(0x00786D6C) = s.width;
    GameVariable<int32_t>(0x00786D70) = s.height;
    GameVariable<float>(0x00786D80) = s.scaleX;
    GameVariable<float>(0x00786D84) = s.scaleY;
    GameVariable<int32_t>(0x00786D88) = s.offsetX;
    GameVariable<int32_t>(0x00786D8C) = s.offsetY;
}

void DrawSprite(Nx::SSprite* sprite, bool original)
{
    if (original)
        Original<void(__thiscall*)(Nx::SSprite*)>(0x004DFCF0)(sprite);
    else
        sprite->Draw();
}

// Original against ours (widescreen_menus off: the original has no such rule) - sprites inside the layout, across
// each edge, wider / taller than it (the full-screen case), at exactly 0 (the y quirk), mirrored by negative
// scales, flipped, rotated (a little, by 180 and -90 degrees), scale 0, transparent, with and without a texture
// (a part of it, all of it), rendering skipped, on four back buffer shapes / layout conversions.
void TestSpriteDraw()
{
    SpriteState state;
    Nx::WidescreenMenus() = false;
    Patch format(0x004D9DB0, Branch(0xE9, 0x004D9DB0, reinterpret_cast<void*>(&SpriteVertexFormat)));
    GameVariable<void*>(0x007D69E0) = StandInDevice();
    GameVariable<void*>(0x007CE478) = nullptr; // the front end

    alignas(16) static Nx::SSpriteTexture textures[2] = {{0, 64, 32, 50, 20}, {0, 256, 128, 256, 128}};
    const Screen screens[] = {
        {1920, 1080, 2.25f, 2.25f, 240, 36}, {1280, 1024, 2.0f, 2.1333334f, 0, 34}, {640, 480, 1.0f, 1.0f, 0, 16},
        {803, 611, 1.2537f, 1.3011f, 13, 7}};
    struct Place {
        float x, y;
    };
    const Place places[] = {{100.3f, 50.7f}, {-20.25f, 10.0f}, {600.5f, 470.1f}, {0.0f, 0.0f},
                            {-5.0f, -7.0f},  {50.0f, -32.0f},  {320.0f, 240.0f}};
    struct Shape {
        uint16_t width, height;
        float xscale, yscale;
    };
    const Shape shapes[] = {{64, 32, 1.0f, 1.0f},     {256, 128, 2.6f, 4.0f}, {16, 16, -1.5f, 2.0f},
                            {40, 20, 1.25f, -0.5f},   {10, 10, 0.0f, 1.0f},   {10, 10, 1.0f, 0.0f},
                            {700, 500, 1.0f, 1.0f},   {640, 480, 1.0f, 1.0f}};
    const float hots[][2] = {{0.0f, 0.0f}, {32.5f, 16.25f}};
    const float rotations[] = {0.0f, 0.3f, 3.14159274f, -1.57079637f};

    std::vector<uint32_t> traces[2];
    int drawn[2] = {};
    for (int run = 0; run < 2; ++run) {
        bool original = run == 0;
        g_sprite_log.clear();
        GameVariable<int>(0x00786EBC) = 10;
        GameVariable<int>(0x00786EC0) = 20;
        int n = 0;
        for (const Screen& screen : screens) {
            SetScreen(screen);
            for (const Place& place : places)
                for (const Shape& shape : shapes)
                    for (const auto& hot : hots)
                        for (float rotation : rotations) {
                            alignas(16) uint8_t bytes[0x40] = {};
                            auto* sprite = reinterpret_cast<Nx::SSprite*>(bytes);
                            sprite->texture = n % 3 == 0 ? nullptr : &textures[n % 3 - 1];
                            sprite->x = place.x;
                            sprite->y = place.y;
                            sprite->width = shape.width;
                            sprite->height = shape.height;
                            sprite->xscale = shape.xscale;
                            sprite->yscale = shape.yscale;
                            sprite->hotX = hot[0];
                            sprite->hotY = hot[1];
                            sprite->rotation = rotation;
                            sprite->color = n % 11 == 5 ? 0x00332211u : 0x80563412u + static_cast<uint32_t>(n);
                            sprite->flipV = (n >> 1) % 2 != 0;
                            sprite->flipU = (n / 5) % 2 != 0;
                            GameVariable<bool>(0x007D1EC0) = n % 13 == 7;
                            size_t before = g_sprite_log.size();
                            DrawSprite(sprite, original);
                            if (g_sprite_log.size() > before + 20)
                                ++drawn[run];
                            g_sprite_log.push_back(0xE0Du);
                            ++n;
                        }
        }
        traces[run] = g_sprite_log;
        traces[run].push_back(GameVariable<uint32_t>(0x00786EBC));
        traces[run].push_back(GameVariable<uint32_t>(0x00786EC0));
    }
    for (size_t i = 0, shown = 0; i < traces[0].size() && i < traces[1].size() && shown < 8; ++i)
        if (traces[0][i] != traces[1][i]) {
            Log("  sprite entry %u: original %08X ours %08X", static_cast<unsigned>(i), traces[0][i], traces[1][i]);
            ++shown;
        }
    if (traces[0].size() != traces[1].size())
        Log("  sprite trace sizes: original %u ours %u", static_cast<unsigned>(traces[0].size()),
            static_cast<unsigned>(traces[1].size()));
    Check(traces[0] == traces[1] && drawn[0] > 800, "gfx: sprite draw (layout mapping, mirrors, flips, rotation)");
}

// The widescreen front end's rule: on 1920x1080 (layout scale 2.25, offset 240 / 36), an upright sprite's edge at
// or past the layout's left / right edge goes to the screen's edge (if the sprite overlaps the layout); with the
// setting off or in a level, for a sprite wholly outside the layout, and for a sprite turned by an angle that isn't
// a quarter turn, nothing changes. Worked-out x of the four corners (top
// left, bottom left, top right, bottom right).
void TestSpriteWidescreenEdges()
{
    SpriteState state;
    Patch format(0x004D9DB0, Branch(0xE9, 0x004D9DB0, reinterpret_cast<void*>(&SpriteVertexFormat)));
    GameVariable<void*>(0x007D69E0) = StandInDevice();
    GameVariable<bool>(0x007D1EC0) = false;
    SetScreen({1920, 1080, 2.25f, 2.25f, 240, 36});
    alignas(16) static uint8_t career[0x640];
    alignas(16) static uint8_t skater[0x40];
    *reinterpret_cast<void**>(skater + 0x20) = career;
    career[0x630] = 3;

    struct Case {
        bool setting, inLevel;
        float x, width, rotation;
        float expected[4];
    };
    const Case cases[] = {
        // across the left edge: from the screen's left edge (off: 2.25 * -10 + 240 = 217.5)
        {true, false, -10.0f, 100.0f, 0.0f, {0.0f, 0.0f, 442.5f, 442.5f}},
        {false, false, -10.0f, 100.0f, 0.0f, {217.5f, 217.5f, 442.5f, 442.5f}},
        {true, true, -10.0f, 100.0f, 0.0f, {217.5f, 217.5f, 442.5f, 442.5f}},
        // exactly at the left edge (off: 240)
        {true, false, 0.0f, 100.0f, 0.0f, {0.0f, 0.0f, 465.0f, 465.0f}},
        {false, false, 0.0f, 100.0f, 0.0f, {240.0f, 240.0f, 465.0f, 465.0f}},
        // across the right edge (layout 700 -> 1815)
        {true, false, 600.0f, 100.0f, 0.0f, {1590.0f, 1590.0f, 1920.0f, 1920.0f}},
        {false, false, 600.0f, 100.0f, 0.0f, {1590.0f, 1590.0f, 1815.0f, 1815.0f}},
        // from just inside the left edge to past the right one (not the full-width case: that needs x <= 0)
        {true, false, 0.5f, 640.0f, 0.0f, {241.125f, 241.125f, 1920.0f, 1920.0f}},
        {false, false, 0.5f, 640.0f, 0.0f, {241.125f, 241.125f, 1681.125f, 1681.125f}},
        // exactly to the right edge (layout 640 -> 1680)
        {true, false, 540.0f, 100.0f, 0.0f, {1455.0f, 1455.0f, 1920.0f, 1920.0f}},
        {false, false, 540.0f, 100.0f, 0.0f, {1455.0f, 1455.0f, 1680.0f, 1680.0f}},
        // inside: unchanged
        {true, false, 100.0f, 100.0f, 0.0f, {465.0f, 465.0f, 690.0f, 690.0f}},
        // turned by 180 degrees about its top left: it spans layout -50..50 (whole pixels on this path)
        {true, false, 50.0f, 100.0f, 3.14159274f, {352.0f, 352.0f, 0.0f, 0.0f}},
        {false, false, 50.0f, 100.0f, 3.14159274f, {352.0f, 352.0f, 127.0f, 127.0f}},
        {true, true, 50.0f, 100.0f, 3.14159274f, {352.0f, 352.0f, 127.0f, 127.0f}},
        // ... 700..600
        {true, false, 700.0f, 100.0f, 3.14159274f, {1920.0f, 1920.0f, 1590.0f, 1590.0f}},
        {false, false, 700.0f, 100.0f, 3.14159274f, {1815.0f, 1815.0f, 1590.0f, 1590.0f}},
        // ... 640..540 (its top left exactly at the right edge)
        {true, false, 640.0f, 100.0f, 3.14159274f, {1920.0f, 1920.0f, 1455.0f, 1455.0f}},
        {false, false, 640.0f, 100.0f, 3.14159274f, {1680.0f, 1680.0f, 1455.0f, 1455.0f}},
        // turned by -90 degrees: (x, y) -> (y, -x), x from 50 to 60 (corners 0 and 2 at 50, 1 and 3 at 60)...
        {true, false, 50.0f, 100.0f, -1.57079637f, {352.0f, 375.0f, 352.0f, 374.0f}}, // (59.9999952: truncated)
        // ... and from -5 to 5 (layout x <= 0 -> 0)
        {true, false, -5.0f, 100.0f, -1.57079637f, {0.0f, 251.0f, 0.0f, 251.0f}},
        {false, false, -5.0f, 100.0f, -1.57079637f, {228.0f, 251.0f, 228.0f, 251.0f}},
        // turned by pi + 0.001, as the menus' bottom band is: corners at layout x 5, 5.01 (its own left) and
        // -94.99995, -94.99 (its own right, now on the left): the two leftmost go to the screen's left edge
        {true, false, 5.0f, 100.0f, 3.14259f, {251.0f, 251.0f, 0.0f, 0.0f}},
        {false, false, 5.0f, 100.0f, 3.14259f, {251.0f, 251.0f, 26.0f, 26.0f}},
        {true, true, 5.0f, 100.0f, 3.14259f, {251.0f, 251.0f, 26.0f, 26.0f}},
        // wholly outside the layout (parked off-screen, a band's end strip): drawn where it is
        {true, false, -100.0f, 50.0f, 0.0f, {15.0f, 15.0f, 127.5f, 127.5f}},
        {true, false, -50.0f, 50.0f, 0.0f, {127.5f, 127.5f, 240.0f, 240.0f}}, // (right edge exactly at 0)
        {true, false, 650.0f, 50.0f, 0.0f, {1702.5f, 1702.5f, 1815.0f, 1815.0f}},
        {true, false, 640.0f, 50.0f, 0.0f, {1680.0f, 1680.0f, 1792.5f, 1792.5f}}, // (left edge exactly at 640)
        {true, false, -50.0f, 100.0f, 3.14159274f, {127.0f, 127.0f, -97.0f, -97.0f}},     // (-150..-50)
        {true, false, -1.0f, 100.0f, 3.14159274f, {237.0f, 237.0f, 12.0f, 12.0f}},        // (-101..-1)
        {true, false, 800.0f, 100.0f, 3.14159274f, {2040.0f, 2040.0f, 1815.0f, 1815.0f}}, // (700..800)
        {true, false, 740.0f, 100.0f, 3.14159274f, {1905.0f, 1905.0f, 1680.0f, 1680.0f}}, // (640..740)
    };
    bool ok = true;
    std::vector<uint32_t> tilted[2];
    for (const Case& c : cases) {
        Nx::WidescreenMenus() = c.setting;
        GameVariable<void*>(0x007CE478) = c.inLevel ? skater : nullptr;
        alignas(16) uint8_t bytes[0x40] = {};
        auto* sprite = reinterpret_cast<Nx::SSprite*>(bytes);
        sprite->x = c.x;
        sprite->y = 100.0f;
        sprite->width = static_cast<uint16_t>(c.width);
        sprite->height = 10;
        sprite->xscale = c.width / sprite->width;
        sprite->yscale = 1.0f;
        sprite->rotation = c.rotation;
        sprite->color = 0x80FFFFFFu;
        g_sprite_log.clear();
        sprite->Draw();
        bool match = g_sprite_log.size() == 2 + 5 * 2 + 9 + 28;
        float got[4] = {};
        for (int i = 0; match && i < 4; ++i) {
            std::memcpy(&got[i], &g_sprite_log[2 + 5 * 2 + 9 + 7 * i], 4);
            match = match && Bits(got[i]) == Bits(c.expected[i]);
        }
        if (!match)
            Log("  sprite edges (setting %d, level %d, x %g, width %g, rotation %g): %g %g %g %g", c.setting, c.inLevel,
                c.x, c.width, c.rotation, got[0], got[1], got[2], got[3]);
        ok = ok && match;
    }
    // A sprite turned by a little: as with the setting off.
    for (int setting = 0; setting < 2; ++setting) {
        Nx::WidescreenMenus() = setting != 0;
        GameVariable<void*>(0x007CE478) = nullptr;
        alignas(16) uint8_t bytes[0x40] = {};
        auto* sprite = reinterpret_cast<Nx::SSprite*>(bytes);
        sprite->x = -10.0f;
        sprite->y = 100.0f;
        sprite->width = 700;
        sprite->height = 10;
        sprite->xscale = sprite->yscale = 1.0f;
        sprite->rotation = 0.3f;
        sprite->color = 0x80FFFFFFu;
        g_sprite_log.clear();
        sprite->Draw();
        tilted[setting] = g_sprite_log;
    }
    ok = ok && tilted[0] == tilted[1] && tilted[0].size() > 40;
    Check(ok, "gfx: sprite edges in the widescreen front end (upright / quarter turns only; off and levels unchanged)");
}

} // namespace

SELFTEST(Early, TestSpriteDraw);
SELFTEST(Early, TestSpriteWidescreenEdges);
