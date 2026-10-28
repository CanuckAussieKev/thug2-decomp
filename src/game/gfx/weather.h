#pragma once

#include <cstddef>
#include <cstdint>

namespace Script {
class CStruct;
class CScript;
} // namespace Script

// The engine's weather (Nx; the PC's CPcWeather - pc_weather.h - derives from it): the rain's, splashes' and snow's
// settings the Weather* script commands set; the drawing is the PC's.
namespace Nx {

// The engine's weather (vtable 0x00649E30; its fields up to +0x5C - see CPcWeather for the rest).
class CWeather {
public:
    // 0x004A14C0: the defaults (rain 2000 high, 40 frames, 100 long, rate 0, black to white; splashes rate 0, 8
    // frames, 16 big, grey; snow 500 high, 254 frames, 4 big, rate 0, grey; rain chosen, no grid). Returns this.
    CWeather* Construct();
    // 0x004A1080: the destructor's body.
    void DestroyBody();
    // 0x004A1550 (virtual [0]): destroyed, and freed with flags & 1. Returns this.
    CWeather* Destroy(unsigned int freeFlags);
    // 0x004A1020 / 0x004A1030 / 0x004A1040 (virtual [1] - [3]; 0x004A1040 is shared by other vtables): the platform's
    // ("STUB: plat_update_grid" / "plat_process" / "plat_render").
    void UpdateGrid();
    void Update(int unused);
    void Render();
    // 0x004A1050 / 0x004A1060 / 0x004A1070 (virtual [4] - [6]): the blend modes ("STUB: plat_set_..._blend_mode").
    void SetRainBlendMode(uint32_t name, int fixedAlpha);
    void SetSplashBlendMode(uint32_t name, int fixedAlpha);
    void SetSnowBlendMode(uint32_t name, int fixedAlpha);

    // The script commands' setters (thiscall with the command's parameters; true, except the blend modes without a
    // mode's name): each value's default when it isn't given; the frames kept within 0-254.
    bool ScriptSetRainHeight(Script::CStruct* params, Script::CScript* script);      // 0x004A10B0
    bool ScriptSetRainFrames(Script::CStruct* params, Script::CScript* script);      // 0x004A10D0
    bool ScriptSetRainLength(Script::CStruct* params, Script::CScript* script);      // 0x004A1110
    bool ScriptSetRainBlendMode(Script::CStruct* params, Script::CScript* script);   // 0x004A1130
    bool ScriptSetRainRate(Script::CStruct* params, Script::CScript* script);        // 0x004A1190
    bool ScriptSetRainColor(Script::CStruct* params, Script::CScript* script);       // 0x004A11B0: top=, bottom=
    bool ScriptSetSplashRate(Script::CStruct* params, Script::CScript* script);      // 0x004A1240
    bool ScriptSetSplashLife(Script::CStruct* params, Script::CScript* script);      // 0x004A1260
    bool ScriptSetSplashSize(Script::CStruct* params, Script::CScript* script);      // 0x004A1280
    bool ScriptSetSplashColor(Script::CStruct* params, Script::CScript* script);     // 0x004A12A0
    bool ScriptSetSplashBlendMode(Script::CStruct* params, Script::CScript* script); // 0x004A12F0
    bool ScriptSetSnowHeight(Script::CStruct* params, Script::CScript* script);      // 0x004A1350
    bool ScriptSetSnowFrames(Script::CStruct* params, Script::CScript* script);      // 0x004A1370
    bool ScriptSetSnowSize(Script::CStruct* params, Script::CScript* script);        // 0x004A13B0
    bool ScriptSetSnowBlendMode(Script::CStruct* params, Script::CScript* script);   // 0x004A13D0
    bool ScriptSetSnowRate(Script::CStruct* params, Script::CScript* script);        // 0x004A1430
    bool ScriptSetSnowColor(Script::CStruct* params, Script::CScript* script);       // 0x004A1450
    bool ScriptSetSnowActive(Script::CStruct* params, Script::CScript* script);      // 0x004A14A0: rain off
    bool ScriptSetRainActive(Script::CStruct* params, Script::CScript* script);      // 0x004A14B0

    void** m_vtable;
    float m_rainHeight;          // +0x04
    int m_rainFrames;            // +0x08
    float m_rainLength;          // +0x0C
    float m_rainRate;            // +0x10
    uint8_t m_rainTop[4];        // +0x14 RGBA
    uint8_t m_rainBottom[4];     // +0x18
    float m_splashRate;          // +0x1C
    int m_splashLife;            // +0x20
    float m_splashSize;          // +0x24
    uint8_t m_splashColour[4];   // +0x28
    int m_drops;                 // +0x2C (the PC's)
    bool m_active;               // +0x30
    uint8_t m_pad31[3];
    float m_snowHeight;          // +0x34
    int m_snowFrames;            // +0x38
    float m_snowSize;            // +0x3C
    float m_snowRate;            // +0x40
    uint8_t m_snowColour[4];     // +0x44
    uint8_t m_platform48[0x10];  // +0x48 (the PC's)
    bool m_raining;              // +0x58
};
static_assert(offsetof(CWeather, m_active) == 0x30 && offsetof(CWeather, m_snowColour) == 0x44 &&
                  offsetof(CWeather, m_raining) == 0x58,
              "layout must match the original");

// 0x004A1090 / 0x004A10A0: tail jumps to `this`'s virtual [1] / [3], the arguments left as they are (the linker
// folded every such dispatcher into these two: the weather's UpdateGrid / Render, a primitive's texture / layout,
// a viewport camera's projection, an event listener's filter, a sector's...).
void VirtualSlot1Jump();
void VirtualSlot3Jump();

} // namespace Nx
